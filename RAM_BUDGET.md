# RAM budget and hardware validation

Measured on NL802915 / STM32WBA5MMGH, 2026-11-14. Baseline is main `eea53fd`
(including the Cortex-M33 FPU context fix).
Evidence, backups and the exact baseline/optimized ELF, HEX and map files are in
`artifacts/ram_optimization/`; `sha256.json` identifies the binaries.

## Linked memory

| Budget (bytes) | Baseline | Optimized | Saved |
| --- | ---: | ---: | ---: |
| GNU size `.data` | 1,844 | 1,828 | 16 |
| GNU size BSS including reserved MSP | 127,512 | 106,392 | 21,120 |
| Total static RAM plus MSP reservation | 129,356 | 108,220 | **21,136** |
| Space from `_end` to the reserved MSP | 1,672 | 22,808 | 21,136 |
| Firmware binary | 353,976 | 334,480 | 19,496 |

The reclaimed space is **not** automatically added to the FreeRTOS heap.
`configTOTAL_HEAP_SIZE` stays 32,000 bytes; its measured minimum free space is
3,376 bytes on the baseline. Newlib `malloc` uses the separate `_sbrk` region
between `_end` and `_estack - _Min_Stack_Size`. BLE event nodes and BLE timers
use that region, so it is not wholly unused RAM. `_Min_Heap_Size == 0` means
no minimum linker reservation, not that malloc is disabled. The MSP reservation
remains 4,096 bytes. A future runtime needs an explicit budget within the freed
space, leaving headroom for these existing libc allocations.

## Task budgets

Application depths are centralized in `Core/Inc/app_freertos.h`, in **32-bit
words**. `app_freertos.c` passes `sizeof(buffer)` to CMSIS in **bytes**. These
application tasks are hand maintained; this IOC has no generated task-list
entries. Kernel idle/timer depths and overflow checking are synchronized in
`FreeRTOSConfig.h` and the IOC.

| Task | Old bytes | New bytes | Baseline touched bytes observed |
| --- | ---: | ---: | ---: |
| IDLE | 4,112 | 1,024 | 140 |
| Tmr Svc | 4,112 | 1,024 | 120 |
| defaultTask | 6,144 | 3,072 | 504 |
| commTask | 8,192 | 4,096 | 1,872 |
| servoTask | 4,096 | 2,048 | 448 |
| bleTxTask | 4,096 | 3,072 | 1,276 |
| vl53l8cxTask | 8,192 | 4,096 | 1,052 |
| displayTask | 2,048 | 2,048 | 1,252 |

Stack reductions save 20,512 bytes. ST's dynamic tasks and the HCI dispatcher
retain their original budgets, including the 1,536-byte Flash Manager stack:
its observed use rose from 152 bytes at idle to 1,032 bytes during file writes.
This is why an idle-only measurement is insufficient. FPU support remains on.
Stack overflow checking is strengthened from mode 1 to mode 2 (also checks the
stack boundary fill pattern at context switches).

Watermarks show touched memory, not a mathematical worst-case bound. Unwritten
local arrays, untested failure paths and compiler changes can invalidate a
watermark-only estimate. GCC `.su` reports and the application call paths were
also inspected; substantial headroom remains. Repeat hardware measurements
after adding callbacks, log formatting, local arrays, recursion or timer work.

## BLE and queues

Both CubeIDE configurations now use `BLE_STACK_PERIPHERAL_ONLY`, the matching
`ble_peripheral_only` headers and `LinkLayer_BLE_Peripheral_Only_V3_0.a`.
This matches the IOC's existing `BLE_STACK_TYPE_PO` selection. The host archive
continues to be named `stm32wba_ble_stack_full.a`: this ST version selects the
host feature set at compile time from that shared archive.

