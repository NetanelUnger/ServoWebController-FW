"""Verify ADC RAW framing, NUS coexistence and notification stop/restart on hardware."""
import argparse
import asyncio
import struct
import time
from bleak import BleakClient, BleakScanner

RAW = '91bad4b1-b950-4226-aa2b-4ede9fa42f59'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'


def decode(payload, valid=True):
    assert len(payload) == 10, payload.hex()
    version, mask, battery, current1, current2, battery_mv = struct.unpack('<BBHHHH', payload)
    assert version == 2 and mask & ~7 == 0, payload.hex()
    if valid:
        assert mask == 7, ('ADC conversion failure', payload.hex())
    assert all(0 <= value <= 4095 for value in (battery, current1, current2))
    assert battery_mv == ((battery * 7830 + 1556) // 3112 if mask & 1 else 0)
    return battery, current1, current2


async def main(address):
    device = await BleakScanner.find_device_by_address(address, timeout=15)
    assert device, 'No advertisement'
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        characteristic = client.services.get_characteristic(RAW)
        assert characteristic and {'read', 'notify'} <= set(characteristic.properties)
        assert not {'write', 'write-without-response'} & set(characteristic.properties)
        decode(bytes(await client.read_gatt_char(RAW)), valid=False)
        frames, replies = asyncio.Queue(), asyncio.Queue()
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data)))
        await client.start_notify(RAW, lambda _, data: frames.put_nowait((time.monotonic(), bytes(data))))
        samples = []
        for index in range(25):
            timestamp, payload = await asyncio.wait_for(frames.get(), 4)
            samples.append((timestamp, decode(payload)))
            if index in (5, 10, 15):
                name = ('ADC_BAT', 'ADC1', 'ADC2')[(index // 5) - 1]
                await client.write_gatt_char(RX, f'ADC_READ {name}'.encode(), response=True)
                response = await asyncio.wait_for(replies.get(), 4)
                assert b'OK ADC_READ' in response and name.encode() in response, response
                print('NUS', response.decode().strip(), flush=True)
        rate = (len(samples) - 1) / (samples[-1][0] - samples[0][0])
        assert 2 <= rate <= 7, ('Unexpected rate', rate)
        print(f'PASS 25 valid frames at {rate:.2f} Hz; BAT/ADC1/ADC2={samples[-1][1]}', flush=True)
        decode(bytes(await client.read_gatt_char(RAW)))
        await client.stop_notify(RAW)
        await asyncio.sleep(0.7)
        while not frames.empty():
            frames.get_nowait()
        await asyncio.sleep(0.8)
        assert frames.empty(), 'Notifications after stop'
        stopped = bytes(await client.read_gatt_char(RAW))
        decode(stopped, valid=False)
        assert stopped[1] == 0, ('Stale validity after stop', stopped.hex())
        await client.start_notify(RAW, lambda _, data: frames.put_nowait((time.monotonic(), bytes(data))))
        for _ in range(5):
            _, payload = await asyncio.wait_for(frames.get(), 4)
            decode(payload)
        await client.stop_notify(RAW)
        await client.stop_notify(TX)
        print('PASS READ, NUS ADC_READ coexistence, stop and restart', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    asyncio.run(main(parser.parse_args().address))
