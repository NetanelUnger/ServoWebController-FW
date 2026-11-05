"""Exercise Web's CCCD stop -> mode write -> CCCD start -> readback lifecycle."""
import argparse
import asyncio
import struct
import time
from bleak import BleakClient, BleakScanner

DATA = '91bad4a1-b950-4226-aa2b-4ede9fa42f59'
MODE = '91bad4a3-b950-4226-aa2b-4ede9fa42f59'

async def wait_mode(client, expected):
    deadline = time.monotonic() + 4
    while True:
        actual = bytes(await client.read_gatt_char(MODE))
        if actual == bytes([expected]):
            return
        assert time.monotonic() < deadline, ('Mode readback timeout', expected, actual)
        await asyncio.sleep(.12)

async def main(address):
    device = await BleakScanner.find_device_by_address(address, timeout=20)
    assert device, 'Board not advertising'
    queue = asyncio.Queue()
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        await wait_mode(client, 0)
        await client.start_notify(DATA, lambda _, b: queue.put_nowait(bytes(b)))
        for mode in [1, 2, 0, 1, 0, 2, 1, 2, 0]:
            start = time.monotonic()
            await client.stop_notify(DATA)
            await wait_mode(client, 0)
            await client.write_gatt_char(MODE, bytes([mode]), response=True)
            # Accepted while unsubscribed, but not applied until CCCD resumes.
            await wait_mode(client, 0)
            while not queue.empty():
                queue.get_nowait()
            await client.start_notify(DATA, lambda _, b: queue.put_nowait(bytes(b)))
            await wait_mode(client, mode)
            elapsed = time.monotonic() - start
            if mode:
                while True:
                    packet = await asyncio.wait_for(queue.get(), 5)
                    if packet[0] == mode:
                        assert struct.unpack_from('<I', packet, 40)[0] == 0, 'I2C errors'
                        break
            else:
                await asyncio.sleep(.3)
                while not queue.empty():
                    assert queue.get_nowait()[0] == 3, 'Measurement while off'
            print(f'PASS stop/write/start/read mode={mode} in {elapsed:.3f}s', flush=True)
        # Critical regression: disconnect with a nonzero request queued while
        # notifications are OFF. A later subscriber must not inherit it.
        await client.stop_notify(DATA)
        await client.write_gatt_char(MODE, b'\x02', response=True)
    # Windows may retain a physical link after disposing a GATT client. Only
    # a fresh advertisement proves the controller actually disconnected.
    device = await BleakScanner.find_device_by_address(address, timeout=20)
    assert device, 'Physical disconnect not observed: Windows still holds the BLE link'
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        await wait_mode(client, 0)
        while not queue.empty():
            queue.get_nowait()
        await client.start_notify(DATA, lambda _, b: queue.put_nowait(bytes(b)))
        await asyncio.sleep(1.2)
        await wait_mode(client, 0)
        while not queue.empty():
            packet = queue.get_nowait()
            assert packet[0] == 3 and packet[3] == 0, 'Previous connection mode leaked'
        await client.stop_notify(DATA)
    print('PASS reconnect reads OFF; pending unsubscribed request was cleared', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    asyncio.run(main(parser.parse_args().address))
