"""Validate IMU identity, raw packets, timestamps and resubscription on hardware.
Does not send motor commands. Requires bleak; use --address for the target board.
"""
import argparse
import asyncio
import json
import math
import struct
from bleak import BleakClient, BleakScanner

RAW = '91bad4a1-b950-4226-aa2b-4ede9fa42f59'
MODE = '91bad4a3-b950-4226-aa2b-4ede9fa42f59'
SERVICE = '91bad4a0-b950-4226-aa2b-4ede9fa42f59'

async def main(address):
    device = await BleakScanner.find_device_by_address(address, timeout=20)
    assert device, 'Board not advertising'
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        assert client.services.get_service(SERVICE), 'IMU service missing'
        for cycle in range(2):
            queue = asyncio.Queue()
            await client.start_notify(RAW, lambda _, b: queue.put_nowait(bytes(b)))
            await client.write_gatt_char(MODE,b'\x01',response=True)
            previous = None
            frames = []
            for i in range(30):
                p = await asyncio.wait_for(queue.get(), 12 if i == 0 else 3)
                while p[0]==3:
                    assert p[1]==0,p.hex()
                    p = await asyncio.wait_for(queue.get(), 12)
                assert len(p) == 48 and p[0] == 1, p.hex()
                assert p[1] & 0x17 == 0x17 and p[2:4] == b'\x73\x6b', p.hex()
                seq, uptime, sensor_time = struct.unpack_from('<III', p, 4)
                assert p[36:40] == bytes([1,4,4,6]), p.hex()
                assert struct.unpack_from('<I',p,40)[0] == 0, 'I2C failures'
                if previous:
                    assert (seq-previous[0]) & 0xffffffff > 0
                    assert (sensor_time-previous[1]) & 0xffffffff > 0
                previous = seq,sensor_time
                a = [v*0.000122 for v in struct.unpack_from('<3h',p,16)]
                g = [v*0.070 for v in struct.unpack_from('<3h',p,22)]
                h = [v*0.010417 for v in struct.unpack_from('<3h',p,28)]
                t = 25 + struct.unpack_from('<h',p,34)[0]/256
                frames.append(dict(seq=seq, uptime_ms=uptime, acc_g=a, gyro_dps=g, high_g=h, temp_c=t))
            print(json.dumps({'cycle':cycle+1,'frames':len(frames),'first':frames[0],'last':frames[-1]},indent=2),flush=True)
            assert any(math.sqrt(sum(v*v for v in f['acc_g'])) > 0.1 for f in frames)
            await client.stop_notify(RAW)
            await asyncio.sleep(0.4)
            stopped = bytes(await client.read_gatt_char(RAW))
            assert stopped[0] == 3 and stopped[3] == 0, 'Streaming did not stop'
        print('PASS: identity, 60 RAW frames, monotonic timestamps, no I2C errors, stop/restart')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    asyncio.run(main(parser.parse_args().address))
