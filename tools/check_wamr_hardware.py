"""Qualify the embedded WAMR guest over BLE. Requires --run and a matching build.

HOLD resubmits the current CH0 target; no new servo target or persistent file is
created. --cycle-radio interrupts all Bluetooth links to test real disconnect.
"""
import argparse
import asyncio
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time
from bleak import BleakClient
from check_ble_transport_hardware import Session, find_board, TX, MODE, IMU, TOF, ADC, SERVO, check_stats
from ble_radio_cycle import cycle_bluetooth


async def vm_status(s):
    assert s.replies.empty()
    await s.send('WASM_STATUS')
    lines = []
    for _ in range(15):
        line = await s.reply()
        lines.append(line)
        if line == 'END':
            break
    assert lines[-1] == 'END', lines
    assert lines[0].startswith('OK WASM_STATUS '), lines
    fields = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', '\n'.join(lines))}
    return dict(state=lines[0].split()[-1], lines=lines, **fields)


async def start(s, name):
    assert s.replies.empty()
    await s.send('WASM_RUN ' + name)
    await s.reply('OK WASM_RUN')


async def completed(s, state):
    deadline = time.monotonic() + 5
    while True:
        result = await vm_status(s)
        if result['state'] not in ('QUEUED', 'LOADING', 'RUNNING'):
            assert result['state'] == state, result
            assert result['clean'] == 24, result
            assert result['stack'] >= 1024, result
            return result
        assert time.monotonic() < deadline, result
        await asyncio.sleep(.02)


async def check(args, report):
    device = await find_board(args.address, args.name)
    async with BleakClient(device, winrt={'use_cached_services': False}) as client:
        s = Session(client, report)
        await s.subscribe(TX)
        original = await s.values()
        original_mode = await s.read(MODE)
        report['original_servo'] = original
        report['original_mode'] = original_mode.hex()
        try:
            await s.command('STOP')
            before = (await s.stats())['fields']
            initial = await vm_status(s)
            assert initial['state'] == 'IDLE', initial
            for name, state, result in [('MATH', 'DONE', 3700), ('HOLD', 'DONE', 0),
                                        ('BAD_CHANNEL', 'DONE', -1), ('BAD_PULSE', 'DONE', -1),
                                        ('LOOP', 'TRAP', None), ('OOB', 'TRAP', None)]:
                await start(s, name)
                value = await completed(s, state)
                if result is not None:
                    assert value['r'] == result, value
                if name == 'LOOP': assert any('instruction limit exceeded' in x for x in value['lines']), value
                if name == 'OOB': assert any('out of bounds' in x for x in value['lines']), value
                assert await s.values() == original, 'Guest changed servo targets'
                report['checks'][name] = value
                print(name, value, flush=True)
            for _ in range(20):
                for name, state in [('LOOP', 'TRAP'), ('OOB', 'TRAP'), ('MATH', 'DONE')]:
                    await start(s, name)
                    value = await completed(s, state)
                    assert value['pool'] == report['checks'][name]['pool'], value
                    if name == 'MATH': assert value['r'] == 3700, value
            report['fault_recovery_cycles'] = 20
            for text in ['WASM_RUN UNKNOWN', 'WASM_RUN MATH extra', 'WASM_STOP extra', 'WASM_STATUS extra']:
                await s.send(text)
                assert (await s.reply()).startswith('ERR '), text
            await s.sensor_load()
            await s.write(MODE, b'\x02')  # SFLP runs floating-point orientation conversion in defaultTask.
            await asyncio.sleep(2)
            assert await s.read(MODE) == b'\x02'
            for name in ('SENSORS', 'FLOAT'):
                await start(s, name)
                await s.send('WASM_RUN MATH')
                await s.reply('ERR WASM_RUN BUSY')
                started = time.monotonic()
                previous = 0
                samples = []
                while time.monotonic() - started < args.seconds:
                    await s.command('PING')
                    await s.snapshot(original)
                    value = await vm_status(s)
                    assert value['state'] == 'RUNNING' and value['n'] > previous, value
                    previous = value['n']
                    assert value['stack'] >= 1024 and value['pool'] < 12288, value
                    if name == 'SENSORS':
                        assert value['imu'] > 0 and value['tof'] > 0 and value['r'] >= 768, value
                    else:
                        assert value['r'] == 496768, value
                        packet = await s.read(IMU)
                        assert len(packet) == 48 and packet[0] == 2, packet.hex()
                        quaternion = struct.unpack_from('<4e', packet, 16)
                        assert all(math.isfinite(x) for x in quaternion), quaternion
                        assert .8 < sum(x*x for x in quaternion) < 1.2, quaternion
                    samples.append(value)
                    await asyncio.sleep(.1)
                await s.command('WASM_STOP')
                stopped = await completed(s, 'STOPPED')
                assert stopped['stop_ms'] <= 100, stopped
                report['soaks'][name] = dict(seconds=time.monotonic()-started, samples=samples, stopped=stopped)
                print('PASS soak', name, stopped, flush=True)
            for command in ('STOP', 'SAFE_POSE'):
                await start(s, 'SENSORS')
                await s.send(command)
                assert (await s.reply()).startswith('OK '+command)
                report['checks'][command] = await completed(s, 'STOPPED')
            after = (await s.stats())['fields']
            check_stats(before, after)
            assert not s.invalid_frames, s.invalid_frames
            assert all(s.counts[x] > 0 for x in ('imu', 'tof', 'adc')), dict(s.counts)
            report['notifications'] = dict(s.counts)
        finally:
            await s.command('WASM_STOP')
            await s.command('STOP')
            await s.snapshot(original)
            await s.write(MODE, original_mode)
            assert await s.values() == original, 'Target restoration failed'
            for uuid in (IMU, TOF, ADC, SERVO): await s.unsubscribe(uuid)
        if args.cycle_radio:
            await start(s, 'SENSORS')
            assert (await vm_status(s))['state'] == 'RUNNING'
    if args.cycle_radio:
        await cycle_bluetooth()
        device = await find_board(args.address, args.name)
        async with BleakClient(device, winrt={'use_cached_services': False}) as client:
            s = Session(client, report)
            await s.subscribe(TX)
            value = await completed(s, 'STOPPED')
            report['disconnect_stop'] = value
            await s.command('PING')
        await cycle_bluetooth()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--cycle-radio', action='store_true')
    parser.add_argument('--seconds', type=float, default=60, help='Seconds per sensor/float soak')
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--name', default='NL802915')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.run: parser.error('Explicit --run required')
    report = dict(checks={}, soaks={}, operations=[], stats=[])
    report['binary_sha256'] = hashlib.sha256(Path('STM32CubeIDE/Debug/BleServoController.bin').read_bytes()).hexdigest()
    try:
        asyncio.run(check(args, report))
        report['result'] = 'passed'
    except BaseException as error:
        report.update(result='failed', error=repr(error))
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2))
if __name__ == '__main__': main()
