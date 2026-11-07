"""Real Bleak API load regression for the BLE transport repair (NOT browser QA).

Moves channels 0..9 through the known 1050..1450 us reproduction envelope.
Requires exclusive access to NL802915, installed bleak, and explicit --run.
No firmware flashing, reset, persistent settings, or browser state is touched.
Original 16-channel values and IMU mode are restored, with STOP in cleanup.
The load plan derives from a 50-event browser document configured for a 1300 ms
repeat. This harness always submits its first 20 events: one initial snapshot
and ten timed moves toward the 3000 ms targets. The later targets are omitted.
1300 ms is only a requested minimum before the hold operations. Serial ATT
writes, replies, reads, hold writes and periodic HELP can make cycles much longer.
Unlike the browser, this harness does not cancel unsent commands at the repeat
boundary. Actual complete/partial cycle timings are recorded even on failure.
"""
import argparse
import asyncio
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

SERVO = '6e400004-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
ADC = '91bad4b1-b950-4226-aa2b-4ede9fa42f59'
IMU = '91bad4a1-b950-4226-aa2b-4ede9fa42f59'
MODE = '91bad4a3-b950-4226-aa2b-4ede9fa42f59'
TOF = '91bad493-b950-4226-aa2b-4ede9fa42f59'
ROOT = Path(__file__).resolve().parents[1]
NAMES = {SERVO: 'servo', ADC: 'adc', IMU: 'imu', TOF: 'tof'}
REPEAT_SECONDS = 1.3
TIMEOUT = 8.0
EXPECTED_HELP = [
    'OK HELP', 'PING', 'DEMO1', 'STOP', 'GPIO_GET <pin>',
    'GPIO_SET <pin> <0|1>', 'ADC_READ <pin>', 'PWM_GET <ch>',
    'SET_DIRECT <ch> <us>', 'SET_TIMING', ' <ch> <us> <ms>',
    'TOF_MODE <0|1|3>', 'TOF_SET_MIN_MAX', ' <min> <max>',
    'PCA9685_SET_SERVO_US', '=SET_DIRECT', 'servo_StartMove', '=SET_TIMING',
    'OK HELP END',
]


def require(condition, detail):
    if not condition:
        raise AssertionError(detail)


def positive_int(value):
    try:
        parsed = int(value)
    except (TypeError, ValueError) as error:
        raise argparse.ArgumentTypeError('must be a positive integer') from error
    if parsed < 1:
        raise argparse.ArgumentTypeError('must be a positive integer')
    return parsed


def timeline_document():
    commands = []
    for channel in range(10):
        first = 1050 if channel in (0, 3) else 1450 if channel in (1, 2) else 1250
        last = 1450 if channel in (0, 3) else 1050 if channel in (1, 2) else 1250
        for target, value, duration in [(0, 1250, 0), (3000, first, 3000),
                                        (5000, 1250, 2000), (8000, last, 3000),
                                        (10000, 1250, 2000)]:
            commands.append(dict(channel=channel, value=value, targetAtMs=target,
                                 durationMs=duration, motion='smooth' if duration else 'jump'))
    return commands


