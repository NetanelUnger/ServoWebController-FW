"""Offline failure-path tests. No compiler, Bluetooth adapter or device is used."""
import asyncio
from contextlib import asynccontextmanager, redirect_stdout
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch

import build_ota
import common
import update_ota
import ota_ble
from ota_package import ACTIVE, pack_image, unpack_package


def sample_package():
    image = bytearray(b"\xff" * 0x170)
    struct.pack_into("<II", image, 0, 0x20020000, ACTIVE + 0x101)
    struct.pack_into("<I", image, 0x160, ACTIVE + 0x168)
    struct.pack_into("<I", image, 0x168, 0x94448A29)
    return pack_image(image, "test.1")


def arguments(**kwargs):
    return SimpleNamespace(**(dict(address=None, name=None, scan=False, version=False, probe=False,
                                   erase_files=False, scan_seconds=1, chunk=224, stop_after=0) | kwargs))


def devices(count=1):
    return {str(i): (SimpleNamespace(address=f"00:00:00:00:00:{i:02x}", name="NL123456"),
                     SimpleNamespace(local_name="NL123456", service_uuids=[ota_ble.NUS], rssi=-40))
            for i in range(count)}


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "ota"
        self.output.mkdir()
        self.quiet = redirect_stdout(io.StringIO())
        self.quiet.__enter__()
        self.addCleanup(self.quiet.__exit__, None, None, None)

    def publish(self):
        binary = self.root / "app.bin"
        binary.write_bytes(sample_package()[64:])
        return build_ota.publish_package(binary, "test.1", self.output)

    def test_publish_and_latest_roundtrip(self):
        path, info = self.publish()
        actual, metadata, _ = common.latest_package(self.output)
        self.assertEqual(actual, path)
        self.assertEqual(metadata["sha256"], info["sha256"])

    def test_latest_works_after_checkout_is_moved(self):
        path, info = self.publish()
        info["path"] = str(self.root / "old_checkout" / path.name)
        (self.output / "latest.json").write_text(json.dumps(info))
        self.assertEqual(common.latest_package(self.output)[0], path)

    def test_manifest_mismatch_is_rejected(self):
        _, info = self.publish()
        info["sha256"] = "0" * 64
        (self.output / "latest.json").write_text(json.dumps(info))
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            common.latest_package(self.output)

    def test_corrupt_payload_is_rejected_before_bluetooth(self):
        package = bytearray(sample_package())
        package[-1] ^= 1
        path = self.root / "bad.ota.bin"
        path.write_bytes(package)
        with patch.object(update_ota.asyncio, "run") as run:
            with self.assertRaisesRegex(ValueError, "Damaged"):
                update_ota.main(["--file", str(path), "--erase-files"])
            run.assert_not_called()

    def test_invalid_image_keeps_previous_latest(self):
        self.publish()
        previous = (self.output / "latest.json").read_bytes()
        binary = self.root / "bad.bin"
        binary.write_bytes(b"bad")
        with self.assertRaises(ValueError):
            build_ota.publish_package(binary, "test.2", self.output)
        self.assertEqual((self.output / "latest.json").read_bytes(), previous)

    def test_zero_exit_without_new_outputs_rejects_old_binary(self):
        binary = self.root / "app.bin"
        binary.write_bytes(b"old")
        binary.with_suffix(".elf").write_bytes(b"old")
        with patch.object(build_ota, "ROOT", self.root), patch.object(build_ota, "run_logged"):
            with self.assertRaisesRegex(RuntimeError, "new artifact"):
                build_ota.build_target(self.root, self.root, self.root, "App/Debug", binary, self.output)
        self.assertFalse(binary.exists())

    def test_build_error_text_even_with_zero_exit_is_failure(self):
        def fake_run(command, **kwargs):
            kwargs["stdout"].write("fatal error: missing header\n")
            return SimpleNamespace(returncode=0)
        with patch.object(build_ota.subprocess, "run", side_effect=fake_run):
            with self.assertRaisesRegex(RuntimeError, "Command failed"):
                build_ota.run_logged(["fake"], self.output / "build.log")

    def test_explicit_bad_cubeide_path_does_not_fall_back(self):
        with self.assertRaisesRegex(RuntimeError, "not found"):
            common.find_cubeide(self.root / "missing")

    def test_noninteractive_upload_requires_erasure_acknowledgement(self):
        args = arguments()
        with patch.object(update_ota.sys.stdin, "isatty", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "--erase-files"):
                update_ota.confirm_update(args, devices()["0"][0])

    def test_cancel_before_connect_sends_no_writes(self):
        args = arguments()
        with patch("bleak.BleakScanner.discover", new=AsyncMock(return_value=devices())), \
             patch.object(update_ota.sys.stdin, "isatty", return_value=True), \
             patch("builtins.input", return_value=""), patch.object(ota_ble, "run", new=AsyncMock()) as run:
            with self.assertRaisesRegex(RuntimeError, "Cancelled before"):
                asyncio.run(update_ota.run(args, unpack_package(sample_package())))
            run.assert_not_called()

    def test_multiple_devices_need_explicit_selection(self):
        with patch.object(update_ota.sys.stdin, "isatty", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "Multiple"):
                update_ota.choose_device(devices(2), arguments())
        selected = update_ota.choose_device(devices(2), arguments(address="00:00:00:00:00:01"))
        self.assertEqual(selected.address, "00:00:00:00:00:01")

    def test_selected_device_passes_to_existing_protocol(self):
        package = unpack_package(sample_package())
        with patch("bleak.BleakScanner.discover", new=AsyncMock(return_value=devices())), \
             patch.object(ota_ble, "run", new=AsyncMock()) as run:
            asyncio.run(update_ota.run(arguments(erase_files=True), package))
            self.assertEqual(run.await_args.kwargs["device"].address, "00:00:00:00:00:00")
            self.assertEqual(run.await_args.args[1], package)


