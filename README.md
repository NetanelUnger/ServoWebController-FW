# BleServoController

BleServoController is an educational open-source firmware project for a BLE-controlled servo and time-of-flight sensor kit built around the STM32WBA5M platform.

The project is designed as a hands-on learning kit for children, students, makers, and educators who want to explore sensors, motors, embedded systems, BLE, AI-assisted experimentation, and entry-level to advanced robotics. The learning path can start with very simple motion experiments and grow into full sensor-driven behavior and robotics projects. Young children should use the kit with adult supervision.

## What This Firmware Does

- Drives up to 16 servo outputs through a PCA9685 PWM controller
- Accepts BLE text commands over Nordic UART Service (NUS)
- Supports immediate servo moves and timed servo interpolation
- Streams 8x8 VL53L5CX time-of-flight distance data over BLE
- Lets the BLE client re-scale the TOF BLE payload range at runtime
- Runs on FreeRTOS with separate tasks for communication, servo control, and TOF acquisition

## Main Use Cases

- First experiments with motors and motion
- Learning how distance sensors work
- Building reactive behavior such as obstacle avoidance or gesture response
- Teaching BLE command/response protocols
- Building robotics demos that combine actuation and sensing

## Hardware Target

- STM32WBA5M based board
- Project currently configured for the B-WBA5M-WPAN discovery board
- PCA9685 servo driver on I2C
- VL53L5CX time-of-flight sensor on I2C
- Standard hobby servos

## BLE Command Interface

The firmware exposes a simple text command protocol over BLE NUS.

Supported commands include:

- `PING`
- `SET_DIRECT <channel> <pulse_us>`
- `SET_TIMING <channel> <pulse_us> <duration_ms>`
- `TOF_SET_MIN_MAX <min_mm> <max_mm>`

Examples:

```text
PING
SET_DIRECT 0 1500
SET_TIMING 3 2200 1000
TOF_SET_MIN_MAX 200 1200
```

## Repository Layout

Project-specific application logic is mainly in these locations:

- `STM32CubeIDE/Application/User/Core/config.h`
  Global project configuration, feature flags, BLE device name, watchdog flags, and TOF scaling defaults.

- `STM32CubeIDE/Application/User/Core/comm/`
  BLE command parsing and transport glue for the text command protocol.

- `STM32CubeIDE/Application/User/Core/servo/`
  High-level servo runtime state and timed motion interpolation.

- `STM32CubeIDE/Application/User/Core/PCA9685/`
  Low-level PCA9685 PWM and servo pulse driver.

- `STM32CubeIDE/Application/User/Core/i2c_os/`
  RTOS-safe I2C helpers used by the servo and TOF drivers.

- `STM32CubeIDE/Application/User/Core/VL53L5CX/`
  VL53L5CX sensor integration, vendor driver files, and BLE payload encoding for the 8x8 distance map.

- `STM32_WPAN/App/`
  BLE stack application layer, including NUS, DIS, OTA, and the custom TOF service.

- `Core/Src/app_freertos.c`
  FreeRTOS object creation and task startup.

- `Core/Src/app_entry.c`
  BLE stack initialization, trace hooks, board integration, and serial command dispatch.

- `Core/Src/main.c`
  MCU startup and peripheral initialization generated around the application code.

Platform and vendor content lives mainly in these directories:

- `Core/`
- `Drivers/`
- `Middlewares/`
- `Projects/Common/`
- `System/`
- `Utilities/`

## Build Options

This repository contains project files for:

- STM32CubeIDE

The main CubeMX configuration file is:

- `BleServoController.ioc`

The current STM32CubeIDE build output is generated under:

- `STM32CubeIDE/Debug/`

## Getting Started

1. Open the project in STM32CubeIDE or your preferred supported toolchain.
2. Build the firmware.
3. Flash the board.
4. Connect from a BLE client that can talk to Nordic UART Service.
5. Send text commands to control servos or configure TOF BLE scaling.

## Educational Positioning

This project is meant to be understandable at multiple levels:

- Beginners can start by sending simple servo commands.
- Intermediate learners can explore task-based firmware structure and BLE transport.
- Advanced students can work on sensor fusion, autonomy, control loops, robotics behavior, and AI-assisted experimentation on top of the existing firmware.

## Open Source Notes

This repository combines custom application code with third-party and vendor code from ST, CMSIS, FreeRTOS, and sensor support packages.

Important notes before publishing:

- Preserve the license files already present in vendor and middleware directories.
- Keep attribution notices that come with third-party code.
- If you want a single top-level open-source license for your original application code, add a root `LICENSE` file that matches your intent.

One example of an existing component license in this repository:

- `STM32CubeIDE/Application/User/Core/VL53L5CX/LICENSE.md`

## Safety

- Use an appropriate power supply for the connected servos.
- Do not power multiple servos from a weak USB rail if the load is significant.
- Keep fingers, cables, and loose parts away from moving arms and linkages.
- Young children should use the kit with adult supervision.

## Contributing

Contributions that improve reliability, documentation, educational examples, BLE tools, motion behaviors, and sensor-based demos are welcome.

If you are new to the codebase, start with:

- `agent.md`

That file explains the project structure, code ownership areas, generated-code boundaries, and where to add new behavior safely.
