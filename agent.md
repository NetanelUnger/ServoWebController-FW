# agent.md

This file is a codebase map for contributors, maintainers, and automation tools working on BleServoController.

It explains where the important code lives, what each area is responsible for, and which files are safer to edit versus generated or vendor-owned areas.

## Project Intent

BleServoController is an educational BLE robotics firmware project.

Its core goals are:

- BLE-based control of servo motors
- Runtime control over motion timing
- Streaming 8x8 time-of-flight data over BLE
- A simple architecture that students can read and extend
- A foundation for sensor-driven robotics experiments

## Mental Model

The firmware has three main application concerns:

1. BLE communication and command parsing
2. Servo control through the PCA9685
3. TOF sensing through the VL53L5CX

Those concerns are connected through FreeRTOS tasks, shared runtime state, and BLE services.

## Best Entry Points

If you need to understand the project quickly, read these files first:

- `README.md`
- `BleServoController.ioc`
- `STM32CubeIDE/Application/User/Core/config.h`
- `Core/Src/app_freertos.c`
- `Core/Src/app_entry.c`
- `STM32CubeIDE/Application/User/Core/comm/comm_interface.c`
- `STM32CubeIDE/Application/User/Core/servo/servo_task.c`
- `STM32CubeIDE/Application/User/Core/VL53L5CX/vl53l5cx_task.c`
- `STM32_WPAN/App/app_ble.c`
- `STM32_WPAN/App/nus_app.c`
- `STM32_WPAN/App/tof_app.c`

## Directory Guide

### Root

- `BleServoController.ioc`
  CubeMX project configuration and the main source of truth for generated STM32 setup.

- `README.md`
  Public-facing project description.

- `agent.md`
  This developer and automation guide.

### `Core/`

Mostly STM32Cube-generated startup and integration files.

Important files:

- `Core/Src/main.c`
  MCU boot path, peripheral initialization, and startup flow.

- `Core/Src/app_entry.c`
  BLE stack and system integration. This is where serial commands, trace hooks, and middleware initialization are coordinated.

- `Core/Src/app_freertos.c`
  Creates RTOS tasks, queues, mutexes, and event flags, and starts the app-level workers.

### `STM32CubeIDE/Application/User/Core/`

This is the main custom application area.

#### `config.h`

Location:

- `STM32CubeIDE/Application/User/Core/config.h`

Purpose:

- Feature toggles
- BLE advertising/device name
- Watchdog flag layout
- TOF scaling defaults and limits
- Common timing constants used across tasks

Change this file when you need to adjust product-level behavior.

#### `comm/`

Location:

- `STM32CubeIDE/Application/User/Core/comm/`

Purpose:

- Parses BLE text commands
- Validates command arguments
- Routes commands to the servo or TOF layers
- Formats textual responses

Important files:

- `comm_interface.h`
- `comm_interface.c`

Typical edits here:

- Add a new BLE command
- Change command syntax
- Add validation or response formatting

#### `servo/`

Location:

- `STM32CubeIDE/Application/User/Core/servo/`

Purpose:

- High-level servo control logic
- Current servo state tracking
- Timed move interpolation
- Calls into the PCA9685 driver

Important files:

- `servo_task.h`
- `servo_task.c`

Typical edits here:

- Add new movement behavior
- Change interpolation behavior
- Add safety rules or per-channel policies

#### `PCA9685/`

Location:

- `STM32CubeIDE/Application/User/Core/PCA9685/`

Purpose:

- Low-level servo PWM output driver
- Maps pulse widths to PCA9685 register values

Important files:

- `pca9685.h`
- `pca9685.c`

Typical edits here:

- Hardware-specific PWM behavior
- Servo pulse range rules
- Register-level improvements

#### `i2c_os/`

Location:

- `STM32CubeIDE/Application/User/Core/i2c_os/`

Purpose:

- Wraps HAL I2C calls in RTOS-safe access patterns
- Protects the shared I2C bus with a mutex

Important files:

- `i2c_os.h`
- `i2c_os.c`

This module is shared by both the servo and TOF subsystems.

#### `VL53L5CX/`

Location:

- `STM32CubeIDE/Application/User/Core/VL53L5CX/`

Purpose:

- TOF sensor task logic
- TOF frame acquisition
- BLE payload encoding for the 8x8 distance map
- Vendor integration files for the VL53L5CX sensor

Important project-specific file:

- `vl53l5cx_task.c`
- `vl53l5cx_task.h`

Important vendor or support files:

- `platform.c`
- `vl53l5cx*.c`
- `vl53l5cx*.h`

Typical edits here:

- Change TOF polling behavior
- Adjust BLE encoding
- Add frame filtering
- Add more sensor diagnostics

### `STM32_WPAN/App/`

Purpose:

- BLE services and STM32 WPAN application glue

Important files:

- `app_ble.c`
  Main BLE stack application setup and advertising behavior.

- `nus.c`, `nus_app.c`
  Nordic UART Service transport used for command and response traffic.

- `tof.c`, `tof_app.c`
  Custom BLE service for streaming TOF distance-map data.

