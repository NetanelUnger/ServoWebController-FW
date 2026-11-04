"""Run the real display/face C modules on an emulated ARM CPU, without hardware.

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
            'GC9A01_Init', 'GC9A01_SetRotation', 'GC9A01_SetDisplayEnabled',
            'GC9A01_FillRect', 'GC9A01_BlitRGB565', 'GC9A01_DrawText']}
        self.image = Image.new('RGB', (240, 240), 'white')
        self.now = 0
        self.locked = False
        self.require_lock = False
        self.calls = []
        self.wait = None
        self.fail_next = False
        self.task_context = None
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
    m.task_step()
    assert m.wait == 2000
    assert any(c[0] == 'GC9A01_DrawText' for c in m.calls)
    m.now = 1999
    m.calls.clear()
    m.task_step()
    assert m.wait == 1 and not m.calls, 'Welcome duration was shortened'
    m.now = 2000
    m.task_step()
    assert any(c[0] == 'GC9A01_BlitRGB565' for c in m.calls)
    assert m.call('Display_Stop') == 0
    m.calls.clear()
    m.task_step()
    assert m.wait == 0xffffffff and not m.calls, 'Stopped task still polls or draws'
    assert m.call('Display_WriteControlPin', 0x1234, 4, 0) == 0
    assert m.calls[-1][0] == 'HAL_GPIO_WritePin'
    m.calls.clear()
    assert m.call('Display_Start') == 0
    m.task_step()
    assert m.calls[0][0] == 'GC9A01_Init', 'Resume did not recover panel configuration'
    m.call('Display_Stop')
    m.call('Display_Start')
    m.fail_next = True
    m.task_step()
    assert m.wait == 0xffffffff, 'SPI failure caused a retry spin'
    m.call('Display_Start')
    m.task_step()
    assert m.wait == 2000, 'Could not recover from a transfer failure'

    m = Machine(elf)
    assert m.call('FaceAnimation_Init', 0) == 0
    initial = m.image.copy()
    assert len(initial.getcolors(57600)) > 100, 'Color artwork was lost'
    m.calls.clear()
    m.call('FaceAnimation_Update', 500)
    assert not m.calls, 'Unchanged pose generated transfers'
    m.call('FaceAnimation_Init', 0)
    frames = []
    total_pixels = 0
    for now in range(0, 12001, 50):
        m.calls.clear()
        m.call('FaceAnimation_Update', now)
        total_pixels += sum(a[2]*a[3] for name,a in m.calls if name == 'GC9A01_BlitRGB565')
        frames.append(m.image.copy())
    assert frames[0].tobytes() == frames[-1].tobytes(), 'Loop boundary drift'
    assert frames[0].tobytes() != frames[32].tobytes(), 'Eyes never moved'
    bright = lambda im: sum(count for count,(r,g,b) in im.crop((44,80,112,140)).getcolors(57600) if r>150 and g>170 and b>170)
    assert bright(frames[83]) < bright(initial) / 3, 'Eyes did not close during blink'
    assert frames[156].crop((44,56,112,144)).tobytes() == initial.crop((44,56,112,144)).tobytes(), 'Wink closed both eyes'
    assert frames[156].crop((128,56,196,144)).tobytes() != initial.crop((128,56,196,144)).tobytes(), 'Wink did not close right eye'
    assert frames[108].crop((88,153,152,193)).tobytes() != initial.crop((88,153,152,193)).tobytes(), 'Surprised mouth missing'
    assert frames[136].crop((88,153,152,193)).tobytes() != frames[108].crop((88,153,152,193)).tobytes(), 'Happy mouth missing'
    # Repainting a dirty rectangle must fully erase a previous wide-open eye.
    fresh = Machine(elf)
    fresh.call('FaceAnimation_Init', 0)
    fresh.call('FaceAnimation_Update', 4150)
    assert fresh.image.tobytes() == frames[83].tobytes(), 'Eye movement left trails'
    # Every performance pose must match a fresh render, independent of history.
    for time in (1250, 2900, 4350, 5050, 5700, 6750, 7900, 8550, 10250, 11000):
        fresh.call('FaceAnimation_Init', 0)
        fresh.call('FaceAnimation_Update', time)
        assert fresh.image.tobytes() == frames[time//50].tobytes(), f'Dirty rectangle artifact at {time} ms'
    wrapped = Machine(elf)
    wrapped.call('FaceAnimation_Init', 0xffffff00)
    wrapped.call('FaceAnimation_Update', (0xffffff00 + 1600) & 0xffffffff)
    assert wrapped.image.tobytes() == frames[32].tobytes(), 'Clock rollover changed animation'
    # Exercise all packed rows, including any poses skipped by the timeline.
    for asset in range(30):
        for row in range(240 if asset == 0 else 88 if asset < 27 else 40):
            assert m.call('decode_row', asset, row, 0x20017000) == 0
    if output:
        output.mkdir(parents=True, exist_ok=True)
        mask = Image.new('L', (240, 240))
        ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)
        previews = [Image.composite(frame, Image.new('RGB', (240, 240), '#30343b'), mask).resize((480, 480)) for frame in frames[:-1]]
        previews[0].save(output / 'face-preview.png')
        previews[0].save(output / 'face-preview.gif', save_all=True, append_images=previews[1:], duration=50, loop=0)
    print('PASS: welcome timing, stop/resume, GPIO ownership, fault recovery, colors, gaze, blink, wink, expressions, all packed rows, dirty regions, idle frames and timer wrap')
    print(f'12-second cycle: {total_pixels:,} transmitted pixels; {total_pixels*2/12:,.0f} pixel bytes/second average (excluding SPI commands)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='servo-face-test-') as tmp:
        tmp = Path(tmp)
        for name, content in STUBS.items():
            (tmp / name).write_text(content)
        elf = tmp / 'display.elf'
        subprocess.run([args.cc, '-mcpu=cortex-m3', '-mthumb', '-O1', '-g', '-ffreestanding', '-nostdlib',
                        '-I' + str(tmp), '-I' + str(APP), '-I' + str(ROOT / 'Utilities/Fonts'),
                        str(APP / 'display/display.c'), str(APP / 'display/face_animation.c'),
                        str(APP / 'display/face_assets.c'), str(tmp / 'stubs.c'),
                        '-Wl,-Ttext=0x10000,-Tdata=0x20000000,-e,Display_Task', '-lgcc', '-o', str(elf)], check=True)
        verify(elf, args.output)


if __name__ == '__main__':
    main()