def latency_summary(records):
    groups = {}
    for key in sorted({r['operation'] for r in records}):
        rows = [r for r in records if r['operation'] == key]
        values = sorted(r['ms'] for r in rows if 'error' not in r)
        groups[key] = dict(count=len(rows), errors=sum('error' in r for r in rows))
        if values:
            groups[key].update(median_ms=values[len(values) // 2],
                               p95_ms=values[min(len(values) - 1, int(len(values) * .95))],
                               max_ms=values[-1])
    return groups


def duration_summary(values):
    values = sorted(values)
    if not values:
        return dict(count=0)
    return dict(count=len(values), min_ms=values[0], mean_ms=sum(values) / len(values),
                median_ms=values[len(values) // 2],
                p95_ms=values[min(len(values) - 1, int(len(values) * .95))], max_ms=values[-1])


def validate_help(lines):
    # BLE_STATS may be added anywhere in HELP without changing the old commands.
    require([line for line in lines if not line.startswith('BLE_STATS')] == EXPECTED_HELP,
            ('Missing, reordered, or unexpected HELP line', lines))


def parse_stats(lines):
    require(lines and lines[0].startswith('OK BLE_STATS'), ('Missing BLE_STATS', lines))
    fields = {key: int(value, 0) for key, value in
              re.findall(r'([A-Za-z][A-Za-z0-9_]*)[=:](0x[0-9a-fA-F]+|\d+)', '\n'.join(lines))}
    for key, depth, high in re.findall(r'(rx|tx)=(0x[0-9a-fA-F]+|\d+)/(0x[0-9a-fA-F]+|\d+)', '\n'.join(lines)):
        fields[key] = int(depth, 0)
        fields[key + '_high'] = int(high, 0)
    return fields


def check_stats(before, after, allow_stale=False):
    required = {'s', 'rsv', 'rx', 'rx_high', 'tx', 'tx_high', 'rxrej', 'txrej',
                'sent', 'done', 'pool', 'timeout', 'err', 'stale', 'merge',
                'heap', 'min', 'stack'}
    require(required <= set(after), ('Incomplete BLE_STATS', after))
    require(after['rx'] == 0 and after['tx'] == 0 and after['rsv'] == 1,
            ('Queues or reservations did not drain', after))
    require(after['stack'] > 0 and after['heap'] > 0 and after['min'] > 0,
            ('Exhausted stack or heap', after))
    if 'hci' in after:
        require(after['hci'] > 0, ('Exhausted HCI event task stack', after))
    # lag is the optional maximum local notification completion latency in ms.
    # Capture it as diagnostic evidence; peer reception is checked separately.
    if before is not None:
        for key in ('rxrej', 'txrej', 'timeout', 'err') + (() if allow_stale else ('stale',)):
            require(after[key] == before[key], ('BLE transport error counter increased', key, before, after))
        for key in ('sent', 'done', 'pool', 'merge'):
            require(after[key] >= before[key], ('Counter reset without MCU reset', key, before, after))
        require(after['heap'] == before['heap'], ('FreeRTOS heap changed over regression', before, after))


def artifact_metadata(hex_path, elf_path):
    metadata = dict(repository=str(ROOT), captured_utc=datetime.now(timezone.utc).isoformat(),
                    identity_scope='local artifacts at run start; flash/readback proof is recorded separately')
    metadata['head'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    status = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
    metadata['worktree_dirty'] = bool(status.strip())
    metadata['worktree_status'] = status.splitlines()
    metadata['artifacts'] = {}
    for kind, path in [('hex', hex_path), ('elf', elf_path)]:
        path = path.resolve()
        content = path.read_bytes()
        details = path.stat()
        metadata['artifacts'][kind] = dict(path=str(path), sha256=hashlib.sha256(content).hexdigest(),
                                           bytes=len(content), modified_ns=details.st_mtime_ns)
    metadata['test_script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    return metadata


class Session:
    def __init__(self, client, report):
        self.client = client
        self.report = report
        self.replies = asyncio.Queue()
        self.subscriptions = set()
        self.counts = Counter()
        self.invalid_frames = []
        self.tx_packets = 0
        self.checked_commands = 0
        self.accepted_commands = 0

    async def operation(self, name, awaitable):
        started = time.monotonic()
        record = {'operation': name, 'at': started}
        try:
            return await asyncio.wait_for(awaitable, TIMEOUT)
        except BaseException as error:
            record['error'] = repr(error)
            raise
        finally:
            record['ms'] = (time.monotonic() - started) * 1000
            self.report['operations'].append(record)

    async def subscribe(self, uuid):
        def callback(_, payload):
            value = bytes(payload)
            if uuid == TX:
                self.tx_packets += 1
                try:
                    self.replies.put_nowait(value.decode('utf-8'))
                except UnicodeDecodeError as error:
                    self.replies.put_nowait('<invalid UTF-8: %s>' % error)
                return
            self.counts[NAMES[uuid]] += 1
            expected = {SERVO: 32, ADC: 10, TOF: 64}.get(uuid)
            if expected is not None and len(value) != expected:
                self.invalid_frames.append((NAMES[uuid], value.hex()))
        await self.operation('subscribe_' + NAMES.get(uuid, 'nus'),
                             self.client.start_notify(uuid, callback))
        self.subscriptions.add(uuid)

    async def unsubscribe(self, uuid):
        if uuid in self.subscriptions:
            await self.operation('unsubscribe_' + NAMES.get(uuid, 'nus'), self.client.stop_notify(uuid))
            self.subscriptions.remove(uuid)

    async def read(self, uuid):
        return bytes(await self.operation('read_' + NAMES.get(uuid, 'mode'), self.client.read_gatt_char(uuid)))

    async def values(self):
        payload = await self.read(SERVO)
        require(len(payload) == 32, ('Servo frame length', len(payload)))
        return list(struct.unpack('<16H', payload))

    async def write(self, uuid, payload):
        await self.operation('write_' + NAMES.get(uuid, 'mode'),
                             self.client.write_gatt_char(uuid, payload, response=True))

    async def snapshot(self, values):
        await self.write(SERVO, struct.pack('<16H', *values))

    async def send(self, text):
        await self.operation('write_nus', self.client.write_gatt_char(RX, text.encode(), response=True))
        self.accepted_commands += 1

    async def reply(self, expected=None):
        line = await self.operation('reply_nus', self.replies.get())
        if expected is not None:
            require(line == expected, ('Lost, stale, or reordered reply', expected, line))
        return line

    async def command(self, text):
        require(self.replies.empty(), ('Unconsumed reply before command', text))
        await self.send(text)
        result = await self.reply('OK ' + text)
        self.checked_commands += 1
        return result

    async def help_ping(self):
        require(self.replies.empty(), 'Unconsumed reply before HELP/PING')
        # Two accepted requests coexist while the long HELP response is draining.
        await self.send('HELP')
        await self.send('PING')
        lines = []
        for _ in range(40):
            line = await self.reply()
            lines.append(line)
            if line == 'OK HELP END':
                break
        validate_help(lines)
        await self.reply('OK PING')
        self.checked_commands += 2
        require(self.replies.empty(), 'Extra reply after HELP/PING')
        return lines

    async def stats(self):
        await asyncio.sleep(.25)  # Let the preceding response's completion event drain.
        require(self.replies.empty(), 'Unconsumed reply before BLE_STATS')
        await self.send('BLE_STATS')
        lines = [await self.reply()]
        # Multiline diagnostics use an explicit END sentinel; a compact response
        # is collected as one packet. Update here if the firmware contract changes.
        if lines[0] == 'OK BLE_STATS':
            for _ in range(40):
                lines.append(await self.reply())
                if lines[-1] == 'OK BLE_STATS END':
                    break
            require(lines[-1] == 'OK BLE_STATS END', 'BLE_STATS did not terminate')
        fields = parse_stats(lines)
        self.checked_commands += 1
        item = dict(at=time.monotonic(), lines=lines, fields=fields)
        self.report['stats'].append(item)
        return item

    async def stable_read_test(self, stable):
        await self.snapshot(stable)
        await asyncio.sleep(.5)
        before = self.counts['servo']
        started = time.monotonic()
        for _ in range(20):
            require(await self.values() == stable, 'Stable servo READ changed values')
        await asyncio.sleep(.3)
        after = self.counts['servo']
        require(after == before, ('Stable READ generated telemetry notifications', before, after))
        self.report['phases'].append(dict(name='stable_reads_do_not_notify', reads=20,
                                          seconds=time.monotonic() - started,
                                          extra_notifications=after - before))
        print('PASS 20 stable servo READs produced no notifications', flush=True)

    async def sensor_load(self):
        before = dict(self.counts)
        for uuid in (SERVO, ADC, IMU, TOF):
            await self.subscribe(uuid)
        await self.write(MODE, b'\x01')
        deadline = time.monotonic() + 35
        while not all(self.counts[name] > before.get(name, 0) for name in ('adc', 'imu', 'tof')):
            require(time.monotonic() < deadline, ('Missing sensor telemetry', dict(self.counts)))
            await asyncio.sleep(.1)
        while await self.read(MODE) != b'\x01':
            require(time.monotonic() < deadline, 'IMU mode did not become active')
            await asyncio.sleep(.1)

    async def repeat_load(self, seconds, help_every_cycles=10):
        started = time.monotonic()
        cycles = 0
        next_update = 30
        notifications_before = dict(self.counts)
        phase = dict(name='repeat_api_load', result='running', requested_repeat_min_ms=1300,
                     help_every_cycles=help_every_cycles,
                     source_document_events=50, events_per_completed_cycle=20,
                     scheduling='Serial ATT and replies; minimum wait before hold; no browser queue cancellation',
                     cycle_timings=[], completed_cycles=0)
        self.report['phases'].append(phase)
        try:
            while time.monotonic() - started < seconds:
                cycle_start = time.monotonic()
                timing = dict(cycle=cycles + 1, completed=False, timing_commands_written=0,
                              one_ms_late_commands=0)
                try:
                    snapshot = await self.values()
                    snapshot[:10] = [1250] * 10
                    await self.snapshot(snapshot)
                    timing['initial_snapshot_ms'] = (time.monotonic() - cycle_start) * 1000
                    command_start = time.monotonic()
                    expected = []
                    # This is intentionally a complete ten-command API batch.
                    # Web instead cancels pending queued commands at its boundary.
                    for channel in range(10):
                        target = 1050 if channel in (0, 3) else 1450 if channel in (1, 2) else 1250
                        duration = max(1, round(3000 - 1000 * (time.monotonic() - cycle_start)))
                        text = f'SET_TIMING {channel} {target} {duration}'
                        await self.send(text)
                        timing['timing_commands_written'] += 1
                        timing['one_ms_late_commands'] += int(duration == 1)
                        expected.append('OK ' + text)
                    timing['commands_write_ms'] = (time.monotonic() - command_start) * 1000
                    reply_start = time.monotonic()
                    for response in expected:
                        await self.reply(response)
                        self.checked_commands += 1
                    timing['replies_wait_ms'] = (time.monotonic() - reply_start) * 1000
                    wait_start = time.monotonic()
                    await asyncio.sleep(max(0, REPEAT_SECONDS - (wait_start - cycle_start)))
                    timing['minimum_period_wait_ms'] = (time.monotonic() - wait_start) * 1000
                    hold_start = time.monotonic()
                    # Preserve the original Web's two hold READs and atomic WRITE.
                    hold = await self.values()
                    latest = await self.values()
                    latest[:10] = hold[:10]
                    await self.snapshot(latest)
                    timing['hold_ms'] = (time.monotonic() - hold_start) * 1000
                    help_start = time.monotonic()
                    if (cycles + 1) % help_every_cycles == 0:
                        await self.help_ping()
                    timing['help_ping_ms'] = (time.monotonic() - help_start) * 1000
                    timing['completed'] = True
                    cycles += 1
                finally:
                    timing['elapsed_ms'] = (time.monotonic() - cycle_start) * 1000
                    phase['cycle_timings'].append(timing)
                    phase['completed_cycles'] = cycles
                    phase['soak_seconds'] = time.monotonic() - started
                elapsed = time.monotonic() - started
                if elapsed >= next_update:
                    print(f'RUN {elapsed:.1f}s; {cycles} cycles; actual mean {elapsed / cycles:.3f}s; '
                          f'notifications {dict(self.counts)}', flush=True)
                    next_update += 30
            elapsed = time.monotonic() - started
            phase['soak_seconds'] = elapsed
            require(elapsed >= 300, ('Soak shorter than required', elapsed))
            require(self.counts['servo'] > notifications_before.get('servo', 0), 'No servo telemetry during motion')
            require(not self.invalid_frames, self.invalid_frames)
            await self.command('STOP')
            await self.help_ping()
            phase['result'] = 'passed'
            print(f'PASS API load {elapsed:.1f}s, {cycles} cycles, actual mean {elapsed / cycles:.3f}s; '
                  'no missing command replies', flush=True)
        except BaseException:
            phase['result'] = 'failed'
            raise
        finally:
            phase['notifications'] = dict(self.counts)
            phase['actual_cycle_ms'] = duration_summary([
                row['elapsed_ms'] for row in phase['cycle_timings'] if row['completed']])
            phase['partial_cycle_ms'] = [row['elapsed_ms'] for row in phase['cycle_timings'] if not row['completed']]

    async def subscription_restart(self, stable):
        await self.command('STOP')
        await self.snapshot(stable)
        for uuid in (SERVO, ADC, IMU, TOF):
            await self.unsubscribe(uuid)
        await asyncio.sleep(.7)
        stopped = dict(self.counts)
        await asyncio.sleep(.8)
        require(dict(self.counts) == stopped, ('Telemetry after CCCD stop', stopped, dict(self.counts)))
        await self.help_ping()
        await self.sensor_load()
        await self.stable_read_test(stable)
        self.report['phases'].append(dict(name='subscription_stop_restart', result='passed'))
        print('PASS telemetry CCCD stop/start and command coexistence', flush=True)

    def finish_report(self):
        self.report['connections'].append(dict(notifications=dict(self.counts),
                                               invalid_frames=self.invalid_frames,
                                               tx_packets=self.tx_packets,
                                               accepted_commands=self.accepted_commands,
                                               checked_commands=self.checked_commands))


async def find_board(address, name):
    from bleak import BleakScanner
    observed = {}
    def match(device, advertisement):
        if device.address.upper() != address.upper():
            return False
        observed['name'] = advertisement.local_name
        return advertisement.local_name == name
    device = await BleakScanner.find_device_by_filter(match, timeout=25)
    require(device is not None, ('No fresh matching advertisement', address, name, observed))
    return device


async def run(args, report):
    from bleak import BleakClient
    original = None
    original_mode = None
    session = None
    client = None
    cleanup_errors = []
    ever_connected = False
    connection_attempted = False
    state_change_attempted = False
    servo_restored = False
    mode_restored = False
    stop_acknowledged = False
    try:
        device = await find_board(args.address, args.name)
        report['verified_advertisement'] = dict(address=device.address, name=args.name)
        client = BleakClient(device, winrt={'use_cached_services': False})
        connection_attempted = True
        await client.connect()
        ever_connected = True
        session = Session(client, report)
        original = await session.values()
        original_mode = await session.read(MODE)
        require(len(original_mode) == 1 and original_mode[0] in (0, 1, 2), ('Invalid initial IMU mode', original_mode))
        report['original_values'] = original
        report['original_imu_mode'] = original_mode[0]
        await session.subscribe(TX)
        state_change_attempted = True
        await session.command('STOP')
        await session.help_ping()
        initial_stats = (await session.stats())['fields']
        check_stats(None, initial_stats)
        await session.sensor_load()
        stable = original.copy()
        stable[:10] = [1250] * 10
        await session.stable_read_test(stable)
        warm_stats = (await session.stats())['fields']
        check_stats(None, warm_stats)
        await session.repeat_load(args.seconds, args.help_every_cycles)
        soak_stats = (await session.stats())['fields']
        check_stats(warm_stats, soak_stats)
        await session.subscription_restart(stable)
        restart_stats = (await session.stats())['fields']
        check_stats(soak_stats, restart_stats)
        # Interrupt real motion and a potentially in-flight long response. The
        # firmware must stop motion and discard old-connection reply work.
        await session.command('SET_TIMING 0 1300 3000')
        await session.send('HELP')
        await client.disconnect()
        session.finish_report()
        session = None
        client = None
        report['phases'].append(dict(name='intentional_disconnect', abandoned_request='HELP'))
        device = await find_board(args.address, args.name)
        report['phases'].append(dict(name='fresh_advertisement_without_reset', result='passed'))
        client = BleakClient(device, winrt={'use_cached_services': False})
        await client.connect()
        session = Session(client, report)
        await session.subscribe(TX)
        await asyncio.sleep(.8)
        require(session.replies.empty(), 'Old connection NUS response leaked after reconnect')
        require(await session.read(MODE) == b'\x00', 'IMU active mode leaked after reconnect')
        first = await session.values()
        await asyncio.sleep(.4)
        require(await session.values() == first, 'Timed motion continued after physical disconnect')
        await session.command('PING')
        await session.help_ping()
        reconnect_stats = (await session.stats())['fields']
        check_stats(restart_stats, reconnect_stats, allow_stale=True)
        report['phases'].append(dict(name='reconnect_no_stale_reply_or_motion', result='passed'))
        print('PASS fresh advertising and reconnect without reset; no stale NUS reply or motion', flush=True)
    finally:
        # Cleanup attempts are independent: a failed STOP must not prevent a
        # direct snapshot restore, and a failed restore must not prevent disconnect.
        primary_error = sys.exc_info()[1]
        if state_change_attempted and original is not None and (session is None or client is None or not client.is_connected):
            if session is not None:
                session.finish_report()
                session = None
            try:
                device = await find_board(args.address, args.name)
                client = BleakClient(device, winrt={'use_cached_services': False})
                await client.connect()
                session = Session(client, report)
                await session.subscribe(TX)
                # Read transport counters before STOP/restore changes the recovery
                # state. Diagnostic failure never masks the original test error or
                # prevents the independent motor and sensor cleanup steps.
                try:
                    recovery = await session.stats()
                    recovery['context'] = 'failure_recovery_before_cleanup'
                    report['recovery_stats'] = recovery
                except Exception as error:
                    report['recovery_stats_error'] = repr(error)
            except Exception as error:
                cleanup_errors.append('Recovery connect: ' + repr(error))
        if state_change_attempted and session is not None and client is not None and client.is_connected:
            async def cleanup_step(label, operation):
                try:
                    await operation()
                    return True
                except Exception as error:
                    cleanup_errors.append(label + ': ' + repr(error))
                    return False
            # Do not hide stale replies from test assertions. Cleanup alone may
            # drain an abandoned HELP so STOP can be issued independently.
            async def stop():
                await session.send('STOP')
                deadline = time.monotonic() + TIMEOUT
                while time.monotonic() < deadline:
                    if await session.reply() == 'OK STOP':
                        return
                raise AssertionError('Cleanup STOP acknowledgement missing')
            stop_acknowledged = await cleanup_step('STOP', stop)
            if original is not None:
                await cleanup_step('Restore servo snapshot', lambda: session.snapshot(original))
                async def verify():
                    require(await session.values() == original, 'Original snapshot was not restored')
                servo_restored = await cleanup_step('Verify restored snapshot', verify)
            if original_mode is not None:
                await cleanup_step('Restore IMU mode', lambda: session.write(MODE, original_mode))
                async def verify_mode():
                    deadline = time.monotonic() + 4
                    while await session.read(MODE) != original_mode:
                        require(time.monotonic() < deadline, 'Original IMU mode was not restored')
                        await asyncio.sleep(.1)
                mode_restored = await cleanup_step('Verify restored IMU mode', verify_mode)
            for uuid in list(session.subscriptions):
                await cleanup_step('Stop subscription ' + uuid, lambda uuid=uuid: session.unsubscribe(uuid))
            session.finish_report()
        if client is not None:
            try:
                await client.disconnect()
            except Exception as error:
                cleanup_errors.append('Disconnect: ' + repr(error))
        report['cleanup_errors'] = cleanup_errors
        if not ever_connected:
            cleanup_status = ('no_confirmed_connection_dispose_attempted' if connection_attempted
                              else 'not_needed_no_hardware_connection')
            restored = None
        elif not state_change_attempted:
            cleanup_status = 'not_needed_no_test_state_change'
            restored = None
        else:
            restored = bool(stop_acknowledged and servo_restored and mode_restored and not cleanup_errors)
            cleanup_status = 'restored_and_disconnected' if restored else 'restoration_incomplete'
            if not restored and not cleanup_errors:
                cleanup_errors.append('Test state changed but full restoration was not verified')
        report['cleanup'] = dict(status=cleanup_status, hardware_connected=ever_connected,
                                 connection_attempted=connection_attempted,
                                 state_change_attempted=state_change_attempted,
                                 stop_acknowledged=stop_acknowledged, servo_restored=servo_restored,
                                 imu_mode_restored=mode_restored)
        report['restoration_verified'] = restored
        if primary_error is None:
            require(not cleanup_errors, ('Hardware cleanup incomplete', cleanup_errors))


def self_test():
    require(positive_int('1') == 1 and positive_int('10') == 10, 'Valid HELP cadence')
    for invalid in ('0', '-1', '1.5', 'invalid'):
        try:
            positive_int(invalid)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError('Invalid HELP cadence was accepted: ' + invalid)
    plan = timeline_document()
    require(len(plan) == 50, 'Wrong document length')
    due = [event for event in plan if event['targetAtMs'] - event['durationMs'] <= 1300]
    require(len(due) == 20, 'Wrong repeat-window event count')
    require([event['value'] for event in due if event['durationMs']] ==
            [1050, 1450, 1450, 1050, 1250, 1250, 1250, 1250, 1250, 1250], 'Wrong targets')
    validate_help(EXPECTED_HELP)
    try:
        validate_help(EXPECTED_HELP[:-2] + EXPECTED_HELP[-1:])
    except AssertionError:
        pass
    else:
        raise AssertionError('HELP loss was not detected')
    require(parse_stats(['OK BLE_STATS rx_drop=0 busy:0x64']) == {'rx_drop': 0, 'busy': 100}, 'Stats parse')
    require(parse_stats(['OK BLE_STATS', 'rx=0x0/0x5', 'tx=0/5']) ==
            {'rx': 0, 'rx_high': 5, 'tx': 0, 'tx_high': 5}, 'Hex/depth diagnostics parse')
    require(latency_summary([dict(operation='read', ms=3)])['read']['max_ms'] == 3, 'Latency summary')
    require(duration_summary([1300, 4800])['mean_ms'] == 3050, 'Actual cycle timing summary')
    sample = dict(s=0, rsv=1, rx=0, rx_high=1, tx=0, tx_high=1,
                  rxrej=0, txrej=0, sent=10, done=10, pool=0, timeout=0,
                  err=0, stale=0, merge=0, heap=4096, min=2048, stack=512)
    check_stats(sample, sample.copy())
    for field in ('rxrej', 'txrej', 'timeout', 'err', 'stale'):
        changed = dict(sample, **{field: 1})
        try:
            check_stats(sample, changed)
        except AssertionError:
            pass
        else:
            raise AssertionError('Stats error increase was not detected: ' + field)
    check_stats(sample, dict(sample, stale=1), allow_stale=True)
    print('PASS offline plan, HELP loss detection, stats invariants and latency summary; no hardware touched')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', default='00:80:E1:2A:26:24')
    parser.add_argument('--name', default='NL802915')
    parser.add_argument('--seconds', type=float, default=310)
    parser.add_argument('--help-every-cycles', type=positive_int, default=10,
                        help='Send HELP plus PING every N motor cycles (positive integer; default: 10)')
    parser.add_argument('--firmware-hex', type=Path, default=ROOT / 'STM32CubeIDE/Debug/BleServoController.hex')
    parser.add_argument('--firmware-elf', type=Path, default=ROOT / 'STM32CubeIDE/Debug/BleServoController.elf')
    parser.add_argument('--output', type=Path, help='New JSON evidence path (refuses to overwrite)')
    parser.add_argument('--run', action='store_true', help='Explicitly permit hardware connection and motion')
    parser.add_argument('--self-test', action='store_true', help='Offline checks only; no Bleak import')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.run:
        parser.error('Hardware execution requires --run; use --self-test for offline checks.')
    if args.seconds < 300:
        parser.error('--seconds must be at least 300 for this regression.')
    if args.output and args.output.exists():
        parser.error('Output already exists; select a new evidence path.')
    report = dict(test='direct_bleak_api_load_not_browser_reproduction',
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  duration_requested=args.seconds, help_every_cycles=args.help_every_cycles,
                  timeline=timeline_document(),
                  browser_validated=False, physical_motion_observed=False,
                  operations=[], stats=[], phases=[], connections=[],
                  cleanup=dict(status='not_started', hardware_connected=False),
                  restoration_verified=None)
    error = None
    try:
        report['provenance'] = artifact_metadata(args.firmware_hex, args.firmware_elf)
        asyncio.run(run(args, report))
        report['result'] = 'passed'
    except BaseException as exc:
        error = exc
        report['result'] = 'failed'
        report['error'] = repr(exc)
    report['latency'] = latency_summary(report['operations'])
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    if args.output:
        with args.output.open('x', encoding='utf-8') as output:
            json.dump(report, output, indent=2)
    summary = {key: value for key, value in report.items() if key not in ('operations', 'timeline')}
    print(json.dumps(summary, indent=2), flush=True)
    if error is not None:
        raise error


if __name__ == '__main__':
    main()
