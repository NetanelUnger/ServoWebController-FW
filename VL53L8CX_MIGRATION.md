# VL53L8CX integration — 2026-11-05

The fitted VL53L8CX replaces the old software assumption of VL53L5CX.
The actual sensor was identified at 7-bit I2C address 0x29 (HAL 0x52),
device ID 0xF0, revision 0x0C, and now produces real ranging frames.

## Source and local integration

Official ST source: https://github.com/STMicroelectronics/x-cube-tof1
Pinned revision: `c12709c3ac802d91dd2a215178f53cb94c024709` (main at import).
Component: `Drivers/BSP/Components/vl53l8cx`, ULD API `VL53L8CX_2.0.0`.
The BSP, ULD API, sensor firmware, detection/xtalk dependencies, license and
release notes are included under `STM32CubeIDE/Application/User/Core/VL53L8CX`.
Upstream modules/porting files are flattened into that directory.

Local porting changes: RTOS delays yield, multi-byte transfers split into
256-byte chunks using the existing shared I2C mutex/interrupt transport,
and buffer byte swaps cast to unsigned before left shifts. Unused SPAD count,
sigma, reflectance and motion outputs are disabled. One target per zone is used.
The task retains the previous distance encoding and BLE service interface.
The old driver directory is retained as historical source, excluded from Debug
and not linked into the firmware. Active task/config/command names use VL53L8CX.

## Operation

- `DISABLE_I2C_VL53L8CX=0` enables the worker; `1` blocks its task and transport.
- Lazy initialization on BLE notification subscription uploads the sensor's
  approximately 84 KiB firmware. First subscription therefore takes several
  seconds on the 100 kHz bus; later subscriptions reuse initialized firmware.
- 8x8 autonomous ranging, 5 Hz, 20 ms integration setting.
- Unsubscribe/disconnect stops ranging. No IMU sampling was enabled.
- Distance characteristic: `91bad493-b950-4226-aa2b-4ede9fa42f59`.
- Payload: 64 bytes, one per zone; 0..254 maps to configured min/max distance,
  255 means invalid/no target. Default range is 0..500 mm.
- `TOF_SET_MIN_MAX 0 4000` exposes the full distance range at lower resolution.
- `g_vl53l8cx_runtime` exposes raw millimeters, state, frame/fault counters,
  last status and initialization attempts for SWD inspection.
- `g_vl53l8cx_force_ranging` is a RAM-only bring-up override, zero at reset.
  Normal operation and the BLE validation do not use it.
- PCA diagnostic continues to hold PWM_CH1 neutral; normal servo commands
  remain disabled. TOF identification reads in that diagnostic are skipped
  when the real TOF driver is enabled, preventing register-bank interference.

## Validation and memory

Clean Debug build and flash verification passed. Four existing warnings remain
in ST low-power/BLE sources; no new-driver build warnings.
Debug now uses `-Os` with debugging symbols so the sensor firmware and all
existing face assets fit without changing the OTA partition layout.
Application binary: 478784 bytes; active OTA slot: 499712 bytes;
remaining: 20928 bytes (20.44 KiB). ELF BSS is 113696 bytes including static
RTOS stacks/heap; this is not a measurement of runtime stack high-water marks.

`tools/check_tof_ble.py --address 00:80:E1:2A:26:24` passed two subscription
cycles, 12 received 64-byte frames each, valid distance data, scaling commands
and NUS PING. Raw SWD state after disconnect: initialized=1, ranging_active=0,
26 acquired frames, faults=0, last_status=0, init_attempts=1. Most zones measured
approximately 1.85 m in the available scene; this is not a calibrated accuracy
test. PCA remained stage=4, verified=74, last_error=0 on the shared bus.
The complete BLE test passed again after an MCU reset (including firmware
re-initialization without cycling the sensor power).

Debugger halt/resume during early inspection left the tickless runtime without
timer progress; a reset restored operation. Subsequent end-to-end BLE testing
was performed without halting the CPU. Reset after invasive SWD inspection.
Layout, OTA tags, enabled-driver and obsolete-driver symbol checks pass via
`tools/validate_firmware.py`. The machine-readable BLE report is generated at
`STM32CubeIDE/Debug/tof-ble-validation.json` (build artifact).