- `dis.c`, `dis_app.c`
  Device Information Service.

- `ota.c`, `ota_app.c`
  OTA-related BLE service support.

Typical edits here:

- Change advertising behavior
- Change or extend BLE services
- Add a new characteristic
- Update how notifications are sent

### `System/`, `Utilities/`, `Drivers/`, `Middlewares/`, `Projects/Common/`

Purpose:

- STM32 platform support
- HAL drivers
- FreeRTOS and ST middleware
- ST-provided support code

Edit these only when necessary.

In most cases, application changes should stay in:

- `STM32CubeIDE/Application/User/Core/`
- `STM32_WPAN/App/`
- `Core/Src/app_entry.c`
- `Core/Src/app_freertos.c`
- `STM32CubeIDE/Application/User/Core/config.h`

## Main Runtime Flows

### BLE Command to Servo Motion

Path:

1. BLE client writes text to NUS
2. NUS app code forwards data into the communication queue
3. `comm_interface.c` parses the command
4. `servo_task.c` updates the runtime state
5. `pca9685.c` writes the PWM output

Main files:

- `STM32_WPAN/App/nus_app.c`
- `STM32CubeIDE/Application/User/Core/comm/comm_interface.c`
- `STM32CubeIDE/Application/User/Core/servo/servo_task.c`
- `STM32CubeIDE/Application/User/Core/PCA9685/pca9685.c`

### TOF Sensor to BLE Notification

Path:

1. `vl53l5cx_task.c` polls the sensor
2. Raw mm distances are stored in runtime state
3. The frame is encoded into a BLE-friendly 8-bit map
4. `tof_app.c` sends the notification over BLE

Main files:

- `STM32CubeIDE/Application/User/Core/VL53L5CX/vl53l5cx_task.c`
- `STM32_WPAN/App/tof_app.c`
- `STM32_WPAN/App/tof.c`

### System Startup

Path:

1. `main.c` initializes MCU peripherals
2. `app_entry.c` initializes BLE and middleware
3. `app_freertos.c` creates tasks and queues
4. Servo, communication, and TOF tasks begin running

Main files:

- `Core/Src/main.c`
- `Core/Src/app_entry.c`
- `Core/Src/app_freertos.c`

## Safe Editing Guidance

Usually safe to edit:

- `STM32CubeIDE/Application/User/Core/config.h`
- `STM32CubeIDE/Application/User/Core/comm/*`
- `STM32CubeIDE/Application/User/Core/servo/*`
- `STM32CubeIDE/Application/User/Core/PCA9685/*`
- `STM32CubeIDE/Application/User/Core/i2c_os/*`
- `STM32CubeIDE/Application/User/Core/VL53L5CX/vl53l5cx_task.*`
- `STM32_WPAN/App/tof*`
- `STM32_WPAN/App/nus_app.*`
- `Core/Src/app_entry.c`
- `Core/Src/app_freertos.c`

Use caution when editing:

- `Core/Src/main.c`
- `STM32_WPAN/App/app_ble.c`
- `BleServoController.ioc`

These files are tightly coupled to STM32CubeMX or generated project structure.

Avoid casual edits in:

- `Drivers/`
- `Middlewares/`
- most of `Projects/Common/`
- vendor VL53L5CX driver files unless you know the upstream implications

## CubeMX and Generated Code Notes

The `.ioc` file is the configuration source for generated project content.

That means:

- Some project files may be regenerated
- Manual edits in generated sections can be lost
- FreeRTOS object definitions in generated files should stay consistent with the `.ioc`

If you add or remove tasks, queues, or middleware-level objects, update both:

- the generated code areas that compile today
- `BleServoController.ioc`

## Build Artifacts

Current generated build outputs are under:

- `STM32CubeIDE/Debug/`

These files are build products, not the long-term source of truth.

If a build file and the `.ioc` disagree, prefer treating the `.ioc` and the hand-maintained source files as the real design intent.

## Practical Extension Examples

If you want to add a new BLE command:

- Start in `comm_interface.c`
- Route the command to the correct subsystem
- Keep parsing, validation, and response formatting local there

If you want to add a new motion mode:

- Start in `servo_task.c`
- Only touch `pca9685.c` if you need lower-level output changes

If you want to change the TOF BLE payload semantics:

- Start in `vl53l5cx_task.c`
- Update the custom BLE service layer only if packet shape changes

If you want to add a new BLE characteristic:

- Start in `STM32_WPAN/App/`
- Expect to touch `app_ble.c` and the relevant service files

## Licensing Reality

This repository contains both original project code and third-party/vendor code.

Before publishing changes widely:

- keep existing upstream license files
- do not remove vendor notices
- add a root project license for your own original application code if needed

## Short Summary

If you only remember one thing, remember this:

- `comm/` handles commands
- `servo/` handles motion
- `PCA9685/` drives outputs
- `VL53L5CX/` handles sensing
- `STM32_WPAN/App/` exposes BLE services
- `app_entry.c` and `app_freertos.c` glue the whole system together
