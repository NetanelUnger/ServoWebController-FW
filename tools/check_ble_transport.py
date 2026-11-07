"""Compile and run the real BLE transport with deterministic host mocks.

Requires a host C compiler (MSVC, GCC or Clang). No board, BLE adapter, package
installation, firmware build, flashing or motor movement is involved.
"""
import argparse
import hashlib
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tools" / "test_ble_transport.c"
MOCKS = ROOT / "tools" / "ble_transport_mocks"


def find_msvc_environment():
    """Use an installed Visual Studio compiler without changing this shell."""
    installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    vswhere = installer / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.exists():
        return None
    found = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True,
    ).strip()
    if not found:
        return None
    setup = Path(found) / "VC/Auxiliary/Build/vcvars64.bat"
    output = subprocess.check_output(
        f'cmd.exe /d /s /c ""{setup}" >nul && set"',
        text=True,
    )
    env = os.environ.copy()
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key:
            # Windows environment names are case-insensitive.
            for old in list(env):
                if old.upper() == key.upper():
                    del env[old]
            env[key] = value
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="Host C compiler path; defaults to installed compiler")
    parser.add_argument("--links", type=int, choices=(4, 8), default=4, help="Mock stack link capacity (default4, boundary8)")
    args = parser.parse_args()
    production = [ROOT / name for name in (
        "STM32_WPAN/App/ble_tx.c", "STM32_WPAN/App/ble_tx.h", "Core/Src/app_freertos.c",
        "STM32_WPAN/App/nus_app.c", "STM32_WPAN/App/nus.c")]
    fingerprints = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in production}
    for path, digest in fingerprints.items():
        print("SOURCE", path.relative_to(ROOT).as_posix(), digest, flush=True)
    env = None
    cc = args.cc or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc and os.name == "nt":
        env = find_msvc_environment()
        if env:
            path = next(value for key, value in env.items() if key.upper() == "PATH")
            cc = shutil.which("cl.exe", path=path)
    if not cc:
        parser.error("No host C compiler found; use --cc or an existing developer shell.")
    msvc = Path(cc).name.lower() in ("cl", "cl.exe")
    with tempfile.TemporaryDirectory(prefix="ble-transport-test-") as directory:
        build = Path(directory)
        executable = build / ("ble_transport_test.exe" if os.name == "nt" else "ble_transport_test")
        # Compile the exact callback function from the generated task source,
        # without mocking all unrelated peripheral/task definitions in that file.
        for filename, function, output in [
            ("Core/Src/app_freertos.c", "osStatus_t NUS_APP_EnqueueRX", "rx_under_test.inc"),
            ("STM32_WPAN/App/nus_app.c", "void NusRxTask", "rx_task_under_test.inc"),
            ("STM32_WPAN/App/nus.c", "static void NUS_PermitRead", "nus_permit_read_under_test.inc"),
            ("STM32_WPAN/App/nus.c", "static void NUS_PermitWrite", "nus_permit_write_under_test.inc"),
        ]:
            source = ROOT / filename
            text = source.read_text(encoding="utf-8-sig")
            pattern = "^" + re.escape(function) + r"\([^)]*\)\s*\{.*?^}"
            match = re.search(pattern, text, re.M | re.S)
            if not match:
                raise SystemExit("Cannot locate production function: " + function)
            line = text.count("\n", 0, match.start()) + 1
            (build / output).write_text(
                f'#line {line} "{source.as_posix()}"\n' + match.group() + "\n", encoding="utf-8")
        if msvc:
            command = [cc, "/nologo", "/TC", "/std:c11", "/W4", "/WX", "/Od", "/Zi",
                       "/D_CRT_SECURE_NO_WARNINGS", f"/DCFG_BLE_NUM_LINK={args.links}U", "/I" + str(MOCKS), "/I" + str(build),
                       "/Fe:" + str(executable), "/Fo:" + str(build / "test.obj"),
                       "/Fd:" + str(build / "test.pdb"), str(TEST)]
        else:
            command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0", "-g",
                       f"-DCFG_BLE_NUM_LINK={args.links}U", "-I" + str(MOCKS), "-I" + str(build), str(TEST), "-o", str(executable)]
        print("Compiling actual firmware C with mocked stack/RTOS:", TEST.name, flush=True)
        subprocess.run(command, cwd=build, env=env, check=True, timeout=60)
        scenarios = subprocess.check_output([str(executable), "--list"], text=True, timeout=10).splitlines()
        failed = []
        for scenario in scenarios:
            try:
                result = subprocess.run([str(executable), scenario], cwd=build, timeout=10)
            except subprocess.TimeoutExpired:
                failed.append(scenario + " (nonblocking step hung)")
                continue
            if result.returncode:
                failed.append(scenario)
        if failed:
            raise SystemExit("Failed scenarios: " + ", ".join(failed))
        print(f"PASS {len(scenarios)} compiled production-code scenarios")
    if any(hashlib.sha256(path.read_bytes()).hexdigest() != digest for path, digest in fingerprints.items()):
        raise SystemExit("Production sources changed during testing; rerun against a stable snapshot.")
    print("Software-only result. RF timing, controller event ordering, peer reception and hardware remain unverified.")


if __name__ == "__main__":
    main()
