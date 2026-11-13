# FW Application Core Agent Notes

This is the main hand-written firmware area. Most BLE command, servo, PCA9685, shared-I2C, and TOF behavior belongs here.

## Module Map

- `config.h`: product-level feature flags, BLE advertising name, watchdog bits, common task timing, and TOF encoding limits.
- `display/`: GC9A01 driver, one-shot NatiLab startup and bounded synchronous BLE rectangle writes. No playback or framebuffer. The display mutex protects startup, remote writes and diagnostic GPIO. See display/README.md for packet and lifetime contract.
- `board_io.*`: allowlisted GPIO diagnostics, latest DMA ADC sample and TOF interrupt counting; see root `BOARD_IO.md` for the board pin map.
- `board_io` also owns the MPQ/PWM connection gate. All PCA init/retry/diagnostic paths must use `BoardIO_SetPwmReady`; never write OE directly and bypass the BLE disconnect state.
- `adc_dma.*` / `adc_filter.h`: TIM2-triggered 1 kHz sampling per input, circular GPDMA channel 6, paired settling ranks, and amplitude-weighted RAW window means. No task may reconfigure ADC4 while this sampler runs.
- `adc_ble.*`: optional READ/NOTIFY service for battery and both current-sense RAW counts. defaultTask publishes filtered DMA windows at approximately 5 Hz while subscribed.
- `comm/`: text command parser and NUS queue integration. It owns command names, argument parsing, response strings, and routing into subsystem APIs.
- `servo/`: high-level 16-channel servo state, immediate moves, and timed linear interpolation.
- `PCA9685/`: low-level PWM/servo pulse driver for the PCA9685 on I2C3.
- `i2c_os/`: mutex-protected HAL I2C wrappers used by both PCA9685 and VL53L5CX.
- `VL53L5CX/`: TOF sensor task, latest-frame runtime state, 64-byte BLE payload encoding, and the ST sensor support files.
- `leds/`: small board/support helpers.

## Command Flow

BLE command path:

1. `STM32_WPAN/App/nus.c` receives a NUS RX write.
2. `NUS_APP_EnqueueRX()` in `Core/Src/app_freertos.c` copies it into `commRxQueueHandle`.
3. `comm_Task()` drains the RX queue and calls `NusRxTask()`.
4. `NusRxTask()` calls `parser_ProcessOne()`.
5. `comm_interface.c` validates the text command and calls the target module.
6. Replies are enqueued through `NUS_APP_EnqueueTX()`.

Currently important commands:

- `HELP` (all command syntax and legacy aliases, delivered as short NUS lines)
- `PING`
- `GPIO_GET <signal_or_pin>`, `GPIO_SET <signal_or_pin> <0|1>`, `ADC_READ <signal_or_pin>`
- `SET_DIRECT <channel> <pulse_us>`
- `SET_TIMING <channel> <pulse_us> <duration_ms>`
- `TOF_SET_MIN_MAX <min_mm> <max_mm>`

Legacy aliases are still accepted for the older servo commands. Preserve aliases unless a task explicitly removes backward compatibility.

## Servo Notes

- `servo_task.c` is the correct place for motion behavior and channel state.
- `pca9685.c` is the correct place for hardware PWM calculations and register behavior.
- Servo channels are `0..15`.
- Servo pulses are clamped through the PCA9685 limits: `500..2500` microseconds.
- Only servoTask writes normal PCA PWM outputs. Command producers change state and wake it; never write an old I2C snapshot back into command state.
- servoTask has High2 priority, command RX High1, BLE dispatch High3. Sensors/display remain lower and the radio link layer remains above application tasks.
- Shared `servos_controller` state is volatile and protected with critical sections around multi-field updates.
- A direct move cancels the channel's active timed move.

## TOF Notes

- `vl53l5cx_task.c` lazily initializes the sensor only while TOF notifications are enabled.
- The task stops ranging when notifications are disabled.
- The BLE frame is always 64 bytes, one byte per 8 x 8 zone.
- `VL53L5CX_BLE_DISTANCE_INVALID` (`0xff`) is reserved for invalid/no-target zones.
- Valid distances are scaled into `0x00..0xfe` using the runtime min/max range.
- `g_vl53l5cx_runtime.distance_mm` stores raw millimeter values, while `ble_payload` stores the encoded BLE bytes.
- Most `vl53l5cx*.c/.h` files are vendor/support code. Prefer editing `vl53l5cx_task.*` unless the task explicitly requires driver integration.

## I2C Notes

- PCA9685 and VL53L5CX share I2C3.
- Use `i2c_readOS`, `i2c_writeOS`, and `i2c_isDeviceReadyOS` so transfers hold `i2cMutexHandle`.
- Do not add direct `HAL_I2C_*` calls in servo or TOF modules unless you also handle shared-bus locking.

## Change Checklist

When adding a command, update:

- `comm/comm_interface.h`
- `comm/comm_interface.c`
- `comm/commands.md` if the public protocol changes
- the owning module API if new behavior is needed
- the Web command provider and UI if the command is user-facing

When changing TOF payload semantics, update:

- `VL53L5CX/vl53l5cx_task.*`
- `STM32_WPAN/App/tof*` if packet shape or characteristic behavior changes
- Web `src/ble/BleProvider.tsx`
- Web TOF UI/workflow consumers that decode or resize the map

## Saved safe servo pose

`servo/safe_pose_format.h` defines the type-4 file in the existing OTA file store.
Storage caches the newest valid committed pose in RAM for callback-safe access.
`SAFE_POSE` and BLE disconnect cancel all motion and queue this pose atomically.
Only servoTask enables `BoardIO_SetSafePoseActive` after writing the full frame;
this is the deliberate exception to disconnected PWM/power gating. Preserve the
safe-pose generation check against stale I2C completion. Web saves a new file
before deleting previous copies. OTA/format remove the persistent pose.
