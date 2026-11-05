"""Exercise actual DEMO1 PWM outputs, STOP, and disconnect/reconnect cancellation.

Run normally, then --verify-reconnect in a fresh process so WinRT releases GATT.
This test deliberately moves all 16 outputs between 500 and 2000 microseconds.
"""
import argparse
import asyncio
import gc
import struct
import time
from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
VALUES = '6e400004-b5a3-f393-e0a9-e50e24dcca9e'


async def main(args):
    if args.winrt_shared:
        from imu_winrt_shared import SharedImuClient
        client = SharedImuClient(args.address, '6e400001-b5a3-f393-e0a9-e50e24dcca9e')
    else:
        device = await BleakScanner.find_device_by_address(args.address, timeout=15)
        assert device, 'Board is not advertising'
        client = BleakClient(device, winrt={'use_cached_services': False})
    leave_demo_for_disconnect = False
    async with client:
        replies = asyncio.Queue()
        samples = []
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))

        async def command(text):
            await client.write_gatt_char(RX, text.encode(), response=True)
            return await asyncio.wait_for(replies.get(), 5)

        async def values():
            return struct.unpack('<16H', await client.read_gatt_char(VALUES))

        def on_values(_, data):
            snapshot = struct.unpack('<16H', data)
            # Characteristic reads can also publish an unchanged snapshot.
            if not samples or snapshot != samples[-1][1]:
                samples.append((time.monotonic(), snapshot))

        try:
            if args.verify_reconnect:
                before = await values()
                await asyncio.sleep(2.5)
                assert await values() == before, 'DEMO1 resumed after reconnection'
                assert await command(f'SET_DIRECT 0 {before[0]}') == f'OK SET_DIRECT 0 {before[0]}'
                print('PASS reconnect: demo stays stopped; manual control accepted', flush=True)
                return

            assert await command('STOP') == 'OK STOP'
            await client.write_gatt_char(RX, b'HELP', response=True)
            lines = []
            while not lines or lines[-1] != 'OK HELP END':
                lines.append(await asyncio.wait_for(replies.get(), 5))
            assert 'DEMO1' in lines and 'STOP' in lines, lines
            assert await command('DEMO1 extra') == '- BAD ARGUMENTS'
            await client.start_notify(VALUES, on_values)
            samples.clear()
            assert await command('demo1') == 'OK DEMO1'
            await asyncio.sleep(.12)
            assert await values() == (500,) * 16
            assert await command('STOP extra') == '- BAD ARGUMENTS'
            assert await command('SET_DIRECT 0 1500') == '- DEMO1 ACTIVE: STOP'
            assert await command('SET_TIMING 0 1500 100') == '- DEMO1 ACTIVE: STOP'
            try:
                await client.write_gatt_char(VALUES, struct.pack('<16H', *([1500] * 16)), response=True)
            except Exception:
                pass  # ATT rejection is expected; state below must remain intact.
            assert await values() == (500,) * 16
            # A duplicate command must not reset the phase or create another timer.
            assert await command('DEMO1') == 'OK DEMO1'
            deadline = time.monotonic() + 6
            while len(samples) < 3:
                assert time.monotonic() < deadline, samples
                await asyncio.sleep(.05)
            phases = []
            for stamp, snapshot in samples:
                assert len(set(snapshot)) == 1 and snapshot[0] in (500, 2000), snapshot
                if not phases or snapshot != phases[-1][1]:
                    phases.append((stamp, snapshot))
            assert [entry[1][0] for entry in phases[:3]] == [500, 2000, 500], phases
            gaps = [phases[i][0] - phases[i-1][0] for i in (1, 2)]
            assert all(1.75 <= gap <= 2.25 for gap in gaps), gaps
            print('PASS 2-second intervals:', gaps, flush=True)
            assert await command('stop') == 'OK STOP'
            held = await values()
            await asyncio.sleep(2.5)
            assert await values() == held, 'STOP failed to hold pulses'
            assert await command('STOP') == 'OK STOP'
            # Freeze each endpoint before 16 register reads: Windows round trips
            # can take longer than a two-second phase and straddle the next edge.
            for pulse, ticks in [(500, 102), (2000, 410)]:
                assert await command('DEMO1') == 'OK DEMO1'
                deadline = time.monotonic() + 4
                while await values() != (pulse,) * 16:
                    assert time.monotonic() < deadline, ('Missing endpoint', pulse)
                    await asyncio.sleep(.05)
                assert await command('STOP') == 'OK STOP'
                assert await values() == (pulse,) * 16
                await asyncio.sleep(.1)
                for channel in range(16):
                    reply = await command(f'PWM_GET {channel}')
                    assert reply == f'OK PWM_GET {channel} 0 {ticks}', reply
                print(f'PASS all 16 physical PWM registers = {pulse} us', flush=True)
            assert await command('SET_TIMING 0 1500 3000') == 'OK SET_TIMING 0 1500 3000'
            await asyncio.sleep(.2)
            assert await command('STOP') == 'OK STOP'
            held = await values()
            await asyncio.sleep(.3)
            assert await values() == held, 'STOP failed to cancel a timed move'
            print('PASS STOP, repeated STOP, invalid arguments, busy protection, timed cancellation', flush=True)
            assert await command('DEMO1') == 'OK DEMO1'
            await asyncio.sleep(.15)
            leave_demo_for_disconnect = True
            print('Disconnecting with DEMO1 active; verify gate OFF and reconnect next', flush=True)
        finally:
            if not leave_demo_for_disconnect:
                await command('STOP')
            await client.stop_notify(TX)
            if not args.verify_reconnect:
                await client.stop_notify(VALUES)
            if not args.winrt_shared:
                client._backend._session.maintain_connection = False
    del client
    gc.collect()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--verify-reconnect', action='store_true')
    parser.add_argument('--winrt-shared', action='store_true')
    asyncio.run(main(parser.parse_args()))
