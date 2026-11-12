"""Run actual OTA receiver/boot installer C with Flash faults (Windows/MSVC).

Only the platform, RTOS and Flash driver are mocked. Production functions and
the shared image validator are read from their source at test time.
"""
import re
import shutil
import subprocess
from pathlib import Path
from check_ble_transport import find_msvc_environment

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^(?:static )?(?:void|uint8_t) ' + name + r'\([^;]*?\)\s*\{.*?^}', source, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group()


def main():
    build = ROOT / 'artifacts/ota-tests'
    build.mkdir(parents=True, exist_ok=True)
    env = find_msvc_environment()
    if not env:
        raise SystemExit('Visual Studio C compiler not installed')
    path = next(v for k, v in env.items() if k.upper() == 'PATH')
    compiler = shutil.which('cl.exe', path=path)
    ota = (ROOT / 'STM32_WPAN/App/ota_app.c').read_text()
    ota = re.sub(r'^#include .*$', '', ota, flags=re.M)
    boot = (ROOT / 'Bootloader/BLE_ApplicationInstallManager/Core/Src/main.c').read_text()
    boot_functions = '\n'.join(function(boot, n) for n in ('BootModeCheck', 'CheckFwAppValidity', 'DeleteSlot', 'MoveToActiveSlot'))
    (build / 'receiver.inc').write_text(ota)
    (build / 'installer.inc').write_text(boot_functions)
    exe = build / 'ota_test.exe'
    subprocess.run([compiler, '/nologo', '/TC', '/std:c11', '/O2', '/W3',
                    '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(build), '/I' + str(ROOT / 'Core/Inc'),
                    '/Fe:' + str(exe), '/Fo:' + str(build / 'ota.obj'),
                    str(ROOT / 'tools/test_ota.c')], cwd=build, env=env, check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
