# Dependency baseline

Checked against official upstream Git repositories on 2026-11-04. Exact commits
and source paths are in `dependencies.lock.json`; dependencies are vendored so a
checkout can build without downloading packages.

| Component | Baseline |
| --- | --- |
| STM32CubeWBA / boot manager | 1.10.0 plus GitHub fixes, commit `4ccfd1526870263657ec8e0854c64cda6ed2b5da` |
| WBA HAL | CubeWBA-selected October 2026 fixes, `3f3458ef245ecb1ae98485f6fa899086da95d076` |
| CMSIS device / BSP / WPAN BLE + Link Layer | Exact revisions selected by the above CubeWBA commit |
| FreeRTOS + CMSIS adapter | ST 11.3.1_20261016, upgraded from 11.2.0 |
| CMSIS-RTOS2 API header | Arm API 2.3.0, required by the new FreeRTOS adapter |
| VL53L5CX API | ST X-CUBE-TOF1 API 2.0.0, upgraded from 1.3.9 |

The CubeWBA release itself pins FreeRTOS 10.6.2. This project deliberately uses
ST's newer 11.3.1 branch; it is a separately integrated and tested combination,
not a claim that ST qualifies that exact combination. The remaining CubeWBA
components follow the parent repository's selected revisions, rather than
independently taking every component's development head.

## Integration changes

- Link `LinkLayer_BLE_Basic_V3_0.a` and `stm32wba_ble_stack_full.a`.
- Port the upstream System / WPAN target changes while preserving the project's
  FreeRTOS scheduling, RNG handling, timer allocation and application services.
- Set `CFG_USE_SEQUENCER` to 0 for the FreeRTOS integration.
- Initialize the selected Cortex-M33 port's interrupt-priority validation in
  `MX_FREERTOS_Init` before WPAN enables interrupts. A radio ISR can post a
  semaphore before scheduler start; leaving the validation state uninitialized
  caused an intermittent startup assertion. Assertions remain enabled.
- Use the updated SNVMA host buffer identifier and configuration.
- Keep only the GCC Cortex-M33 non-secure FreeRTOS port (plus memory managers),
  as in the previous repository layout; unrelated architecture ports must not
  be compiled by CubeIDE.
- Keep the local VL53L5CX platform callbacks, RTOS-aware delays and selected
  output fields; update callback names to the API 2.0.0 prefix.
- CubeIDE boot-manager links resolve to this repository's shared drivers.

## CubeMX regeneration

The `.ioc` package baseline is now WBA 1.10.0. Its X-CUBE-FREERTOS 1.4.0
metadata still describes the original generated 11.2.0 integration. Do not
blindly regenerate and overwrite the vendored 11.3.1 sources: review the diff,
retain the versions in the lock file, then run the build and validation tools.
Generated application startup still needs the project's custom servo task and
I2C-disable guards after regeneration.

## Validation scope

Build both images with `tools/build.ps1`. Validate binary layout and disabled
I2C paths using `tools/validate_firmware.py --nm <path-to-arm-none-eabi-nm.exe>`.
The application is also compiled with each of the four device-switch combinations.
Hardware smoke testing covers a normal reset through the boot manager, BLE
advertising, NUS PING, disabled-device replies and advertising after disconnect.
Actual sensor ranging/PWM and a complete OTA download/install cycle require
separate hardware tests; a successful build is not proof of those behaviors.

## Licenses

The root MIT license applies to original application code. ST/Arm/FreeRTOS
components retain their own notices and licenses. The boot manager has its own
ST SLA license. Do not replace vendor licenses with the root project license.
Whitespace inherited from upstream sources is preserved where possible.
