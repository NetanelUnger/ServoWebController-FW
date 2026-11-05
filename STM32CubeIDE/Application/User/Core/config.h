/**
 * @file    config.h
 * @brief   Project-wide build, protocol, and product identity configuration.
 */

#ifndef PROJECTS_CONFIG_H
#define PROJECTS_CONFIG_H

/** @brief Disable the watchdog integration while bring-up is still in progress. */
#define DISABLE_WDT

#define ENABLE_COMM
#define ENABLE_LEDS

/* Keep the fixed-pulse diagnostic off while normal servo control owns the PCA. */
#define ENABLE_PCA9685_DIAGNOSTIC 0

/** GC9A01 worker: welcome screen, then color face animation. */
#ifndef ENABLE_DISPLAY
#define ENABLE_DISPLAY 1
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0
#endif
#ifndef DISPLAY_WELCOME_MS
#define DISPLAY_WELCOME_MS 2000U
#endif
#ifndef DISPLAY_FRAME_PERIOD_MS
#define DISPLAY_FRAME_PERIOD_MS 50U
#endif
#if DISPLAY_WELCOME_MS < 1 || DISPLAY_WELCOME_MS > 60000 || DISPLAY_FRAME_PERIOD_MS < 20 || DISPLAY_FRAME_PERIOD_MS > 1000
#error "Invalid display timing"
#endif
#if (ENABLE_DISPLAY != 0 && ENABLE_DISPLAY != 1) || (DISPLAY_ROTATION < 0 || DISPLAY_ROTATION > 3)
#error "Invalid display configuration"
#endif
/** @name I2C device switches
 *  Set to 1 to disable all access to that device; set to 0 to enable it.
 *  I2C3 stays configured; disabled devices issue no bus transfers.
 *  Build flags may override these defaults.
 *  @{ */
#ifndef DISABLE_I2C_IMU
#define DISABLE_I2C_IMU 0
#endif
#if DISABLE_I2C_IMU != 0 && DISABLE_I2C_IMU != 1
#error "DISABLE_I2C_IMU must be 0 or 1"
#endif
#ifndef DISABLE_I2C_PCA9685
#define DISABLE_I2C_PCA9685 0
#endif
#ifndef DISABLE_I2C_VL53L8CX
#define DISABLE_I2C_VL53L8CX 0
#endif
#if ((DISABLE_I2C_PCA9685 != 0) && (DISABLE_I2C_PCA9685 != 1)) || \
    ((DISABLE_I2C_VL53L8CX != 0) && (DISABLE_I2C_VL53L8CX != 1))
#error "I2C device switches must be 0 or 1"
#endif
#if !DISABLE_I2C_VL53L8CX
#define ENABLE_VL53L8CX
#endif
/** @} */

/**
 * @brief BLE device name used for both the GAP device name and the advertised complete local name.
 *
 * Keep this string at 11 visible characters or fewer with the current advertising payload layout.
 * That 11-character limit comes from the 31-byte legacy advertising packet after reserving space
 * for the 128-bit service UUID record in `app_ble.c`.
 */
#define ADV_NAME "BleServo"


/** @name Debug Output Configuration
 *  @{
 */
/** @brief Number of bytes emitted per line by hexadecimal dump helpers. */
#define CONFIG_BYTES_PER_LINE 16
/* #define CONFIG_DEBUG_TX 1 */
/* #define CONFIG_DEBUG_RX 1 */
/* #define CONFIG_DEBUG_TIME 1 */
/** @} */

/** @name BLE UART Event Flags
 *  @{
 */
#define EV_FLAGS_NUS_TX 0x01
#define EV_FLAGS_NUS_RX 0x02
#define EV_FLAGS_NUS (EV_FLAGS_NUS_TX | EV_FLAGS_NUS_RX)
/** @} */

/** @brief Default wait timeout used by worker tasks when they block on queues. */
#define THREAD_WAIT_TIMEOUT_MS    (500)
/** @brief Timeout converted into scheduler ticks for CMSIS-RTOS wait APIs. */
#define THREAD_WAIT_TIMEOUT_TICKS (pdMS_TO_TICKS(THREAD_WAIT_TIMEOUT_MS))

/** @name VL53L8CX Streaming Configuration
 *  @{
 */
#define CONFIG_VL53L8CX_STARTUP_DELAY_MS      100U
#define CONFIG_VL53L8CX_INIT_RETRY_MS        1000U
#define CONFIG_VL53L8CX_TASK_PERIOD_MS         20U
#define CONFIG_VL53L8CX_TIMING_BUDGET_MS       20U
#define CONFIG_VL53L8CX_RANGING_FREQUENCY_HZ    5U
/** @brief Absolute lower bound accepted by the runtime TOF BLE scaling command. */
#define CONFIG_VL53L8CX_DISTANCE_LIMIT_MIN_MM    0U
/** @brief Absolute upper bound accepted by the runtime TOF BLE scaling command. */
#define CONFIG_VL53L8CX_DISTANCE_LIMIT_MAX_MM 4000U
/** @brief Default lower bound mapped to BLE payload value 0. */
#define CONFIG_VL53L8CX_DISTANCE_MIN_MM 0U
/**
 * @brief Default upper bound mapped to the highest valid BLE payload value.
 *
 * The value `0xFF` stays reserved for invalid/no-target zones, so valid distances
 * are encoded across `0x00..0xFE`.
 */
#define CONFIG_VL53L8CX_DISTANCE_MAX_MM 500U
/** @} */

/** @name Watchdog Registration Flags
 *  @{
 */
#define WATCHDOG_FLAG_DEFAULT     (1UL << 0)
#define WATCHDOG_FLAG_COMM        (1UL << 1)
#define WATCHDOG_FLAG_VL53L8CX    (1UL << 2)
#ifdef ENABLE_VL53L8CX
#define WATCHDOG_ALL_FLAGS        (WATCHDOG_FLAG_DEFAULT | WATCHDOG_FLAG_COMM | WATCHDOG_FLAG_VL53L8CX)
#else
#define WATCHDOG_ALL_FLAGS        (WATCHDOG_FLAG_DEFAULT | WATCHDOG_FLAG_COMM)
#endif
/** @} */

/** @name BLE Device Information Service Strings
 *  @{
 */
#define BLE_ManufacturerNameString      "BleServosController"
#define BLE_ModelNumberString           "1.00"
#define BLE_SerialNumberString          "xxxxxxx"
#define BLE_HardwareRevisionString      "1.00"
#define BLE_FirmwareRevisionString      "1.00"
#define BLE_SoftwareRevisionString      "1.00"
#define BLE_SystemID                    "xxxxxxx"
/** @} */

#endif /* PROJECTS_CONFIG_H */
