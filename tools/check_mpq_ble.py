"""On-board MPQ link gating test: HELP, connected GPIO levels, disconnected IDR/ODR.

Connects to the specified board, intentionally enabling both MPQ power paths.
Requires bleak and STM32CubeProgrammer. Resets after each SWD observation.
"""
import argparse
import asyncio
import re
import subprocess
import sys
from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
PINS = [('EN1', 'PB5'), ('EN2', 'PB4'), ('DIAG_EN1', 'PA10'), ('DIAG_EN2', 'PA11')]

def check_off(programmer):
    result = subprocess.run([programmer, '-c', 'port=SWD', 'mode=HOTPLUG',
                             '-r32', '0x42020010', '8', '-r32', '0x42020410', '8', '-r32', '0x2000bd5c', '8', '-r32', '0x2000bcd0', '16'],
                            capture_output=True, text=True, errors='replace', check=True)
    try:
        for address, mask in [('42020010', 0xC00), ('42020410', 0x30)]:
            match = re.search(r'0x' + address + r'\s*:\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)', result.stdout)
            assert match, result.stdout
            assert all(int(word, 16) & mask == 0 for word in match.groups()), result.stdout
        print('OFF verified: PA10/PA11/PB4/PB5 input and output registers low', flush=True)
    finally:
        subprocess.run([programmer, '-c', 'port=SWD', 'mode=HOTPLUG', '-rst'],
                       capture_output=True, check=True)

async def main(args):
    if not args.connected_only:
        check_off(args.programmer)
        for cycle in range(2):
            subprocess.run([sys.executable, __file__, '--address', args.address,
                            '--programmer', args.programmer, '--connected-only'], check=True)
            # End the WinRT process so Windows releases all GATT references.
            await asyncio.sleep(5)
            check_off(args.programmer)
        print('PASS: startup OFF, connection ON, disconnect OFF, reconnect ON/OFF, HELP')
        return
    for cycle in range(1):
        device = await BleakScanner.find_device_by_address(args.address, timeout=15)
        assert device, 'Board not advertising'
        async with BleakClient(device, winrt={'use_cached_services': False}) as client:
            replies = asyncio.Queue()
            await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))
            async def command(text):
                await client.write_gatt_char(RX, text.encode(), response=True)
                return await asyncio.wait_for(replies.get(), 5)
            await client.write_gatt_char(RX, b'HELP', response=True)
            lines = [await asyncio.wait_for(replies.get(), 5) for _ in range(16)]
            assert lines[0] == 'OK HELP' and lines[-1] == 'OK HELP END'
            assert 'GPIO_GET <pin>' in lines and 'ADC_READ <pin>' in lines
            for signal, pin in PINS:
                assert await command('GPIO_GET ' + signal) == f'OK GPIO_GET {signal} 1'
                assert await command('GPIO_GET ' + pin) == f'OK GPIO_GET {signal} 1'
                for target in [signal, pin]:
                    for value in [0, 1]:
                        assert await command(f'GPIO_SET {target} {value}') == '- READ ONLY'
            assert await command('PING') == 'OK PING'
            print(f'Cycle {cycle+1}: HELP complete; four pins high; manual writes rejected', flush=True)
            await client.stop_notify(TX)
            client._backend._session.maintain_connection = False
        del client
        import gc
        gc.collect()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--programmer', required=True)
    parser.add_argument('--connected-only', action='store_true')
    asyncio.run(main(parser.parse_args()))
