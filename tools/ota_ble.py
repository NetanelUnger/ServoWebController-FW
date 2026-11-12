"""NatiLab BLE OTA v2: scan, version, update, and reconnect verification.

Requires Python 3.10+ and bleak (on this workstation: py -3.13).
No BLE dependency is needed for --check-file. See OTA.md.
"""
import argparse
import asyncio
import json
import re
import secrets
import struct
import time
from contextlib import asynccontextmanager
from pathlib import Path
from ota_package import TARGET, unpack_package

NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
CONTROL = '0000fe22-8e22-4541-9d4c-21edae82ed19'
STATUS = '0000fe23-8e22-4541-9d4c-21edae82ed19'
DATA = '0000fe24-8e22-4541-9d4c-21edae82ed19'
OTA_SERVICE = '0000fe20-cc7a-482a-984a-7f2ed5b3e58f'


@asynccontextmanager
async def connection(device, timeout=25):
    from bleak import BleakClient
    client = BleakClient(device, services=[NUS, OTA_SERVICE], timeout=timeout,
                         winrt={'use_cached_services': False})
    try:
        await asyncio.wait_for(client.connect(), timeout + 5)
        if not all(client.services.get_characteristic(c) for c in (RX, TX, CONTROL, DATA, STATUS)):
            raise RuntimeError('NUS/OTA services are unavailable. Close other BLE clients and reconnect; '
                               'older firmware needs the initial ST-LINK installation.')
        yield client
    finally:
        try:
            # WinRT can otherwise wait two minutes when another application
            # retains the same GATT session. Do not hide the original failure.
            await asyncio.wait_for(client.disconnect(), 8)
        except Exception as exc:
            print(f'Bluetooth cleanup did not finish: {type(exc).__name__}. Close other BLE clients.', flush=True)


async def version(client):
    text = ''
    lines = []
    ready = asyncio.Event()

    def notify(_, data):
        nonlocal text
        line = bytes(data).decode('ascii', errors='replace').strip()
        if re.fullmatch(r'OK VERSION \S+ IMAGE=[0-9A-F]{8} SIZE=\d+ OTA=2', line):
            text = line
            ready.set()
        elif line == 'OK VERSION':
            lines.clear()
        elif line == 'OK VERSION END':
            fields = dict(s.split('=', 1) for s in lines if '=' in s)
            text = f'OK VERSION {fields.get("VER", "")}{fields.get("VER+", "")} IMAGE={fields.get("IMAGE")} SIZE={fields.get("SIZE")} OTA={fields.get("OTA")}'
            ready.set()
        else:
            lines.append(line)

    await client.start_notify(TX, notify)
    try:
        await client.write_gatt_char(RX, b'VERSION\n', response=True)
        await asyncio.wait_for(ready.wait(), 8)
        line = text.strip()
        if not re.fullmatch(r'OK VERSION [A-Za-z0-9.+_-]+ IMAGE=[0-9A-F]{8} SIZE=\d+ OTA=2', line):
            raise RuntimeError(f'Firmware does not implement VERSION/OTA v2: {line}')
        return line
    finally:
        await client.stop_notify(TX)


def decode_status(data):
    if len(data) != 20 or data[0] != 2:
        raise RuntimeError('OTA v2 missing. First flash the application AND bootloader using tools/flash.ps1.')
    proto, state, op, error, session, received, length, crc = struct.unpack('<BBBBIIII', data)
    return dict(state=state, op=op, error=error, session=session, received=received, length=length, crc=crc)


