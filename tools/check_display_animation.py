"""Run the real display packet/task C module on an emulated ARM CPU, without hardware.

Requires arm-none-eabi-gcc, pyelftools, unicorn and Pillow. HAL/RTOS boundaries
are mocked; this does not simulate SPI timing, the RTOS scheduler or a real panel.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from PIL import Image, ImageDraw
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'STM32CubeIDE/Application/User/Core'
STUBS = {
    'main.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned dummy; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define UNUSED(x) ((void)(x))
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void Error_Handler(void);
''',
    'cmsis_os2.h': '''#pragma once
#include <stdint.h>
typedef void *osThreadId_t;
typedef void *osMutexId_t;
typedef int osStatus_t;
#define osOK 0
#define osErrorResource -3
#define osWaitForever 0xffffffffU
#define osFlagsWaitAny 0U
osStatus_t osMutexAcquire(osMutexId_t, uint32_t);
osStatus_t osMutexRelease(osMutexId_t);
uint32_t osThreadFlagsSet(osThreadId_t, uint32_t);
uint32_t osThreadFlagsWait(uint32_t, uint32_t, uint32_t);
uint32_t osKernelGetTickCount(void);
''',
    'app_freertos.h': '''#pragma once
#include "cmsis_os2.h"
extern osThreadId_t displayTaskHandle;
extern osMutexId_t displayMutexHandle;
#define pdMS_TO_TICKS(ms) (ms)
''',
    'stubs.c': '''#include "display/display.h"
#include "display/gc9a01.h"
#include "app_freertos.h"
osThreadId_t displayTaskHandle = (void *)1;
osMutexId_t displayMutexHandle = (void *)2;
sFONT Font24 = {0, 17, 24};
uint32_t HAL_GetTick(void) { return 0; }
uint32_t osKernelGetTickCount(void) { return 0; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState s) {}
void Error_Handler(void) { for (;;) {} }
osStatus_t osMutexAcquire(osMutexId_t m, uint32_t t) { return 0; }
osStatus_t osMutexRelease(osMutexId_t m) { return 0; }
uint32_t osThreadFlagsSet(osThreadId_t t, uint32_t f) { return f; }
uint32_t osThreadFlagsWait(uint32_t f, uint32_t o, uint32_t t) { return 0; }
void GC9A01_SetTransferTimeout(uint32_t t) {}
HAL_StatusTypeDef GC9A01_BlitWire(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *p) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_Init(void) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_SetRotation(uint8_t r) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_SetDisplayEnabled(bool e) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_BlitRGB565(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *p) { return HAL_OK; }
HAL_StatusTypeDef GC9A01_DrawText(uint16_t x, uint16_t y, const char *s, const sFONT *f, uint16_t fg, uint16_t bg) { return HAL_OK; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
''',
}


class Machine:
    STOP = 0x80000

    def __init__(self, elf):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.uc.mem_map(0x10000, 0x80000)
        self.uc.mem_map(0x20000000, 0x20000)
        with open(elf, 'rb') as stream:
            executable = ELFFile(stream)
            for segment in executable.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    self.uc.mem_write(segment['p_vaddr'], segment.data())
            self.symbols = {s.name: s['st_value'] for s in executable.get_section_by_name('.symtab').iter_symbols()}
        self.hooks = {self.symbols[name] & ~1: name for name in [
            'HAL_GetTick', 'osKernelGetTickCount', 'HAL_GPIO_WritePin', 'Error_Handler',
            'osMutexAcquire', 'osMutexRelease', 'osThreadFlagsSet', 'osThreadFlagsWait',
            'GC9A01_SetTransferTimeout', 'GC9A01_BlitWire', 'GC9A01_Init', 'GC9A01_SetRotation', 'GC9A01_SetDisplayEnabled',
            'GC9A01_FillRect', 'GC9A01_BlitRGB565', 'GC9A01_DrawText']}
        self.image = Image.new('RGB', (240, 240), 'white')
        self.now = 0
        self.locked = False
        self.require_lock = False
        self.calls = []
        self.wait = None
        self.fail_next = False
        self.task_context = None
        self.mutex_busy = False
        self.wire_pointer = None
        self.spi_timeout = 1000
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def args(self):
        return [self.uc.reg_read(r) for r in [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]]

    def ret(self, value=0):
        self.uc.reg_write(UC_ARM_REG_R0, value & 0xffffffff)
        self.uc.reg_write(UC_ARM_REG_PC, self.uc.reg_read(UC_ARM_REG_LR))

    def hook(self, uc, address, size, data):
        if address == self.STOP:
            uc.emu_stop()
            return
        name = self.hooks.get(address)
        if name is None:
            return
        a = self.args()
        if name in ('HAL_GetTick', 'osKernelGetTickCount'):
            self.ret(self.now)
        elif name == 'osMutexAcquire':
            if self.mutex_busy:
                self.ret(-3)
                return
            assert not self.locked, 'Recursive/unserialized panel ownership'
            self.locked = True
            self.ret()
        elif name == 'osMutexRelease':
            assert self.locked
            self.locked = False
            self.ret()
        elif name == 'osThreadFlagsSet':
            self.ret(a[1])
        elif name == 'osThreadFlagsWait':
            assert not self.locked, 'Worker slept while owning the panel'
            self.wait = a[2]
            self.ret()
            uc.emu_stop()
        elif name == 'GC9A01_SetTransferTimeout':
            assert self.locked
            assert a[0] > 10, 'SPI timeout must exceed the 10 ms HAL tick quantum'
            self.spi_timeout = a[0]
            self.ret()
        elif name == 'Error_Handler':
            raise AssertionError('Unexpected Error_Handler')
        else:
            if self.require_lock:
                assert self.locked, f'{name} bypassed display lock'
            self.calls.append((name, a))
            if self.fail_next and name.startswith('GC9A01_'):
                self.fail_next = False
                self.ret(1)
                return
            if name == 'GC9A01_BlitWire':
                self.wire_pointer = struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM_REG_SP), 4))[0]
            if name in ('GC9A01_FillRect', 'GC9A01_BlitRGB565'):
                x, y, w, h = a
                assert 0 < w <= 240 - x and 0 < h <= 240 - y, 'Out-of-panel write'
                fifth = struct.unpack('<I', uc.mem_read(uc.reg_read(UC_ARM_REG_SP), 4))[0]
                if name == 'GC9A01_FillRect':
                    ImageDraw.Draw(self.image).rectangle((x, y, x + w - 1, y + h - 1), fill=rgb565(fifth))
                else:
                    pixels = struct.unpack(f'<{w*h}H', uc.mem_read(fifth, w*h*2))
                    patch = Image.new('RGB', (w, h))
                    patch.putdata([rgb565(c) for c in pixels])
                    self.image.paste(patch, (x, y))
            self.ret()

    def call(self, name, *args):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2001e000)
        self.uc.reg_write(UC_ARM_REG_LR, self.STOP | 1)
        for reg, arg in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args):
            self.uc.reg_write(reg, arg)
        self.uc.emu_start(self.symbols[name] | 1, self.STOP + 2, count=8000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == self.STOP, f'{name} did not return'
        return self.uc.reg_read(UC_ARM_REG_R0)

    def task_step(self):
        self.require_lock = True
        self.wait = None
        if self.task_context:
            self.uc.context_restore(self.task_context)
            start = self.uc.reg_read(UC_ARM_REG_PC) | 1
        else:
            self.uc.reg_write(UC_ARM_REG_SP, 0x2001a000)
            self.uc.reg_write(UC_ARM_REG_LR, self.STOP | 1)
            start = self.symbols['Display_Task'] | 1
        self.uc.emu_start(start, self.STOP + 2, count=8000000)
        assert self.wait is not None, 'Worker failed to block'
        self.task_context = self.uc.context_save()


def rgb565(color):
    r, g, b = color >> 11, (color >> 5) & 63, color & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def verify(elf, output):
    m = Machine(elf)
    address = 0x20018000
    def send(data):
        m.uc.mem_write(address, bytes(data))
        return m.call('Display_WritePacket', address, len(data))
    valid = [1, 0, 0, 120, 1, 0xf8, 0]
    assert send(valid) == 0x81, 'Drawing before initialization'
    m.task_step()
    assert m.wait == 0xffffffff
    assert any(c[0] == 'GC9A01_DrawText' for c in m.calls)
    m.calls.clear()
    m.task_step()
    assert not m.calls, 'Idle task redraws the panel'
    assert send(valid) == 0
    assert m.calls[-1][0] == 'GC9A01_FillRect'
    assert send([1,0,0,240,240,0xff,0xff]) == 0
    assert m.calls[-1] == ('GC9A01_FillRect', [0, 0, 240, 240])
    raw = [2, 239, 239, 1, 1, 0x07, 0xe0]
    assert send(raw) == 0
    assert m.calls[-1][0] == 'GC9A01_BlitWire'
    assert m.wire_pointer == address + 5, 'Pixel payload was copied'
    m.mutex_busy = True
    assert send(raw) == 0x81
    m.mutex_busy = False
    assert m.call('Display_WritePacket', 0, 7) == 0x80
    for data in ([], [1], [1,0,0,0,1,0,0], [1,239,0,2,1,0,0],
                 [1,1,0,240,240,0,0], [2,0,0,2,1,0,0],
                 [4,0,0,1,1,0,0], valid+[0], raw[:-1]):
        m.calls.clear()
        assert send(data) == 0x80 and not m.calls, data
    assert send([2,0,0,120,1]+[0x12,0x34]*120) == 0
    row = [3, 10, 20, 0xf8, 0, 0x07, 0xe0]
    assert send(row) == 0
    assert m.calls[-1] == ('GC9A01_BlitWire', [10, 20, 2, 1])
    assert m.wire_pointer == address + 3, 'Row payload was copied'
    assert send([3, 118, 239] + [0x12, 0x34]*122) == 0
    assert m.calls[-1] == ('GC9A01_BlitWire', [118, 239, 122, 1])
    assert send([3, 239, 239, 0xff, 0xff]) == 0
    for data in ([3,0,0], [3,0,0,0], [3,0,0,0,0,0],
                 [3,239,0,0,0,0,0], [3,0,240,0,0],
                 [3,119,0]+[0,0]*122, [3,0,0]+[0,0]*123):
        m.calls.clear()
        assert send(data) == 0x80 and not m.calls, data
    assert m.call('Display_Stop') == 0
    assert send(valid) == 0x81
    assert m.call('Display_Start') == 0
    assert send(valid) == 0x81
    m.task_step()
    m.fail_next = True
    assert send(raw) == 0x82
    assert send(raw) == 0, 'Transient SPI fault permanently disabled remote drawing'
    m.call('Display_Start')
    m.task_step()
    assert send(raw) == 0
    m.call('Display_WriteControlPin', 0x1234, 4, 0)
    assert send(raw) == 0x81
    print('PASS: one-shot startup, idle, rectangle/row boundaries, exact lengths, maximum packet, stop/start, GPIO ownership, SPI failure and recovery')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='servo-display-test-') as tmp:
        tmp = Path(tmp)
        for name, content in STUBS.items():
            (tmp / name).write_text(content)
        elf = tmp / 'display.elf'
        subprocess.run([args.cc, '-mcpu=cortex-m3', '-mthumb', '-O1', '-g', '-ffreestanding', '-nostdlib',
                        '-I' + str(tmp), '-I' + str(APP), '-I' + str(ROOT / 'Utilities/Fonts'),
                        str(APP / 'display/display.c'), str(tmp / 'stubs.c'),
                        '-Wl,-Ttext=0x10000,-Tdata=0x20000000,-e,Display_Task', '-lgcc', '-o', str(elf)], check=True)
        verify(elf, args.output)


if __name__ == '__main__':
    main()
