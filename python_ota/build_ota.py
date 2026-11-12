"""Clean-build the boot manager and application, validate, and publish an OTA v2 file."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from common import ROOT, OUTPUT, arm_tool, check_build_tools, find_cubeide, require_windows, show_package
from ota_package import pack_image, unpack_package

TARGETS = (
    ("BLE_ApplicationInstallManager/Release",
     ROOT / "Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE/Release/BLE_ApplicationInstallManager.bin"),
    ("BleServoController/Debug", ROOT / "STM32CubeIDE/Debug/BleServoController.bin"),
)


def run_logged(command, log):
    print(f"Running: {command[0]}\nLog: {log}", flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run([str(arg) for arg in command], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    text = log.read_text(encoding="utf-8", errors="replace")
    if result.returncode or re.search(r"fatal error:|error:|build failed|cannot run program", text, re.I):
        print("\n".join(text.splitlines()[-25:]), flush=True)
        raise RuntimeError(f"Command failed (exit {result.returncode}); see {log}")
    return text


def build_target(ide, configuration, workspace, target, binary, output):
    print(f"\nBuilding {target} ...", flush=True)
    # Remove only these known build products. A headless build can return zero
    # without finding its target; it must not allow packaging an old binary.
    artifacts = (binary, binary.with_suffix(".elf"))
    for artifact in artifacts:
        artifact.resolve().relative_to(ROOT)
        artifact.unlink(missing_ok=True)
    log = output / (target.replace("/", "-") + ".log")
    run_logged([
        ide / "stm32cubeidec.exe", "--launcher.suppressErrors", "-nosplash",
        "-configuration", configuration, "-application", "org.eclipse.cdt.managedbuilder.core.headlessbuild",
        "-data", workspace, "-import", ROOT / "STM32CubeIDE",
        "-import", ROOT / "Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE", "-cleanBuild", target,
    ], log)
    for artifact in artifacts:
        if not artifact.is_file() or not artifact.stat().st_size:
            raise RuntimeError(f"Build did not produce a new artifact: {artifact}. See {log}")
    print(f"Built {binary.name}: {binary.stat().st_size} bytes", flush=True)


def publish_package(binary, version, output):
    package = pack_image(binary.read_bytes(), version)
    info, _ = unpack_package(package)
    path = (output / f"NatiLab-{version}-{info['crc32']}.ota.bin").resolve()
    info.update(path=str(path), file_uri=path.as_uri())
    # Each publication is atomic; latest.json changes only after a validated
    # package and its metadata exist. A failed build keeps the previous release.
    for destination, data in (
        (path, package),
        (path.with_suffix(".json"), (json.dumps(info, indent=2) + "\n").encode()),
        (output / "latest.json", (json.dumps(info, indent=2) + "\n").encode()),
    ):
        temporary = destination.with_name(destination.name + ".tmp")
        temporary.write_bytes(data)
        temporary.replace(destination)
    return path, info


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cubeide-root", "-CubeIdeRoot", type=Path)
    parser.add_argument("--check", action="store_true", help="Check compiler/IDE only; do not build")
    args = parser.parse_args(argv)
    require_windows()
    ide = find_cubeide(args.cubeide_root)
    check_build_tools(ide)
    if args.check:
        return 0
    version_source = (ROOT / "Core/Inc/firmware_version.h").read_text(encoding="utf-8")
    match = re.search(r'^#define FIRMWARE_VERSION "([A-Za-z0-9.+_-]{1,23})"', version_source, re.M)
    if not match:
        raise RuntimeError("Invalid FIRMWARE_VERSION in Core/Inc/firmware_version.h")
    version = match.group(1)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # Isolate Eclipse state from the user's GUI workspace and IDE installation.
    key = hashlib.sha256(str(ide).encode()).hexdigest()[:12]
    state = ROOT / "artifacts/python-ota" / key
    configuration = state / "configuration"
    workspace = state / "workspace"
    for relative in ("config.ini", "org.eclipse.equinox.simpleconfigurator/bundles.info"):
        destination = configuration / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ide / "configuration" / relative, destination)
    # OS-owned lock is released even if Python exits unexpectedly.
    import msvcrt
    with (OUTPUT / "python-build.lock").open("a+b") as lock:
        lock.seek(0)
        lock.write(b"0")
        lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            raise RuntimeError("Another Python OTA build is running. Wait for it to finish.") from exc
        for target, binary in TARGETS:
            build_target(ide, configuration, workspace, target, binary, OUTPUT)
        if (ROOT / "Core/Inc/firmware_version.h").read_text(encoding="utf-8") != version_source:
            raise RuntimeError("Firmware version changed during compilation; rebuild before packaging.")
        print("\nChecking linked memory layout, vectors, OTA tags and enabled modules ...", flush=True)
        run_logged([sys.executable, ROOT / "tools/validate_firmware.py", "--nm", arm_tool(ide, "arm-none-eabi-nm")],
                   OUTPUT / "validation.log")
        path, info = publish_package(TARGETS[1][1], version, OUTPUT)
    show_package(path, info)
    print("\nBUILD PASSED. Upload this .ota.bin with update_ota.bat or the website.\n"
          "This build does not flash a device. Bootloader installation, when needed, uses ST-LINK.", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nBuild cancelled. Check latest.json for the last successfully published package.", file=sys.stderr)
        sys.exit(130)
    except Exception as exc:
        print(f"BUILD FAILED: {exc}", file=sys.stderr)
        sys.exit(1)
