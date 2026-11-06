"""Real BLE display regression. Draws test patterns; connection enables board power.
Requires bleak. Never moves servos. Use --raw-rows 240 for a full raw image.
"""
import argparse
import asyncio
import json
import time
from datetime import datetime, timezone
from pathlib import Path
from bleak import BleakClient, BleakScanner

COMMAND = '91bad4c1-b950-4226-aa2b-4ede9fa42f59'

async def check(args):
    device = await BleakScanner.find_device_by_address(args.address, timeout=15)
    if device is None:
        raise RuntimeError('Board not advertising; disconnect the Web client first')
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'address': args.address, 'checks': [], 'writes': 0, 'row_chunk': args.row_chunk}
    started = time.monotonic()
    async with BleakClient(device, winrt={'use_cached_services': False}, timeout=20) as client:
        async def write(data):
            await asyncio.wait_for(client.write_gatt_char(COMMAND, bytes(data), response=True), 5)
            report['writes'] += 1
        for name, data in [('full_screen_fill', [1,0,0,240,240,255,255]),
                           ('row_one_pixel', [3,239,239,255,255]),
                           ('rectangle_pixels', [2,0,0,2,1,248,0,7,224]),
                           ('fill_120', [1,0,1,120,1,0,31])]:
            command_started = time.monotonic()
            await write(data)
            if name == 'full_screen_fill': report['full_fill_seconds'] = round(time.monotonic()-command_started,3)
            report['checks'].append(name)
        for name, data in [('odd_row_payload', [3,0,0,0]), ('row_overflow', [3,239,0,0,0,0,0])]:
            try:
                await client.write_gatt_char(COMMAND, bytes(data), response=True)
            except Exception as error:
                # Windows exposes application error 0x80 from the peripheral.
                if '0x80' not in str(error).lower():
                    raise
                report['checks'].append(name + '_rejected')
            else:
                raise AssertionError(name + ' unexpectedly accepted')
            await write([3,0,0,0,0])
        report['mtu'] = client.mtu_size
        raw_start = time.monotonic()
        # Same WRITE_ROW packets as the Web sender, many HAL tick phases.
        for y in range(args.raw_rows):
            for x in range(0,240,args.row_chunk):
                pixels=[]
                for col in range(x,min(240,x+args.row_chunk)):
                    color=((col*31//239)<<11) | ((y*63//max(1,args.raw_rows-1))<<5) | (31-col*31//239)
                    pixels.extend([color>>8,color&255])
                await write([3,x,y]+pixels)
            if y%4==3: print('raw rows',y+1,'writes',report['writes'],flush=True)
        report['raw_seconds'] = round(time.monotonic()-raw_start,2)
        report['checks'].append(f'raw_rows_{args.raw_rows}')
        if not args.skip_fill:
            for y in range(240):
                color=(0xf800,0x07e0,0x001f)[y//80]
                for x in (0,120): await write([1,x,y,120,1,color>>8,color&255])
                if y%40==39: print('fill rows',y+1,flush=True)
            report['checks'].append('full_240x240_fill_image')
    report['seconds']=round(time.monotonic()-started,2)
    report['status']='PASS'
    if args.output: Path(args.output).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--row-chunk',type=int,default=8,choices=range(1,123))
    parser.add_argument('--raw-rows',type=int,default=8,choices=range(1,241))
    parser.add_argument('--skip-fill',action='store_true')
    parser.add_argument('--output')
    asyncio.run(check(parser.parse_args()))
