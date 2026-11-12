"""Create an isolated Python environment, install dependencies, and launch OTA tools."""
from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import sys
import venv

HERE = Path(__file__).resolve().parent
ENVIRONMENT = HERE / ".venv"
PYTHON = ENVIRONMENT / "Scripts/python.exe"
REQUIREMENTS = HERE / "requirements.txt"


def ensure_environment():
    from common import require_windows
    require_windows()
    if not PYTHON.is_file():
        print(f"Creating isolated Python environment: {ENVIRONMENT}", flush=True)
        venv.EnvBuilder(with_pip=True).create(ENVIRONMENT)
    fingerprint = hashlib.sha256(REQUIREMENTS.read_bytes()).hexdigest()
    marker = ENVIRONMENT / "ota-requirements.sha256"
    probe = subprocess.run([str(PYTHON), "-c",
                            "import bleak; from bleak.backends.winrt import client, scanner; "
                            "from importlib.metadata import version; assert version('bleak') == '3.1.0'"],
                           capture_output=True, text=True)
    ready = probe.returncode == 0 and marker.is_file() and marker.read_text().strip() == fingerprint
    if not ready:
        if subprocess.run([str(PYTHON), "-m", "pip", "--version"], capture_output=True).returncode:
            subprocess.run([str(PYTHON), "-m", "ensurepip", "--upgrade"], check=True)
        print("Installing OTA libraries (first run requires internet) ...", flush=True)
        subprocess.run([str(PYTHON), "-m", "pip", "install", "--disable-pip-version-check",
                        "--only-binary=:all:", "-r", str(REQUIREMENTS)], check=True)
        subprocess.run([str(PYTHON), "-c", "import bleak; from bleak.backends.winrt import client, scanner"], check=True)
    subprocess.run([str(PYTHON), "-m", "pip", "check"], check=True)
    marker.write_text(fingerprint + "\n", encoding="ascii")
    print(f"Python environment ready: {PYTHON}", flush=True)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print("NatiLab firmware tools\n"
              "  1. Build firmware and create OTA file\n"
              "  2. Upload latest OTA file through Bluetooth\n"
              "  3. Scan Bluetooth controllers\n"
              "  4. Check Python libraries and STM32CubeIDE\n"
              "  0. Exit")
        choice = input("Choose [1-4, 0]: ").strip()
        if choice == "0":
            return 0
        choices = {"1": ["build"], "2": ["update"], "3": ["update", "--scan"], "4": ["check"]}
        if choice not in choices:
            raise RuntimeError("Invalid menu choice.")
        argv = choices[choice]
    mode, *arguments = argv
    if mode not in ("build", "update", "check"):
        raise RuntimeError("Usage: run.bat [build|update|check] [options]")
    ensure_environment()
    script = "update_ota.py" if mode == "update" else "build_ota.py"
    if mode == "check":
        arguments.insert(0, "--check")
    return subprocess.run([str(PYTHON), "-u", str(HERE / script), *arguments]).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        sys.exit(130)
    except Exception as exc:
        print(f"SETUP FAILED: {exc}\n"
              "Install Python 3.10+ x64 with pip/venv (Python 3.13 recommended). "
              "Check internet/proxy access to PyPI on first run.\n"
              "If the environment was moved or Python was removed, delete only python_ota/.venv and retry.",
              file=sys.stderr)
        sys.exit(1)
