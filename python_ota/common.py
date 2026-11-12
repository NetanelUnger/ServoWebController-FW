"""Paths and checks shared by the Windows OTA launchers."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUTPUT = ROOT / "artifacts/ota"
sys.path.insert(0, str(ROOT / "tools"))
from ota_package import unpack_package  # noqa: E402


def require_windows():
    if sys.version_info < (3, 10):
        raise RuntimeError("Python 3.10 or newer is required. Python 3.13 x64 is recommended.")
    if sys.platform != "win32":
        raise RuntimeError("These launchers support Windows; see tools/ota_ble.py for other systems.")


def natural_key(path):
    return tuple(int(p) if p.isdigit() else p.lower() for p in re.split(r"(\d+)", str(path)))


def find_cubeide(explicit=None):
    """An explicit argument/environment setting must never silently fall back."""
    configured = explicit or os.environ.get("STM32CUBEIDE_ROOT")
    if configured:
        candidates = [Path(configured).expanduser()]
    else:
        candidates = []
        on_path = shutil.which("stm32cubeidec.exe")
        if on_path:
            candidates.append(Path(on_path).parent)
        found = []
        for base in (Path("C:/ST"), Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "ST"):
            if base.is_dir():
                found.extend(base.glob("STM32CubeIDE*"))
        candidates.extend(sorted(found, key=natural_key, reverse=True))
    for candidate in candidates:
        if candidate.is_file():
            candidate = candidate.parent
        for directory in (candidate, candidate / "STM32CubeIDE"):
            if (directory / "stm32cubeidec.exe").is_file():
                return directory.resolve()
    raise RuntimeError(
        "STM32CubeIDE was not found. Install STM32CubeIDE (including its ARM toolchain), "
        "then pass --cubeide-root \"C:\\ST\\STM32CubeIDE_2.2.0\\STM32CubeIDE\" "
        "or set STM32CUBEIDE_ROOT. Download: https://www.st.com/en/development-tools/stm32cubeide.html"
    )


def arm_tool(ide, name):
    matches = sorted(ide.glob(f"plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools*/tools/bin/{name}.exe"),
                     key=natural_key, reverse=True)
    if not matches:
        raise RuntimeError(f"{name}.exe is missing from {ide}. Repair the CubeIDE ARM toolchain installation.")
    return matches[0]


def check_build_tools(ide):
    for relative in ("configuration/config.ini", "configuration/org.eclipse.equinox.simpleconfigurator/bundles.info"):
        if not (ide / relative).is_file():
            raise RuntimeError(f"Incomplete CubeIDE installation: {ide / relative}")
    for name in ("arm-none-eabi-gcc", "arm-none-eabi-nm"):
        result = subprocess.run([str(arm_tool(ide, name)), "--version"], check=True,
                                capture_output=True, text=True)
        print(result.stdout.splitlines()[0], flush=True)
    print(f"STM32CubeIDE: {ide}", flush=True)


def load_package(path):
    path = Path(path).expanduser().resolve()
    if not path.is_file():
        raise RuntimeError(f"OTA package not found: {path}. Run build_ota.bat first.")
    info, image = unpack_package(path.read_bytes())
    return path, info, image


def latest_package(output=OUTPUT):
    manifest = output / "latest.json"
    if not manifest.is_file():
        raise RuntimeError("No successful OTA build found. Run build_ota.bat first, or use --file PATH.")
    metadata = json.loads(manifest.read_text(encoding="utf-8"))
    # Resolve beside the manifest, so moving/cloning the repo does not select
    # another checkout's package through an old absolute path.
    name = Path(metadata["path"]).name
    path, info, image = load_package(output / name)
    if any(metadata.get(key) != info[key] for key in ("version", "length", "crc32", "sha256")):
        raise RuntimeError("latest.json does not match its package. Rebuild or select a verified file explicitly.")
    return path, info, image


def show_package(path, info):
    print(f"\nOTA file: {path}\nVersion: {info['version']}\nImage bytes: {info['length']}"
          f"\nCRC32: {info['crc32']}\nSHA256: {info['sha256']}", flush=True)
