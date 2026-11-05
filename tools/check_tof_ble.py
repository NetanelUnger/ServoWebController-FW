"""Validate VL53L8CX streaming, scaling and re-subscription on an explicit board.

Requires bleak. Does not write GPIO or servo commands.
"""
import argparse
import asyncio
import json
from pathlib import Path
from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
TOF = '91bad493-b950-4226-aa2b-4ede9fa42f59'

async def main(args):
    device = await BleakScanner.find_device_by_address(args.address, timeout=15)
    assert device, 'Board did not advertise'
    frames, replies = asyncio.Queue(), asyncio.Queue()
    report = {'address': args.address, 'runs': []}
    async with BleakClient(device, timeout=20, winrt={'use_cached_services': False}) as client:
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))
        async def command(text):
            await client.write_gatt_char(RX, text.encode(), response=True)
            result = await asyncio.wait_for(replies.get(), 5)
            print(text, '->', result, flush=True)
            return result
        assert await command('PING') == 'OK PING'
        try:
            response = await command('TOF_SET_MIN_MAX 0 4000')
            assert response.startswith('OK') and '4000' in response
            for run in range(2):
                await client.start_notify(TOF, lambda _, data: frames.put_nowait(bytes(data)))
                samples = []
                for n in range(12):
                    frame = await asyncio.wait_for(frames.get(), 35 if n == 0 else 3)
                    assert len(frame) == 64, len(frame)
                    samples.append(list(frame))
                valid = [v for frame in samples for v in frame if v != 255]
                assert valid, 'All zones invalid; check the optical scene'
                report['runs'].append({'frames': len(samples), 'valid_zones': len(valid),
                                       'min_code': min(valid), 'max_code': max(valid),
                                       'last_frame': samples[-1]})
                print('Run', run + 1, report['runs'][-1], flush=True)
                await client.stop_notify(TOF)
                await asyncio.sleep(1)
                while not frames.empty():
                    frames.get_nowait()
                assert await command('PING') == 'OK PING'
        finally:
            # Restore the project's default encoding range after testing.
            await command('TOF_SET_MIN_MAX 0 500')
    report['pass'] = True
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 64-zone TOF notifications, scaling, stop/re-subscribe, NUS responsiveness')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True)
    parser.add_argument('--report')
    asyncio.run(main(parser.parse_args()))