class ReconnectTests(unittest.IsolatedAsyncioTestCase):
    async def test_original_cli_uses_matching_device_not_last_discovered(self):
        found = devices(2)
        connected = []

        @asynccontextmanager
        async def connect(device, **kwargs):
            connected.append(device)
            yield object()

        with patch("bleak.BleakScanner.discover", new=AsyncMock(return_value=found)), \
             patch.object(ota_ble, "connection", connect), \
             patch.object(ota_ble, "version", AsyncMock(return_value="version")), redirect_stdout(io.StringIO()):
            await ota_ble.run(arguments(version=True, address=found["0"][0].address), None)
        self.assertEqual(connected, [found["0"][0]])

    async def test_original_cli_rejects_ambiguous_scan(self):
        with patch("bleak.BleakScanner.discover", new=AsyncMock(return_value=devices(2))), \
             patch.object(ota_ble, "connection") as connect:
            with self.assertRaisesRegex(RuntimeError, "2 matching devices"):
                await ota_ble.run(arguments(version=True), None)
            connect.assert_not_called()

    async def exercise(self, matching):
        package = unpack_package(sample_package())
        info, _ = package
        expected = f"OK VERSION {info['version']} IMAGE={info['crc32']} SIZE={info['length']} OTA=2"
        client = SimpleNamespace(write_gatt_char=AsyncMock())
        connected = []

        @asynccontextmanager
        async def connect(device, **kwargs):
            connected.append(device)
            yield client

        device = devices()["0"][0]
        with patch.object(ota_ble, "connection", connect), \
             patch.object(ota_ble, "version", AsyncMock(side_effect=["old image", expected if matching else "old image"])), \
             patch.object(ota_ble, "upload", AsyncMock(return_value=True)), \
             patch.object(ota_ble.asyncio, "sleep", AsyncMock()), \
             patch.object(ota_ble, "time", SimpleNamespace(monotonic=Mock(side_effect=[0, 1, 91]))), \
             patch("bleak.BleakScanner.discover", new=AsyncMock()) as scan, redirect_stdout(io.StringIO()):
            if matching:
                await ota_ble.run(arguments(), package, device=device)
                self.assertEqual(connected, [device, device.address])
            else:
                with self.assertRaisesRegex(RuntimeError, "Installation not verified"):
                    await ota_ble.run(arguments(), package, device=device)
            scan.assert_not_called()

    async def test_success_requires_matching_running_image(self):
        await self.exercise(True)

    async def test_wrong_running_image_does_not_report_success(self):
        await self.exercise(False)


if __name__ == "__main__":
    unittest.main()
