"""Verify servo READ never emits telemetry and current-value writes preserve state.

Requires bleak. Use --pca-disabled for a build that rejects all servo writes.
"""
import asyncio
import struct
from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
SERVO = '6e400004-b5a3-f393-e0a9-e50e24dcca9e'
TOF = '91bad493-b950-4226-aa2b-4ede9fa42f59'

async def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--pca-disabled', action='store_true')
    args = parser.parse_args()
    device = await BleakScanner.find_device_by_address(args.address, timeout=15)
    assert device, 'Board not advertising'
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        char = client.services.get_characteristic(SERVO)
        assert char and {'read', 'write', 'notify'} <= set(char.properties)
        before = bytes(await client.read_gatt_char(SERVO))
        assert len(before) == 32
        values = struct.unpack('<16H', before)
        assert all(500 <= value <= 2500 for value in values)
        print('Servo characteristic read/write/notify, 16 values:', values, flush=True)
        notices, replies, frames = asyncio.Queue(), asyncio.Queue(), asyncio.Queue()
        await client.start_notify(SERVO, lambda _, data: notices.put_nowait(bytes(data)))
        assert bytes(await client.read_gatt_char(SERVO)) == before
        await asyncio.sleep(0.3)
        assert notices.empty(), "Servo READ unexpectedly emitted a notification"
        for payload in (b'\x00', before):
            try:
                await client.write_gatt_char(SERVO, payload, response=True)
            except Exception as error:
                assert payload == b'\x00' or args.pca_disabled, str(error)
                assert '0x80' in str(error) or '0x0D' in str(error), str(error)
            else:
                assert payload == before and not args.pca_disabled, 'Malformed or disabled write accepted'
        assert bytes(await client.read_gatt_char(SERVO)) == before
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))
        await client.write_gatt_char(RX, b'HELP', response=True)
        lines = []
        while not lines or lines[-1] != "OK HELP END":
            assert len(lines) < 40, "Unterminated HELP response"
            lines.append(await asyncio.wait_for(replies.get(), 5))
        assert lines[0] == 'OK HELP' and lines[-1] == 'OK HELP END'
        await client.write_gatt_char(RX, b'PING', response=True)
        assert await asyncio.wait_for(replies.get(), 5) == 'OK PING'
        await client.start_notify(TOF, lambda _, data: frames.put_nowait(bytes(data)))
        for i in range(4):
            frame = await asyncio.wait_for(frames.get(), 35 if i == 0 else 3)
            assert len(frame) == 64
        print('PASS: non-notifying servo READ, valid/disabled and malformed writes, HELP, PING, TOF', flush=True)

if __name__ == '__main__':
    asyncio.run(main())
