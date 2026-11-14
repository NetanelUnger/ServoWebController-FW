"""BLE v2 storage/display/timeline regression; no formatting or existing-file edits.

Backs up and verifies every existing live file, writes a temporary RGB332 image
and a 4096-event timeline that holds CH0 at its starting target, plus a temporary
safe pose matching all starting targets, then deletes only those test IDs.
Requires an already mounted v2 store and --run. Connection
and disconnect apply the board's normal motor power/safe-pose policy.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib
from bleak import BleakClient, BleakScanner

COMMAND = '91bad4d1-b950-4226-aa2b-4ede9fa42f59'
DATA = '91bad4d2-b950-4226-aa2b-4ede9fa42f59'
STATUS = '91bad4d3-b950-4226-aa2b-4ede9fa42f59'
SERVO = '6e400004-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'


async def check(args, report):
    board = await BleakScanner.find_device_by_address(args.address, timeout=20)
    assert board, 'Board not advertising'
    async with BleakClient(board, winrt={'use_cached_services': False}) as client:
        replies = asyncio.Queue()
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))
        sequence = 0
        created = []

        async def nus(text):
            await client.write_gatt_char(RX, text.encode(), response=True)
            return await asyncio.wait_for(replies.get(), 8)

        async def wait(op, token):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                value = bytes(await client.read_gatt_char(STATUS))
                if len(value) >= 32 and value[0] == 2 and value[2] == op and struct.unpack_from('<H', value, 4)[0] == token:
                    assert value[1] != 3, ('Device error', value.hex())
                    if value[1] == 2:
                        return value
                await asyncio.sleep(.01)
            raise TimeoutError(('Storage operation', op, token))

        async def cmd(op, file_id=0, values=(), extra=b'', kind=0):
            nonlocal sequence
            sequence = sequence % 65535 + 1
            packet = struct.pack('<BBHI', op, kind, sequence, file_id)
            packet += b''.join(struct.pack('<I', v) for v in values) + extra
            await client.write_gatt_char(COMMAND, packet, response=True)
            return await wait(op, sequence)

        async def files():
            records = []
            for index in range(65):
                value = await cmd(0x25, values=(index,))
                if len(value) == 32:
                    return records
                assert len(value) == 128, value.hex()
                record = value[32:]
                fields = struct.unpack_from('<6I2H4B', record)
                row = dict(zip(('id', 'offset', 'length', 'capacity', 'crc', 'revision',
                                'width', 'height', 'type', 'layout', 'state', 'version'), fields))
                row['name'] = record[32:].split(b'\0')[0].decode('utf-8')
                records.append(row)
            raise AssertionError('Unbounded catalog')

        async def read(record):
            result = bytearray()
            for offset in range(0, record['length'], 208):
                value = await cmd(0x24, record['id'], (offset,))
                assert struct.unpack_from('<I', value, 12)[0] == offset
                result += value[32:]
            assert len(result) == record['length'] and zlib.crc32(result) == record['crc']
            return bytes(result)

        async def save(payload, kind, width=0, height=0):
            value = await cmd(0x20, values=(len(payload), zlib.crc32(payload)),
                              extra=bytes((width, height, 0, 1)), kind=kind)
            file_id = struct.unpack_from('<I', value, 8)[0]
            assert file_id not in {r['id'] for r in original}, 'Existing ID was reused'
            created.append(file_id)
            await cmd(0x2b, file_id, (0,), b'RAM TEST')
            nonlocal sequence
            for offset in range(0, len(payload), 224):
                sequence = sequence % 65535 + 1
                packet = struct.pack('<HH', sequence, offset // 16) + payload[offset:offset + 224]
                await client.write_gatt_char(DATA, packet, response=True)
                value = await wait(0x28, sequence)
                assert struct.unpack_from('<I', value, 12)[0] == min(offset + 224, len(payload))
            await cmd(0x21, file_id)
            record = next(row for row in await files() if row['id'] == file_id)
            assert await read(record) == payload
            return file_id

        assert await nus('STOP') == 'OK STOP'
        info = await cmd(0x26)
        assert info[6] == 1, 'Requires a mounted v2 file store; never formats automatically'
        original = await files()
        original_values = bytes(await client.read_gatt_char(SERVO))
        report['original_files'] = original
        report['original_servo'] = list(struct.unpack('<16H', original_values))
        backup = {}
        for record in original:
            if record['state'] == 1:
                payload = await read(record)
                backup[record['id']] = payload
                (args.artifacts / f"existing-{record['id']}.bin").write_bytes(payload)
        report['backup_sha256'] = {key: hashlib.sha256(payload).hexdigest() for key, payload in backup.items()}
        print('Backed up existing files', sorted(backup), flush=True)
        try:
            image = bytes((index * 19) & 255 for index in range(32 * 32))
            image_id = await save(image, 2, 32, 32)
            await cmd(0x22, image_id)
            report['checks'].append('rgb332_upload_readback_show')
            print('PASS RGB332 upload, exact readback and display completion', flush=True)
            pulse = struct.unpack_from('<H', original_values)[0]
            count = 4096
            timeline = struct.pack('<IHHIIIIII', 0x314c4d54, 1, 32, count, count * 2, 0, 2, 0, 0)
            timeline += b''.join(struct.pack('<IIHBBI', i * 2, 0, pulse, 0, 0, 0) for i in range(count)) + b'{}'
            timeline_id = await save(timeline, 3)
            assert await nus(f'TIMELINE_PLAY {timeline_id}') == f'OK TIMELINE_PLAY {timeline_id}'
            await asyncio.sleep(.3)
            report['timeline_status'] = await nus('TIMELINE_STATUS')
            fields = report['timeline_status'].split()
            assert fields[:2] == ['OK', 'TIMELINE_STATUS'] and int(fields[3]) == timeline_id, fields
            assert await nus('TIMELINE_STOP') == 'OK TIMELINE_STOP'
            assert bytes(await client.read_gatt_char(SERVO)) == original_values
            report['checks'].append('max_4096_event_timeline_crc_validate_play_stop')
            print('PASS 4096-event timeline round trip, playback and stop', flush=True)
            safe_pose = struct.pack('<IHH', 0x31504653, 1, 16) + original_values
            await save(safe_pose, 4)
            assert await nus('SAFE_POSE') == 'OK SAFE_POSE APPLIED'
            await asyncio.sleep(.2)
            assert bytes(await client.read_gatt_char(SERVO)) == original_values
            report['checks'].append('temporary_safe_pose_upload_readback_apply')
            print('PASS safe pose round trip and apply', flush=True)
        finally:
            # Stop playback before deleting only IDs created by this test.
            assert await nus('STOP') == 'OK STOP'
            await cmd(0x27)
            remaining = {r['id'] for r in await files()}
            for file_id in reversed(created):
                if file_id in remaining:
                    await cmd(0x23, file_id)
            await client.write_gatt_char(SERVO, original_values, response=True)
            current = await files()
            for original_record in original:
                if original_record['state'] == 1:
                    assert next(r for r in current if r['id'] == original_record['id']) == original_record
                    assert await read(original_record) == backup[original_record['id']]
            assert not any(r['state'] == 1 and r['id'] in created for r in current)
            report['checks'].append('existing_files_byte_exact_and_test_files_deleted')
            print('PASS original files preserved; temporary files deleted', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--artifacts', required=True, type=Path)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        parser.error('Hardware test requires --run')
    args.artifacts.mkdir(parents=True, exist_ok=False)
    report = dict(checks=[])
    try:
        asyncio.run(check(args, report))
        report['result'] = 'passed'
    except BaseException as error:
        report.update(result='failed', error=repr(error))
        raise
    finally:
        (args.artifacts / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
