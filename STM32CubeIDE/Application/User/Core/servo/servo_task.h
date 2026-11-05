/**
 * @file    servo_task.h
 * @brief   High-level servo state machine and timed motion control API.
 */

#ifndef APPLICATION_USER_CORE_SERVO_PID_TASK_H_
#define APPLICATION_USER_CORE_SERVO_PID_TASK_H_

#include "main.h"

/** @brief Total number of servo channels managed by the firmware. */
#define SERVO_CHANNEL_COUNT 16U

/**
 * @brief State used to interpolate a servo move over a fixed duration.
 */
typedef struct
{
    /** @brief Non-zero while the move is being interpolated. */
    uint8_t active;
    /** @brief Pulse width at the start of the move, in microseconds. */
    uint16_t start_value;
    /** @brief Target pulse width at the end of the move, in microseconds. */
    uint16_t target_value;
    /** @brief Total move duration in milliseconds. */
    uint32_t duration_ms;
    /** @brief Time already elapsed since the move started, in milliseconds. */
    uint32_t elapsed_ms;
} servo_timed_move_t;

/**
 * @brief Runtime state for one logical servo output.
 */
typedef struct
{
    /** @brief Most recently commanded pulse width, in microseconds. */
    uint16_t current_value;
    /** @brief Interpolation state for the current timed move, if any. */
    servo_timed_move_t move;
} servo_state_t;

/**
 * @brief Top-level controller state shared between the command and servo tasks.
 */
typedef struct
{
    /** @brief Indicates whether the PCA9685 was initialized successfully. */
    uint8_t pca9685_ready;
    /** @brief Non-zero while DEMO1 owns all PWM channels. */
    uint8_t demo1_active;
    /** @brief Per-channel servo runtime state. */
    servo_state_t servo[SERVO_CHANNEL_COUNT];
} servos_controller_t;

/** @brief Global controller state for all servos. */
extern volatile servos_controller_t servos_controller;

/** @brief Callback invoked after one or more servo channel values change. */
typedef void (*servo_ChangeCallback_t)(void);

/**
 * @brief Initialize software-side servo state to neutral positions.
 */
extern void servo_Init(void);

/** BLE lifecycle hook; disconnect cancels demo and timed moves without I2C. */
extern void servo_SetBleConnected(uint8_t connected);
/** Start all 16 channels at 500 us, toggling 500/2000 us every 2000 ms. */
extern HAL_StatusTypeDef servo_StartDemo1(void);
/** Cancel DEMO1 and timed moves, holding the last commanded pulse widths. */
extern void servo_Stop(void);

/**
 * @brief Periodic worker that initializes the driver and updates timed moves.
 */
extern void servo_Task(void);

/**
 * @brief Register a callback that is invoked whenever commanded servo values change.
 * @param callback Function to call after a servo value change, or NULL to unregister.
 */
extern void servo_RegisterChangeCallback(servo_ChangeCallback_t callback);

/**
 * @brief Queue a servo pulse for the high-priority worker and cancel any timed move.
 * @param channel Servo channel index in the range `0..15`.
 * @param value Requested pulse width in microseconds.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef servo_SetServoImmediateValue(uint8_t channel, uint16_t value);

/**
 * @brief Queue all pulse widths atomically and cancel all active timed moves.
 * @param values Requested pulse widths in microseconds, indexed by channel `0..15`.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef servo_SetAllServoImmediateValues(const uint16_t values[SERVO_CHANNEL_COUNT]);

/**
 * @brief Copy the most recently commanded pulse widths for every servo channel.
 * @param values Output array receiving channel values in microseconds.
 */
extern void servo_GetAllServoImmediateValues(uint16_t values[SERVO_CHANNEL_COUNT]);

/**
 * @brief Start a linear move from the current position to a target pulse width.
 * @param channel Servo channel index in the range `0..15`.
 * @param target_value Requested final pulse width in microseconds.
 * @param duration_ms Move duration in milliseconds. A value of zero applies the target immediately.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef servo_StartTimedMove(uint8_t channel, uint16_t target_value, uint32_t duration_ms);

#endif /* APPLICATION_USER_CORE_SERVO_PID_TASK_H_ */
