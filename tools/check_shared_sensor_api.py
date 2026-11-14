"""Exercise the built ARM firmware's IMU producer and VM sensor imports.

Uses pyelftools/Unicorn with mocked ST register reads, BLE and RTOS boundaries.
No hardware, motor commands or Flash writes. Does not simulate RF/I2C timing.
Run after tools/build.ps1; optionally pass a different firmware ELF path.
"""
import struct
import sys
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]
REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
INVALID = -2147483648


class Machine:
    def __init__(self, path):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(0x08000000, 0x100000)
        self.uc.mem_map(0x20000000, 0x40000)
        self.fields = {}
        with open(path, 'rb') as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    self.uc.mem_write(segment['p_vaddr'], segment.data())
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            for cu in elf.get_dwarf_info().iter_CUs():
                for die in cu.iter_DIEs():
                    attr = die.attributes.get('DW_AT_name')
                    name = attr.value.decode() if attr else ''
                    if die.tag != 'DW_TAG_variable' or name not in ('vm', 'g_imu', 'g_vl53l8cx_runtime'):
                        continue
                    dtype = die.get_DIE_from_attribute('DW_AT_type')
                    while dtype.tag != 'DW_TAG_structure_type':
                        dtype = dtype.get_DIE_from_attribute('DW_AT_type')
                    for member in dtype.iter_children():
                        if member.tag == 'DW_TAG_member':
                            self.fields[name, member.attributes['DW_AT_name'].value.decode()] = member.attributes['DW_AT_data_member_location'].value
        self.now = 10000
        self.mode = 2
        self.request = 1
        self.depth = 0
        self.fail = None
        self.configurations = 0
        self.packets = []
        self.on_driver_call = None
        boundary = {'vPortEnterCritical', 'vPortExitCritical', 'HAL_GetTick',
                    'IMU_BLE_Mode', 'IMU_BLE_RequestId', 'IMU_BLE_Applied', 'IMU_BLE_Publish'}
        self.hooks = {address & ~1: name for name, address in self.symbols.items()
                      if name in boundary or name.startswith('lsm6dsv320x_')}
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def hook(self, uc, address, size, data):
        name = self.hooks.get(address)
        if not name:
            return
        args = [uc.reg_read(r) for r in REGS]
        result = 0
        if name == 'vPortEnterCritical':
            self.depth += 1
        elif name == 'vPortExitCritical':
            self.depth -= 1
            assert self.depth >= 0
        elif name == 'HAL_GetTick':
            result = self.now
        elif name == 'IMU_BLE_Mode':
            result = self.mode
        elif name == 'IMU_BLE_RequestId':
            result = self.request
        elif name == 'IMU_BLE_Publish':
            assert self.depth == 0
            self.packets.append(bytes(uc.mem_read(args[0], 48)))
        elif name.startswith('lsm6dsv320x_'):
            assert self.depth == 0, 'I2C under a critical section'
            if self.on_driver_call:
                self.on_driver_call(name)
            if name == self.fail:
                result = -1
            else:
                values = {
                    'device_id_get': bytes([0x73]),
                    'flag_data_ready_get': bytes([15]),
                    'acceleration_raw_get': struct.pack('<3h', 1000, -2000, -32768),
                    'angular_rate_raw_get': struct.pack('<3h', -100, 200, -300),
                    'hg_acceleration_raw_get': struct.pack('<3h', 4, 5, 6),
                    'temperature_raw_get': struct.pack('<h', 123),
                    'timestamp_raw_get': struct.pack('<I', self.now * 40),
                    'sflp_quaternion_raw_get': struct.pack('<4H', 0x3c00, 0, 0, 0),
                    'sflp_gravity_raw_get': struct.pack('<3h', 0, 0, 16384),
                    'sflp_gbias_raw_get': struct.pack('<3h', 10, 20, 30),
                }
                suffix = name.removeprefix('lsm6dsv320x_')
                if suffix in values:
                    uc.mem_write(args[1], values[suffix])
                elif suffix == 'xl_setup':
                    self.configurations += 1
        uc.reg_write(UC_ARM_REG_R0, result & 0xffffffff)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2003f000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x080ff001)
        for reg, value in zip(REGS, args):
            self.uc.reg_write(reg, value & 0xffffffff)
        self.uc.emu_start(self.symbols[name] | 1, 0x080ff000, count=100000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == 0x080ff000, name + ' did not return'
        assert self.depth == 0
        return struct.unpack('<i', struct.pack('<I', self.uc.reg_read(UC_ARM_REG_R0)))[0]

    def write(self, name, value, fmt='I', member=None):
        at = self.symbols[name] + (self.fields[name, member] if member else 0)
        self.uc.mem_write(at, struct.pack('<' + fmt, value))

    def read(self, name, size, member=None):
        at = self.symbols[name] + (self.fields[name, member] if member else 0)
        return bytes(self.uc.mem_read(at, size))

    def ready(self):
        return self.call('native_tof_ready', 0), self.call('native_imu_ready', 0)


def verify(path):
    m = Machine(path)
    m.write('g_imu', 1, 'B', 'ready')
    m.call('IMU_Step')  # SFLP startup and status event
    assert m.read('g_imu', 1, 'motion_valid') == b'\0'
    m.now += 1001
    m.call('IMU_Step')  # discard the held startup sample
    m.call('IMU_Step')
    assert m.packets[-1][0] == 2
    expected = struct.pack('<6h', 1000, 2000, 32767, -100, -200, 300)
    assert m.read('g_imu', 12, 'motion') == expected
    assert m.read('g_imu', 1, 'motion_valid') == b'\1'
    assert struct.unpack_from('<4H', m.packets[-1], 16) == (0x3c00, 0, 0x8000, 0x8000)

    m.write('api_version', 2, 'B')
    m.write('vm', 8, member='file_id')
    m.write('filter_max', 65534, 'H')
    m.write('g_vl53l8cx_runtime', 1, 'B', 'ranging_active')
    m.write('g_vl53l8cx_runtime', 23, member='frames')
    m.write('g_vl53l8cx_runtime', m.now - 10, member='last_frame_tick_ms')
    m.write('g_vl53l8cx_runtime', 321, 'H', 'distance_mm')
    m.call('capture_sensors')
    assert m.ready() == (0, 0)
    assert m.call('native_tof_count', 0) == 0
    for _ in range(3):
        assert m.call('native_tof_enable', 0, 1) == 0
        assert m.call('native_imu_enable', 0, 1) == 0
        assert m.ready() == (1, 1)  # no intervening producer frame or capture
        assert m.call('native_tof_mm', 0, 0) == 321
        assert m.call('native_tof_frame_id', 0) == 23
        assert m.call('native_imu_accel_mg', 0, 1) == 244
        assert m.call('native_imu_gyro_mdps', 0, 1) == -14000
        assert m.call('native_imu_word', 0, 0) & 255 == 2
    m.call('IMU_Step')
    assert m.configurations == 1 and m.packets[-1][0] == 2
    # Producer activity must not change the guest's snapshot mid-call.
    assert m.call('native_imu_frame_id', 0) == 1
    m.call('capture_sensors')
    assert m.call('native_imu_frame_id', 0) == 2
    assert m.call('native_tof_enable', 0, 2) == -1
    assert m.call('native_imu_enable', 0, 2) == -1
    assert m.ready() == (1, 1)

    for sensor in ('tof', 'imu'):
        assert m.call('native_' + sensor + '_enable', 0, 0) == 0
        assert m.call('native_' + sensor + '_ready', 0) == 0
        assert m.call('native_' + sensor + '_frame_id', 0) == 0
        assert m.call('native_' + sensor + '_age_ms', 0) == -1
    assert m.call('native_tof_count', 0) == 0
    assert m.call('native_imu_gyro_mdps', 0, 1) == INVALID
    m.write('api_version', 1, 'B')
    m.call('capture_sensors')
    assert m.ready() == (1, 1)  # stored ABI-1 files keep implicit access
    m.write('api_version', 2, 'B')
    m.call('capture_sensors')
    assert m.ready() == (0, 0)
    m.call('IMU_Step')
    assert m.configurations == 1 and m.packets[-1][0] == 2  # BLE retains demand
    m.call('native_tof_enable', 0, 1)
    m.call('native_imu_enable', 0, 1)
    m.now += 500
    m.call('capture_sensors')
    assert m.ready() == (0, 0)
    assert m.call('native_imu_enable', 0, 1) == 0
    assert m.call('native_tof_enable', 0, 1) == 0
    assert m.ready() == (0, 0)  # success must not fabricate fresh readings

    m.mode = 1
    m.request += 1
    m.on_driver_call = lambda _: assert_invalid_motion(m)
    m.call('IMU_Step')
    m.on_driver_call = None
    m.now += 251
    m.call('IMU_Step')
    m.call('IMU_Step')
    assert m.packets[-1][0] == 1 and m.read('g_imu', 12, 'motion') == expected
    m.call('capture_sensors')
    assert m.call('native_imu_ready', 0) == 1
    assert m.call('native_imu_gyro_mdps', 0, 3) == INVALID
    m.fail = 'lsm6dsv320x_angular_rate_raw_get'
    m.call('IMU_Step')
    m.call('capture_sensors')
    assert m.call('native_imu_ready', 0) == 0
    assert m.call('native_imu_gyro_mdps', 0, 1) == INVALID
    m.write('g_vl53l8cx_runtime', 0, 'B', 'ranging_active')
    m.call('capture_sensors')
    assert m.call('native_tof_enable', 0, 1) == 0 and m.ready() == (0, 0)
    m.write('vm', 1, member='stop')
    assert m.call('native_imu_enable', 0, 1) == -2
    assert m.call('native_tof_enable', 0, 1) == -2
    assert m.ready() == (0, 0)
    print('PASS: actual ARM IMU/VM, warm TOF+SFLP reuse, repeated enables, units/axes, BLE preservation, disable, stale/cold data, mode transitions, read errors and STOP')


def assert_invalid_motion(machine):
    assert machine.read('g_imu', 1, 'motion_valid') == b'\0'


if __name__ == '__main__':
    verify(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'STM32CubeIDE/Debug/BleServoController.elf')
