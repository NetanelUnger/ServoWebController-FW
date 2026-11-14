# Core FW Agent Notes

This folder is mostly STM32Cube-generated startup and RTOS integration code. Keep edits small and prefer `USER CODE` blocks when changing generated files.

## Important Files

- `Src/main.c`: MCU boot path, peripheral initialization, GPIO setup, and the call sequence into FreeRTOS and WPAN.
- `Src/app_freertos.c`: static FreeRTOS objects, task entry functions, queues, event flags, watchdog flag clearing, and NUS RX/TX queue helpers.
- `Src/app_entry.c`: ST WPAN system startup, BLE stack initialization, flash/RNG/PKA support tasks, log setup, and app BLE startup.
- `Inc/app_freertos.h`: RTOS object handles and app-level helpers exported to other modules.
- `Inc/main.h`: generated pin/peripheral handles and board-level defines.

## Runtime Shape

Startup flow:

1. `main.c` initializes clocks, GPIO, I2C3, timers, RNG, RTC, and other peripherals.
2. `MX_FREERTOS_Init()` in `app_freertos.c` initializes app state and creates RTOS objects.
3. `MX_APPE_Init()` in `app_entry.c` initializes the WPAN/BLE stack and app services.
4. The scheduler runs `defaultTask`, `commTask`, enabled sensor/servo tasks, and `displayTask` when `ENABLE_DISPLAY=1`.

RTOS objects that matter for application work:

- `i2cMutexHandle`: protects the shared I2C3 bus used by PCA9685 and VL53L5CX.
- `displayTaskHandle` / `displayMutexHandle`: static, below-normal display owner and drawing/manual GPIO lock. Stop via `Display_Stop()`, never suspend it mid-transfer. No watchdog bit: it may intentionally wait forever.
- `wasmTaskHandle`: experimental static Low-priority WAMR owner, 4-KiB native stack plus a separate 12-KiB runtime pool. Hand-maintained in USER CODE like the other app tasks (no IOC task-list entry). Cancel through `RobotVM_RequestStop`, never task deletion. See `WAMR_BOARD.md` for the deliberately restricted guest and hardware evidence.
- `commRxQueueHandle` and `commTxQueueHandle`: statically copied RX/reply items with session ownership; no heap pointers. `commTask` only parses RX.
- `bleTxTaskHandle` / `bleTxEventHandle`: sole notification/indication owner with event-driven completion/pool waits; see root `BLE_TRANSPORT.md`.
- `commEventHandle`: wakes `commTask` for NUS RX work.
- `s_threadWatchdogFlags`: watchdog bookkeeping; update `config.h` and the relevant task loop if adding new watched tasks.

## Editing Rules

- Add or resize RTOS tasks, queues, mutexes, and event flags in both `app_freertos.c` and `app_freertos.h`; update `BleServoController.ioc` when the change belongs in generated CubeMX state.
- Keep RTOS object allocation static, matching the current style.
- Do not do heavy work inside BLE callbacks or interrupt callbacks; queue work into tasks.
- If a queue item allocates with `pvPortMalloc`, make sure the consumer frees it exactly once.
- Use `i2c_os` wrappers for shared I2C access instead of calling HAL I2C directly from application modules.
- Treat `main.c` peripheral setup as generated unless the change is a deliberate board or CubeMX configuration update.
