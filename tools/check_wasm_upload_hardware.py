"""Upload browser-generated Wasm and exercise the real file-to-VM path.

Requires --run. Never formats storage. Existing files are read and hashed before
and after. HOLD uses the current CH0 target; no deliberate movement. Test files
are deleted in finally. Board connection/disconnection retains its safe-pose policy.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import zlib
from bleak import BleakClient, BleakScanner
from check_file_store_hardware import COMMAND, DATA, STATUS, SERVO, RX, TX
from imu_winrt_shared import SharedImuClient


async def check(args, report):
    if args.winrt_shared:
        connection = SharedImuClient(args.address, ['6e400001-b5a3-f393-e0a9-e50e24dcca9e', '91bad4d0-b950-4226-aa2b-4ede9fa42f59'])
    else:
        board = await BleakScanner.find_device_by_address(args.address, timeout=20)
        assert board, 'Board not advertising'
        connection = BleakClient(board, winrt={'use_cached_services': False})
    created = []
    report['created'] = created
    async with connection as client:
        replies = asyncio.Queue()
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))
        sequence = 0

        async def nus(text, end=None):
            assert replies.empty(), 'Unexpected pending NUS reply'
            await client.write_gatt_char(RX, text.encode(), response=True)
            answer = ''
            while True:
                answer += await asyncio.wait_for(replies.get(), 10)
                if end is None or answer.endswith(end): return answer

        async def status():
            text = await nus('WASM_STATUS', 'END')
            result = dict((k, int(v)) for k,v in re.findall(r'(id|n|r|calls|pool|clean|stack|max_ms|stop_ms|imu|tof)=(-?\d+)', text))
            result['state'] = re.match(r'OK WASM_STATUS (\w+)', text)[1].split('id')[0]
            result['raw'] = text
            return result

        async def until(states):
            deadline = time.monotonic()+8
            while time.monotonic()<deadline:
                value = await status()
                if value['state'] in states: return value
                await asyncio.sleep(.1)
            raise AssertionError(value)

        async def wait(op, token):
            deadline = time.monotonic()+30
            while time.monotonic()<deadline:
                value = bytes(await client.read_gatt_char(STATUS))
                if len(value)>=32 and value[0]==2 and value[2]==op and struct.unpack_from('<H', value, 4)[0]==token:
                    assert value[1]!=3, ('Storage error', value.hex())
                    if value[1]==2: return value
                await asyncio.sleep(.01)
            raise TimeoutError(('Storage', op))

        async def cmd(op, file_id=0, values=(), extra=b'', kind=0):
            nonlocal sequence
            sequence = sequence%65535+1
            packet = struct.pack('<BBHI', op, kind, sequence, file_id)+b''.join(struct.pack('<I', v) for v in values)+extra
            await client.write_gatt_char(COMMAND, packet, response=True)
            return await wait(op, sequence)

        async def files():
            records = []
            for index in range(65):
                value = await cmd(0x25, values=(index,))
                if len(value)==32: return records
                assert len(value)==128
                values = struct.unpack_from('<6I2H4B', value, 32)
                records.append(dict(zip(('id','offset','length','capacity','crc','revision','width','height','type','layout','state','version'), values)))
            raise AssertionError('Unbounded catalog')

        async def read(record):
            data = bytearray()
            for offset in range(0, record['length'], 208): data += (await cmd(0x24, record['id'], (offset,)))[32:]
            assert len(data)==record['length'] and zlib.crc32(data)==record['crc']
            return bytes(data)

        async def save(name, payload):
            nonlocal sequence
            reply = await cmd(0x20, values=(len(payload), zlib.crc32(payload)), extra=b'\0\0\0\1', kind=5)
            file_id = struct.unpack_from('<I', reply, 8)[0]
            assert file_id not in originals
            created.append(file_id)
            encoded_name=name.encode()[:32]
            for at in range(0,len(encoded_name),8): await cmd(0x2b, file_id, (at,), encoded_name[at:at+8])
            for offset in range(0, len(payload), 224):
                sequence = sequence%65535+1
                await client.write_gatt_char(DATA, struct.pack('<HH', sequence, offset//16)+payload[offset:offset+224], response=True)
                await wait(0x28, sequence)
            await cmd(0x21, file_id)
            row = next(r for r in await files() if r['id']==file_id)
            assert await read(row)==payload
            return file_id

        assert await nus('STOP')=='OK STOP'
        report['info'] = await nus('WASM_INFO', 'OK WASM_INFO END')
        assert report['info'].startswith('OK WASM_INFO 1 2048 ')
        assert (await cmd(0x26))[6]==1, 'Requires existing v2 store; never formats'
        originals = {}
        for row in await files():
            if row['state']==1: originals[row['id']] = hashlib.sha256(await read(row)).hexdigest()
        report['originals'] = originals
        print('Backed up original files:', list(originals), flush=True)
        original_servo = bytes(await client.read_gatt_char(SERVO))
        try:
            ids = {}
            for name in ('sensor','counter','hold','loop'):
                ids[name] = await save('WASM TEST '+name, (args.modules/(name+'.wasm')).read_bytes())
                print('Uploaded browser module:', name, ids[name], flush=True)
            ids['invalid'] = await save('WASM TEST invalid', b'\0asm\1\0\0\0')
            report['programs'] = {}
            for name in ('counter','sensor','hold','loop','invalid'):
                assert await nus(f'WASM_FILE {ids[name]}')==f'OK WASM_FILE {ids[name]}'
                expected = {'TRAP'} if name=='loop' else {'ERROR'} if name=='invalid' else {'RUNNING'}
                first = await until(expected)
                if name in ('counter','sensor','hold'):
                    await asyncio.sleep(2)
                    running = await status(); assert running['n']>first['n']
                    report['programs'][name] = running
                    if name=='counter': assert running['r']==running['n']
                    if name=='sensor':
                        # Cold TOF firmware initialization and IMU settling share
                        # I2C. Unavailable snapshots during warmup are expected.
                        deadline=time.monotonic()+20
                        while not (running['imu']>0 and running['tof']>0) and time.monotonic()<deadline:
                            await asyncio.sleep(.25);running=await status()
                        report['programs'][name]=running
                        assert running['state']=='RUNNING' and running['imu']>0 and running['tof']>0, running
                        await asyncio.sleep(.3);fresh=await status()
                        assert fresh['imu']>running['imu'] and fresh['tof']>running['tof'], fresh
                    if name=='hold':
                        assert running['r']==0 and bytes(await client.read_gatt_char(SERVO))==original_servo
                        target = struct.unpack_from('<H', original_servo)[0]
                        assert await nus(f'SET_DIRECT 0 {target}')=='- MOTION ACTIVE: STOP'
                    report['programs'][name] = running
                    assert await nus('WASM_STOP')=='OK WASM_STOP'
                    stopped = await until({'STOPPED'}); assert stopped['clean']==24
                else:
                    report['programs'][name] = first
                    assert first['clean']==24
                print('PASS program:', name, report['programs'][name]['state'], flush=True)
            # Loaded bytecode is independent of Flash after the copy lease ends.
            file_id = ids['counter']
            assert await nus(f'WASM_FILE {file_id}')==f'OK WASM_FILE {file_id}'
            await until({'RUNNING'}); await cmd(0x23, file_id); created.remove(file_id)
            first = await status(); await asyncio.sleep(.2); second = await status()
            assert second['n']>first['n'] and second['state']=='RUNNING'
            report['delete_running_copy'] = second
        finally:
            await nus('STOP'); await until({'STOPPED','IDLE','TRAP','ERROR','DONE'})
            await cmd(0x27) # Ends only this connection's interrupted upload, if any.
            live={row['id'] for row in await files() if row['state']==1}
            for file_id in created:
                if file_id in live: await cmd(0x23, file_id)
            current = {}
            for row in await files():
                if row['state']==1: current[row['id']] = hashlib.sha256(await read(row)).hexdigest()
            assert current==originals, 'Original file content changed'
            report['originals_preserved'] = True
        report['status'] = 'passed'
        print('PASS uploaded programs, RAM copy lifetime and original file hashes', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--winrt-shared', action='store_true', help='Use explicit shared service access when another Windows client owns GATT')
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--modules', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if not args.run: parser.error('--run is required')
    report = {'status':'running'}
    try: asyncio.run(check(args, report))
    except BaseException as error:
        report.update(status='failed', error=repr(error)); raise
    finally: args.output.write_text(json.dumps(report, indent=2)+'\n')


if __name__=='__main__': main()