This removes unused Central/scanning/GATT-client capability. The product's
Peripheral/GATT-server role, security support, eight-link configuration, MTU
251, all application services and BLE buffer sizes remain. The change saves
another 624 bytes of linked RAM and about 19.5 KB of flash. A future feature
that makes the board a BLE Central/GATT client must restore the Basic selection.
See ST's bundled `Middlewares/ST/STM32_WPAN/ble/stack/doc/STM32WBA_BLE_Stack_User_Manual.html`
and [ST's stack integration guide](https://wiki.st.com/stm32mcu/wiki/Connectivity%3ASTM32WBA_BLE_Stack_Integration).

RX/TX queues remain five copied items each; the timer queue remains ten entries.
Reducing their sizes would reduce burst tolerance. The 4,096-byte logging FIFO,
BLE packet pools, service database, number of links and dynamic heap were not
reduced. Low observed use by one central does not justify reducing multi-peer
or burst capacity.

## Verification and reproduction

Only the application HEX was flashed, with CubeProgrammer verification. The
bootloader was not rewritten and no mass erase was used. A full original Flash
backup is `original_flash.bin`. Existing image files 6 and 7 have separate
byte-for-byte backups under `baseline_storage/`.

`tools/check_ram_snapshot.py` reads a matching ELF and 128-KiB RAM dump. It uses
GNU nm and offline GDB type information, walks all FreeRTOS task lists, checks
the task count and reports stack fill, FreeRTOS heap free/minimum and libc's
`sbrk` commitment. It never calls functions on the target.

Capture **after** BLE testing/disconnect, not during a radio soak:

```powershell
& $programmer -c port=SWD mode=HOTPLUG -u 0x20000000 0x20000 snapshot.bin
python tools/check_ram_snapshot.py --elf firmware.elf --ram snapshot.bin --toolchain $toolchain --output snapshot.json
```

Debug access can disturb the radio. An early baseline run was interrupted near
an SWD capture and is excluded from uninterrupted soak evidence. Reset after
debug sampling before doing further radio tests.

Windows retained the physical ACL connection after ordinary Bleak disposal in
baseline tests: a direct WinRT reconnect still answered PING, while fresh
advertising was absent. Do not confuse local `disconnect()` with wire-level
link loss. `tools/ble_radio_cycle.py --run` deliberately disables Bluetooth for
six seconds and restores its original state in `finally`; it interrupts **all**
local Bluetooth links. The transport test's explicit `--cycle-radio` option
uses this to test actual disconnect/re-advertising/reconnect without an MCU
reset. Wi-Fi is not changed.

`tools/check_file_store_hardware.py --run --artifacts <new-directory>` backs up
existing live files, exercises image upload/read/show, a maximum 4,096-event
timeline and a temporary safe pose, then deletes only test IDs and re-verifies
existing contents. It never formats the store. Catalog generations/next IDs
advance as a normal consequence of these temporary file transactions.

Host checks passed: 59 BLE transport scenarios, eight-link policy/retry tests,
timeline and safe-pose scheduling, file-store recovery (27 power-cut checkpoints),
boot/OTA recovery (372 cut boundaries), and firmware slot/symbol validation.
The flashed and measured configuration is CubeIDE Debug (`-Os`, hard-float).
Both project configurations keep `-Os` and the matching Peripheral Only library
selection. Full OTA upload was not performed on the physical board because it
erases the file-storage region. The retained default-task budget also accounts
for the inspected OTA processing/Flash call path and GCC stack-use reports.
The optimized build completed with no errors and the same three existing
warnings (two unused low-power variables and the advertising const qualifier).

### Optimized hardware results

All tests below used the same flashed Debug image. The final pre-reset RAM
capture reports 1,591,924 RTOS ticks (about 26.5 minutes on that boot).

| Test | Result / evidence in `artifacts/ram_optimization/` |
| --- | --- |
| Servo + sensors | 40 atomic 16-channel writes; actual PCA registers on all 16 channels; timed interpolation/cancellation; `optimized_servo.log` |
| IMU | RAW/SFLP/OFF transitions, quaternion/gravity checks and invalid mode rejection; `optimized_imu.log` |
| TOF | 64-zone data, scaling, stop/re-subscribe, valid optical returns; `optimized_tof.json` |
| ADC | 25 valid frames at 4.94 Hz, READ and NUS coexistence, stop/restart; `optimized_adc.log` |
| Display | 966 acknowledged writes, full 240x240 raw/fill images at MTU 251, invalid rectangle/row rejection; `optimized_display.json` |
| File store | RGB332 readback/show, 4,096-event timeline upload/read/validate/play/stop, safe pose upload/read/apply; original files preserved; `optimized_storage/report.json` |
| Combined depth mode | TOF depth display alongside IMU SFLP/ADC, TOF OFF/ON, VERSION CRC/readout and command rejection paths; `depth.json` |
| BLE soak | **900.29 seconds**, 633 cycles, **6,772 accepted commands**, 40,155 telemetry notifications during the repeat phase; `optimized_ble.json` |
| Link-loss recovery | Radio loss, fresh advertising, reconnect, no old NUS replies or continuing timed motion; cleanup/restoration verified in the same soak report |
| Flash readback | Exact optimized app binary, unchanged bootloader and original image payloads; `readback_verification.json` |

All measured transport rejection, timeout and error counters stayed zero;
FreeRTOS heap free/minimum remained **3,376 bytes** throughout. The median NUS
write was 30.05 ms versus 29.99 ms in the baseline; mean repeat cycle was
1.422 s in both runs. Telemetry merging is intentional latest-value behavior,
not a dropped command reply. The UART capture contains no allocation-failure
or stack-overflow markers (it includes normal unhandled vendor-event logging).

Final stack watermarks from `optimized_final_ram.json`:

| Task | Allocated bytes | Touched bytes | Untouched bytes |
| --- | ---: | ---: | ---: |
| IDLE | 1,024 | 140 | 884 |
| Tmr Svc | 1,024 | 120 | 904 |
| defaultTask | 3,072 | 512 | 2,560 |
| commTask | 4,096 | 1,872 | 2,224 |
| servoTask | 2,048 | 448 | 1,600 |
| bleTxTask | 3,072 | 1,276 | 1,796 |
| vl53l8cxTask | 4,096 | 1,052 | 3,044 |
| displayTask | 2,048 | 1,260 | 788 |
| HCI async Event | 2,048 | 868 | 1,180 |
| BLE Host Task | 2,048 | 1,136 | 912 |
| Link Layer Task | 3,072 | 1,080 | 1,992 |
| Flash Manager | 1,536 | 1,008 | 528 |
| BLE Timer | 512 | 152 | 360 |
| PKA | 512 | 152 | 360 |
| RNG | 512 | 192 | 320 |

The smallest measured remaining margin among the **reduced** stacks is 884
bytes. Libc's region is 22,808 bytes, of which `_sbrk` had committed 404 bytes;
22,404 bytes were still uncommitted. The committed region can itself contain
freed malloc blocks, so this is not an exact active-allocation measurement.

These tests verify protocol behavior and PWM-register output, not observed
mechanical movement or visual panel quality. RF load used one Windows central;
eight simultaneous physical centrals and pairing were not exercised. Their
configuration/buffers and ST task stack budgets were retained. Watermarks and
soak tests do not prove all possible future workloads safe.

### Separate existing parser finding

Supplemental argument-boundary tests observed `SET_TIMING 0 1500 4294967296`
and `SET_TIMING 0 1500 -1` acknowledged with duration `4294967295`. The unchanged
`comm_ParseUint` uses 32-bit `strtoul` without checking its sign or `ERANGE`.
These cases are **not** counted as passing invalid-argument rejection tests;
the accepted motion was stopped. Parser behavior was not changed as part of
the RAM optimization. The supplemental regression uses a nonnumeric argument,
invalid channel/mode and a long PING argument to test existing rejection paths.