async def wait_status(client, session, op, expected, received=None, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        s = decode_status(await client.read_gatt_char(STATUS, use_cached=False))
        if s['session'] != session:
            raise RuntimeError('OTA session changed')
        if s['error']:
            raise RuntimeError(f'Device OTA error 0x{s["error"]:02x}, offset {s["received"]}')
        if s['state'] != 1:
            if s['op'] != op or s['state'] != expected or (received is not None and s['received'] != received):
                raise RuntimeError(f'Unexpected durable OTA status: {s}')
            return s
        await asyncio.sleep(.01)
    raise TimeoutError('Timed out waiting for durable Flash completion')


def command(op, session):
    return struct.pack('<BBBBI', op, 2, 0, 0, session)


async def upload(client, info, image, args):
    decode_status(await client.read_gatt_char(STATUS, use_cached=False))
    session = secrets.randbelow(0xffffffff) + 1
    committed = False
    begun = False
    try:
        begin = struct.pack('<BBBBIIII', 0x10, 2, 1, 0, len(image), int(info['crc32'], 16), session, TARGET)
        await client.write_gatt_char(CONTROL, begin, response=True)
        begun = True
        print('Erasing download slot (stored files are being removed)...', flush=True)
        await wait_status(client, session, 0x10, 2, 0)
        chunk = min(args.chunk, 224, max(16, ((client.mtu_size - 7) // 16) * 16))
        started = time.monotonic()
        last_percent = -1
        for offset in range(0, len(image), chunk):
            payload = image[offset:offset + chunk]
            await client.write_gatt_char(DATA, struct.pack('<I', offset) + payload, response=True)
            end = offset + len(payload)
            await wait_status(client, session, 0x14, 3, end, 10)
            percent = end * 100 // len(image)
            if percent != last_percent:
                print(f'{percent:3d}%  {end}/{len(image)} bytes  {time.monotonic()-started:.1f}s', flush=True)
                last_percent = percent
            if args.stop_after and end >= args.stop_after:
                await client.write_gatt_char(CONTROL, command(0x13, session), response=True)
                await wait_status(client, session, 0x13, 7)
                print('Transfer intentionally aborted; active firmware was not changed.')
                return False
        await client.write_gatt_char(CONTROL, command(0x11, session), response=True)
        await wait_status(client, session, 0x11, 4, len(image))
        print('Device verified the full image CRC and vector/tail layout.', flush=True)
        # After this write, an interrupted acknowledgement is ambiguous. Reconnect
        # and verify the running image; never automatically repeat installation.
        committed = True
        try:
            await client.write_gatt_char(CONTROL, command(0x12, session), response=True)
            await wait_status(client, session, 0x12, 5, len(image))
        except Exception as exc:
            print(f'Install acknowledgement interrupted ({exc}); checking after reboot.', flush=True)
        return True
    finally:
        if begun and not committed and client.is_connected:
            try:
                await client.write_gatt_char(CONTROL, command(0x13, session), response=True)
            except Exception:
                pass  # disconnect also invalidates the RAM upload session


async def run(args, package):
    from bleak import BleakScanner
    devices = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    matches = []
    for device, adv in devices.values():
        name = adv.local_name or device.name or ''
        if args.scan:
            if NUS in adv.service_uuids or name.startswith('NL'):
                print(f'{device.address}  {name}  RSSI={adv.rssi}')
        elif ((args.address and device.address.lower() == args.address.lower()) or
              (args.name and name == args.name) or
              (not args.address and not args.name and NUS in adv.service_uuids)):
            matches.append(device)
    if args.scan:
        return
    if len(matches) != 1:
        raise RuntimeError(f'Found {len(matches)} matching devices. Use --scan then --address or --name.')
    device = matches[0]
    print(f'Connecting: {device.name} ({device.address})', flush=True)
    async with connection(device) as client:
        before = await version(client)
        print(before, flush=True)
        if args.probe:
            baseline = bytes(await client.read_gatt_char(STATUS, use_cached=False))
            decode_status(baseline)
            if baseline[1] != 0:
                raise RuntimeError('Read-only rejection probe requires idle OTA status')
            invalid_begin = struct.pack('<BBBBIIII', 0x10, 2, 0, 0, 512, 0, 42, TARGET)
            wrong_target = struct.pack('<BBBBIIII', 0x10, 2, 1, 0, 512, 0, 42, 0)
            for channel, request in [(CONTROL, b'\x02'), (CONTROL, invalid_begin),
                                     (CONTROL, wrong_target), (DATA, bytes(20)),
                                     (CONTROL, command(0x12, 42))]:
                try:
                    await client.write_gatt_char(channel, request, response=True)
                except Exception as exc:
                    print(f'Correctly rejected {request.hex()}: {exc}', flush=True)
                else:
                    raise RuntimeError('Unsafe request unexpectedly accepted')
                if bytes(await client.read_gatt_char(STATUS, use_cached=False)) != baseline:
                    raise RuntimeError('Rejected request changed OTA status')
            if await version(client) != before:
                raise RuntimeError('Running image changed during rejection probe')
            print('PASS: malformed/legacy/unacknowledged/wrong-target uploads and premature data/install rejected; OTA state and running image unchanged.')
            return
        if args.version:
            return
        info, image = package
        # Release any stored playback before the receiver claims its shared slot.
        await client.write_gatt_char(RX, b'STOP\n', response=True)
        if not await upload(client, info, image, args):
            after = await version(client)
            if after != before:
                raise RuntimeError(f'Unexpected firmware change after abort: {after}')
            print('PASS: VERSION unchanged after aborted transfer.')
            return
    deadline = time.monotonic() + 90
    last_error = None
    await asyncio.sleep(3)
    while time.monotonic() < deadline:
        try:
            async with connection(device.address, timeout=15) as client:
                line = await version(client)
                print(line, flush=True)
                expected = f'OK VERSION {info["version"]} IMAGE={info["crc32"]} SIZE={info["length"]} OTA=2'
                if line != expected:
                    raise RuntimeError(f'Running firmware differs from uploaded package: {line}')
                print('PASS: OTA installed; running VERSION, size and CRC match the package.')
                return
        except Exception as exc:
            last_error = exc
            await asyncio.sleep(2)
    raise RuntimeError(f'Installation not verified after reconnect: {last_error}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--scan', action='store_true')
    mode.add_argument('--version', action='store_true')
    mode.add_argument('--probe', action='store_true', help='Test rejection of invalid requests; never starts an upload or erases Flash')
    mode.add_argument('--file', type=Path)
    mode.add_argument('--check-file', type=Path)
    target = p.add_mutually_exclusive_group()
    target.add_argument('--address')
    target.add_argument('--name')
    p.add_argument('--erase-files', action='store_true', help='Acknowledge erasing ALL device files in the shared OTA slot')
    p.add_argument('--chunk', type=int, default=224, choices=range(16, 225, 16))
    p.add_argument('--scan-seconds', type=float, default=8)
    p.add_argument('--stop-after', type=int, default=0, help='Abort after N bytes to exercise recovery without installation')
    args = p.parse_args()
    package = None
    if args.file or args.check_file:
        package = unpack_package((args.file or args.check_file).read_bytes())
        print(json.dumps(package[0], indent=2), flush=True)
    if args.check_file:
        return
    if args.file and not args.erase_files:
        p.error('OTA erases stored device files. Back them up first, then pass --erase-files.')
    asyncio.run(run(args, package))


if __name__ == '__main__':
    main()
