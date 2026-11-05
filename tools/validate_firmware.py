"""Check linked firmware layout, boot tags and disabled I2C paths (no hardware needed)."""
import argparse
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--nm', required=True, help='Path to arm-none-eabi-nm.exe')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
app = root / 'STM32CubeIDE/Debug/BleServoController.bin'
boot = root / 'Bootloader/BLE_ApplicationInstallManager/STM32CubeIDE/Release/BLE_ApplicationInstallManager.bin'
data = app.read_bytes()
assert 0 < boot.stat().st_size <= 0x6000, 'Boot manager exceeds its reserved area'
assert len(data) <= 488 * 1024, 'Application exceeds the OTA active slot'
stack, reset = struct.unpack_from('<II', data)
assert 0x20000000 <= stack <= 0x20020000
assert reset & 1 and 0x08006000 <= (reset & ~1) < 0x08006000 + len(data)
tag = struct.unpack_from('<I', data, 0x160)[0] - 0x08006000
assert 0 <= tag <= len(data) - 4, 'OTA tag address invalid'
assert struct.unpack_from('<I', data, tag)[0] == 0x94448A29, 'OTA completion tag invalid'
symbols = subprocess.check_output([args.nm, str(app.with_suffix('.elf'))], text=True)
names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
assert 'vl53l5cx_init' not in names, 'Obsolete VL53L5CX firmware linked'
config = (root / 'STM32CubeIDE/Application/User/Core/config.h').read_text()
if '#define DISABLE_I2C_VL53L8CX 0' in config:
    assert {'vl53l8cx_Task', 'vl53l8cx_init', 'VL53L8CX_FIRMWARE'} <= names
for flag, symbol in [('PCA9685', 'pca9685_Init'), ('VL53L8CX', 'vl53l8cx_Task')]:
    if f'#define DISABLE_I2C_{flag} 1' in config:
        assert symbol not in names, f'Disabled device linked: {symbol}'
if '#define ENABLE_PCA9685_DIAGNOSTIC 1' not in config and all(f'#define DISABLE_I2C_{flag} 1' in config for flag in ['PCA9685', 'VL53L8CX']):
    for symbol in ['HAL_I2C_Mem_Read_IT', 'HAL_I2C_Mem_Write_IT', 'HAL_I2C_IsDeviceReady']:
        assert symbol not in names, f'I2C function linked: {symbol}'
print('PASS: boot slot, application slot, reset vector, OTA tags, disabled-device symbols')
