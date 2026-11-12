"""Select a controller and install a validated OTA package through laptop Bluetooth."""
from __future__ import annotations

import argparse
import asyncio
from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime
from pathlib import Path
import sys

from common import OUTPUT, latest_package, load_package, require_windows, show_package
import ota_ble


class Tee:
    def __init__(self, console, log):
        self.console, self.log = console, log

    def write(self, text):
        self.console.write(text)
        self.log.write(text)
        self.flush()

    def flush(self):
        self.console.flush()
        self.log.flush()


def choose_device(devices, args):
    matches = []
    for device, adv in devices.values():
        name = adv.local_name or device.name or ""
        if args.address:
            match = device.address.lower() == args.address.lower()
        elif args.name:
            match = name == args.name
        else:
            match = ota_ble.NUS in [uuid.lower() for uuid in adv.service_uuids] or name.startswith("NL")
        if match:
            matches.append((device, name, adv.rssi))
    matches.sort(key=lambda item: (item[1], item[0].address))
    for index, (device, name, rssi) in enumerate(matches, 1):
        print(f"{index}. {name or '(unnamed)'}  {device.address}  RSSI={rssi}")
    if args.scan:
        if not matches:
            print("No controllers found. Check power, Bluetooth, and disconnect the website/phone.")
        return None
    if not matches:
        raise RuntimeError("No matching controller found. Check power/Bluetooth and close other BLE clients.")
    if len(matches) == 1:
        return matches[0][0]
    if args.address or args.name or not sys.stdin.isatty():
        raise RuntimeError("Multiple matching controllers. Use --address to select exactly one.")
    try:
        index = int(input("Controller number (Ctrl+C cancels): ")) - 1
    except ValueError as exc:
        raise RuntimeError("Enter a controller number from the list.") from exc
    if not 0 <= index < len(matches):
        raise RuntimeError("Controller number is outside the list.")
    return matches[index][0]


def confirm_update(args, device):
    print(f"\nSelected controller: {device.name} ({device.address})")
    print("OTA erases ALL stored images and timelines, including if upload is cancelled.\n"
          "Export needed device files first. The firmware will reboot after installation.")
    if args.erase_files:
        return
    if not sys.stdin.isatty():
        raise RuntimeError("Upload requires --erase-files to acknowledge device-file erasure.")
    if input("Type UPDATE to upload, or press Enter to cancel: ").strip() != "UPDATE":
        raise RuntimeError("Cancelled before upload; device files were not changed.")


async def run(args, package):
    from bleak import BleakScanner
    print(f"Scanning Bluetooth for {args.scan_seconds:g} seconds ...", flush=True)
    devices = await BleakScanner.discover(timeout=args.scan_seconds, return_adv=True)
    device = choose_device(devices, args)
    if args.scan:
        return
    if not args.version:
        confirm_update(args, device)
    # Reuse the protocol implementation: acknowledged writes, durable Flash
    # status, full-image VERIFY, INSTALL, reconnect and VERSION/size/CRC match.
    await ota_ble.run(args, package, device=device)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--file", type=Path, help="Explicit .ota.bin to upload")
    mode.add_argument("--latest", action="store_true", help="Use latest successful build (default)")
    mode.add_argument("--check-file", type=Path, help="Validate offline; no Bluetooth needed")
    mode.add_argument("--scan", action="store_true", help="List nearby controllers; no writes")
    mode.add_argument("--version", action="store_true", help="Read installed VERSION; no upload")
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--address")
    target.add_argument("--name")
    parser.add_argument("--erase-files", action="store_true", help="Acknowledge erasure without an interactive prompt")
    parser.add_argument("--chunk", type=int, default=224, choices=range(16, 225, 16))
    parser.add_argument("--scan-seconds", type=float, default=8)
    args = parser.parse_args(argv)
    if not 0 < args.scan_seconds <= 120:
        parser.error("--scan-seconds must be greater than zero and at most 120")
    args.probe = False
    args.stop_after = 0
    package = None
    if not (args.scan or args.version):
        path, info, image = load_package(args.file or args.check_file) if (args.file or args.check_file) else latest_package()
        show_package(path, info)
        package = info, image
    if args.check_file:
        print("PASS: package header, target, CRC, vectors and OTA tail are valid.")
        return 0
    require_windows()
    if sys.getwindowsversion().build < 22000:
        raise RuntimeError("This Bleak version requires Windows 11 (build 22000+) for Bluetooth.")
    if package and not args.erase_files and not sys.stdin.isatty():
        parser.error("OTA erases stored images/timelines; pass --erase-files to acknowledge this.")
    asyncio.run(run(args, package))
    return 0


def entrypoint():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    log_path = OUTPUT / f"ble-{datetime.now():%Y%m%d-%H%M%S-%f}.log"
    with log_path.open("w", encoding="utf-8") as log:
        with redirect_stdout(Tee(sys.stdout, log)), redirect_stderr(Tee(sys.stderr, log)):
            print(f"Session log: {log_path}")
            try:
                return main()
            except KeyboardInterrupt:
                print("\nCancelled. If INSTALL was sent, reconnect and check VERSION before retrying.", file=sys.stderr)
                return 130
            except Exception as exc:
                print(f"OTA FAILED: {type(exc).__name__}: {exc}\n"
                      "Close other Bluetooth clients; check Windows Bluetooth and device power.\n"
                      "A disconnect alone does not confirm installation. See the session log.", file=sys.stderr)
                return 1


if __name__ == "__main__":
    sys.exit(entrypoint())
