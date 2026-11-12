"""Compile the actual BLE link policy with deterministic HCI mocks (MSVC)."""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from check_ble_transport import find_msvc_environment

ROOT = Path(__file__).resolve().parents[1]
env = find_msvc_environment()
if not env:
    raise SystemExit('Visual Studio C compiler not installed')
path = next(v for k, v in env.items() if k.upper() == 'PATH')
compiler = shutil.which('cl.exe', path=path)
with tempfile.TemporaryDirectory(prefix='ble-link-test-') as directory:
    build = Path(directory)
    source = (ROOT / 'STM32_WPAN/App/ble_link.c').read_text(encoding='utf-8')
    (build / 'link_under_test.inc').write_text(re.sub(r'^#include .*$', '', source, flags=re.M))
    dispatcher = (ROOT / 'STM32_WPAN/App/app_ble.c').read_text(encoding='utf-8')
    match = re.search(r'^static void HciAsyncEvt_Task_Entry\([^;]*?\)\s*\{.*?^}', dispatcher, re.M | re.S)
    if not match:
        raise SystemExit('HCI dispatcher function not found')
    (build / 'dispatcher_under_test.inc').write_text(match.group())
    exe = build / 'test.exe'
    subprocess.run([compiler, '/nologo', '/TC', '/std:c11', '/W4', '/WX',
                    '/I' + str(build), '/Fe:' + str(exe), '/Fo:' + str(build / 'test.obj'),
                    str(ROOT / 'tools/test_ble_link.c')], cwd=build, env=env, check=True)
    subprocess.run([str(exe)], check=True)
