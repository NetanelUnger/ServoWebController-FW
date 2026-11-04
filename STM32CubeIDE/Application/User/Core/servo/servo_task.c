/**
 * @file    servo_task.c
 * @brief   Timed servo interpolation and PCA9685 synchronization task.
 */

#include <servo/servo_task.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "../PCA9685/pca9685.h"

/** @brief Servo update period used for timed interpolation. */
#define SERVO_TASK_PERIOD_MS 20U

/** @brief Global runtime state for the servo controller. */
volatile servos_controller_t servos_controller;

/**
 * @brief Clamp a pulse width to the servo-safe range.
 * @param pulse_us Requested pulse width in microseconds.
 * @return Pulse width limited to the configured servo range.
 */
static uint16_t servo_ClampPulseUs(uint16_t pulse_us)
{
    if (pulse_us < PCA9685_SERVO_MIN_PULSE_US)
    {
        return PCA9685_SERVO_MIN_PULSE_US;
    }

    if (pulse_us > PCA9685_SERVO_MAX_PULSE_US)
    {
        return PCA9685_SERVO_MAX_PULSE_US;
    }

    return pulse_us;
}

/**
 * @brief Cancel the current timed move and align the move endpoints to the current value.
 * @param servo Servo state to update.
 */
static void servo_StopTimedMove(volatile servo_state_t *servo)
{
    servo->move.active = 0U;
    servo->move.start_value = servo->current_value;
    servo->move.target_value = servo->current_value;
    servo->move.duration_ms = 0U;
    servo->move.elapsed_ms = 0U;
}

/**
 * @brief Compute the next pulse value for an active timed move.
 *
 * This helper expects the caller to protect the shared servo state against
 * concurrent access while the move structure is being updated.
 *
 * @param servo Servo state to advance.
 * @param step_ms Time step to apply in milliseconds.
 * @return Next pulse width that should be sent to the hardware.
 */
static uint16_t servo_ComputeTimedMoveValue(volatile servo_state_t *servo, uint32_t step_ms)
{
    int64_t delta;
    int64_t scaled;
    int64_t rounded_offset;
    int32_t next_value;

    if (servo->move.active == 0U)
    {
        return servo->current_value;
    }

    if ((servo->move.duration_ms == 0U) || ((servo->move.elapsed_ms + step_ms) >= servo->move.duration_ms))
    {
        servo->move.elapsed_ms = servo->move.duration_ms;
        servo->move.active = 0U;
        return servo->move.target_value;
    }

    servo->move.elapsed_ms += step_ms;

    delta = (int64_t)((int32_t)servo->move.target_value - (int32_t)servo->move.start_value);
    scaled = delta * (int64_t)servo->move.elapsed_ms;
    rounded_offset = (scaled >= 0) ? (int64_t)(servo->move.duration_ms / 2U) : -(int64_t)(servo->move.duration_ms / 2U);
    next_value = (int32_t)servo->move.start_value + (int32_t)((scaled + rounded_offset) / (int64_t)servo->move.duration_ms);

    return servo_ClampPulseUs((uint16_t)next_value);
}

void servo_Init(void)
{
    uint8_t channel;

    servos_controller.pca9685_ready = 0U;

    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        servos_controller.servo[channel].current_value = PCA9685_SERVO_NEUTRAL_PULSE_US;
        servos_controller.servo[channel].move.active = 0U;
        servos_controller.servo[channel].move.start_value = PCA9685_SERVO_NEUTRAL_PULSE_US;
        servos_controller.servo[channel].move.target_value = PCA9685_SERVO_NEUTRAL_PULSE_US;
        servos_controller.servo[channel].move.duration_ms = 0U;
        servos_controller.servo[channel].move.elapsed_ms = 0U;
    }
}

HAL_StatusTypeDef servo_SetServoImmediateValue(uint8_t channel, uint16_t value)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    uint16_t clamped_value;
    volatile servo_state_t *servo;

    if (channel >= SERVO_CHANNEL_COUNT)
    {
        return HAL_ERROR;
    }

    clamped_value = servo_ClampPulseUs(value);
    servo = &servos_controller.servo[channel];

    taskENTER_CRITICAL();
    servo->current_value = clamped_value;
    servo_StopTimedMove(servo);
    taskEXIT_CRITICAL();

    if (servos_controller.pca9685_ready != 0U)
    {
        if (pca9685_SetServoPulseUs(channel, clamped_value, NULL) != HAL_OK)
        {
            servos_controller.pca9685_ready = 0U;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef servo_StartTimedMove(uint8_t channel, uint16_t target_value, uint32_t duration_ms)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    volatile servo_state_t *servo;
    uint16_t clamped_target;

    if (channel >= SERVO_CHANNEL_COUNT)
    {
        return HAL_ERROR;
    }

    if (duration_ms == 0U)
    {
        return servo_SetServoImmediateValue(channel, target_value);
    }

    servo = &servos_controller.servo[channel];
    clamped_target = servo_ClampPulseUs(target_value);

    taskENTER_CRITICAL();
    servo->move.active = 1U;
    servo->move.start_value = servo->current_value;
    servo->move.target_value = clamped_target;
    servo->move.duration_ms = duration_ms;
    servo->move.elapsed_ms = 0U;
    taskEXIT_CRITICAL();

    return HAL_OK;
}

void servo_Task(void)
{
#if DISABLE_I2C_PCA9685
    osDelay(pdMS_TO_TICKS(SERVO_TASK_PERIOD_MS));
    return;
#endif
    uint8_t channel;
    uint16_t next_value;
    volatile servo_state_t *servo;

    osDelay(pdMS_TO_TICKS(SERVO_TASK_PERIOD_MS));

    if (servos_controller.pca9685_ready == 0U)
    {
        servos_controller.pca9685_ready = (pca9685_Init() == HAL_OK) ? 1U : 0U;
    }

    if (servos_controller.pca9685_ready == 0U)
    {
        return;
    }

    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        servo = &servos_controller.servo[channel];

        taskENTER_CRITICAL();
        next_value = servo_ComputeTimedMoveValue(servo, SERVO_TASK_PERIOD_MS);
        taskEXIT_CRITICAL();

        if (pca9685_SetServoPulseUs(channel, next_value, NULL) == HAL_OK)
        {
            taskENTER_CRITICAL();
            servo->current_value = next_value;
            if (servo->move.active == 0U)
            {
                servo->move.start_value = next_value;
                servo->move.target_value = next_value;
            }
            taskEXIT_CRITICAL();
        }
        else
        {
            servos_controller.pca9685_ready = 0U;
            break;
        }
    }
}
