"""Inspect a SWD RAM dump using the matching firmware ELF (no target execution).

Capture after disconnecting BLE with CubeProgrammer -c port=SWD mode=HOTPLUG
 -u 0x20000000 0x20000 snapshot.bin. Debug access can disturb real-time radio
operation; reset the board before further BLE tests. A live dump is not atomic:
retry if task list validation fails. Watermarks measure touched bytes, not a proof of
worst-case stack demand. GCC .su files and untested paths must also be reviewed.
Requires the STM32 GNU nm and gdb tools; uses GDB only to read ELF type layouts.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess


def inspect(elf, ram_file, toolchain):
    raw = ram_file.read_bytes()
    base = 0x20000000

    def data(address, count):
        assert base <= address <= base + len(raw) - count, hex(address)
        return raw[address - base:address - base + count]

    def word(address):
        return struct.unpack('<I', data(address, 4))[0]

    symbols = {}
    nm = subprocess.check_output([str(toolchain / 'arm-none-eabi-nm.exe'),
                                  '-S', '--defined-only', str(elf)], text=True)
    for line in nm.splitlines():
        match = re.fullmatch(r'([0-9a-f]+) (?:([0-9a-f]+) )?\w (.+)', line)
        if match:
            address, size, name = match.groups()
            symbols[name] = (int(address, 16), int(size or '0', 16))

    fields = {
        'tcb_stack': ('TCB_t', 'pxStack'), 'tcb_name': ('TCB_t', 'pcTaskName'),
        'tcb_sp': ('TCB_t', 'pxTopOfStack'), 'tcb_priority': ('TCB_t', 'uxPriority'),
        'list_count': ('List_t', 'uxNumberOfItems'), 'list_end': ('List_t', 'xListEnd'),
        'item_next': ('ListItem_t', 'pxNext'), 'item_owner': ('ListItem_t', 'pvOwner'),
    }
    expressions = {key: f'(unsigned)&(({typ}*)0)->{field}'
                   for key, (typ, field) in fields.items()}
    expressions['list_size'] = 'sizeof(List_t)'
    expressions['heap_header_size'] = 'sizeof(BlockLink_t)'
    expressions['heap_size_offset'] = '(unsigned)&((BlockLink_t*)0)->xBlockSize'
    command = [str(toolchain / 'arm-none-eabi-gdb.exe'), '-q', '-batch', str(elf)]
    for key, expression in expressions.items():
        command += ['-ex', f'printf "{key}=%u\\n", {expression}']
    layout = subprocess.check_output(command, text=True, stderr=subprocess.STDOUT)
    offsets = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', layout)}
    assert offsets.keys() >= expressions.keys(), layout

    tasks = set()
    for name in ('pxReadyTasksLists', 'xDelayedTaskList1', 'xDelayedTaskList2',
                 'xSuspendedTaskList', 'xTasksWaitingTermination'):
        if name not in symbols:
            continue
        address, size = symbols[name]
        for location in range(address, address + size, offsets['list_size']):
            count = word(location + offsets['list_count'])
            assert count <= 32, (name, count)
            end = location + offsets['list_end']
            item = word(end + offsets['item_next'])
            for _ in range(count):
                assert item != end, ('Live list changed; recapture', name)
                tasks.add(word(item + offsets['item_owner']))
                item = word(item + offsets['item_next'])
            assert item == end, ('Live list changed; recapture', name)

    expected = word(symbols['uxCurrentNumberOfTasks'][0])
    assert len(tasks) == expected, ('Live list changed; recapture', len(tasks), expected)
    heap_address, heap_size = symbols['ucHeap']
    rows = []
    for task in tasks:
        stack = word(task + offsets['tcb_stack'])
        name = data(task + offsets['tcb_name'], 16).split(b'\0', 1)[0].decode('ascii')
        allocation = next(((n, s) for n, (a, s) in symbols.items()
                           if a == stack and s > 4), None)
        if allocation:
            stack_bytes = allocation[1]
            source = allocation[0]
        else:
            assert heap_address <= stack < heap_address + heap_size, (name, hex(stack))
            header = (offsets['heap_header_size'] + 7) & ~7
            block = word(stack - header + offsets['heap_size_offset'])
            assert block & 0x80000000, (name, hex(block))
            stack_bytes = (block & 0x7fffffff) - header
            source = 'FreeRTOS heap block (includes allocator alignment)'
        contents = data(stack, stack_bytes)
        untouched = next((i for i, value in enumerate(contents) if value != 0xA5), stack_bytes)
        free_bytes = untouched // 4 * 4
        rows.append(dict(name=name, allocated_bytes=stack_bytes,
                         untouched_bytes=free_bytes, touched_bytes=stack_bytes-free_bytes,
                         saved_sp_margin=word(task + offsets['tcb_sp'])-stack,
                         priority=word(task + offsets['tcb_priority']), source=source))
    end = symbols['_end'][0]
    msp_bottom = symbols['_estack'][0] - symbols['_Min_Stack_Size'][0]
    brk = word(symbols['__sbrk_heap_end'][0]) or end
    return dict(elf=str(elf), ram=str(ram_file), tasks=sorted(rows, key=lambda r: r['name']),
                tick=word(symbols['xTickCount'][0]),
                freertos_heap=dict(total=heap_size,
                    free=word(symbols['xFreeBytesRemaining'][0]),
                    minimum_free=word(symbols['xMinimumEverFreeBytesRemaining'][0])),
                libc_heap=dict(capacity=msp_bottom-end, sbrk_committed=brk-end,
                               uncommitted=msp_bottom-brk),
                msp_reserved=symbols['_Min_Stack_Size'][0])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--ram', required=True, type=Path)
    parser.add_argument('--toolchain', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = inspect(args.elf, args.ram, args.toolchain)
    encoded = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(encoded, encoding='utf-8')
    print(encoded)
