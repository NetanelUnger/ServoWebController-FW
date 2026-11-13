/**
 * @file    servo_task.c
 * @brief   Timed servo interpolation and PCA9685 synchronization task.
 */

#include <servo/servo_task.h>

#include "config.h"
#include <stddef.h>
#include <stdio.h>
#include "../display/image_store.h"
#include "timeline_format.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "app_freertos.h"
#include "../PCA9685/pca9685.h"
#include "../board_io.h"
#include "timer_if.h"

/** @brief Servo update period used for timed interpolation. */
#define SERVO_TASK_PERIOD_MS 20U
#define SERVO_DEMO1_PERIOD_MS 2000U
#define SERVO_DEMO1_LOW_US 500U
#define SERVO_DEMO1_HIGH_US 2000U

_Static_assert(SERVO_CHANNEL_COUNT == PCA9685_CHANNEL_COUNT,
               "BLE servo snapshots and PCA frames must cover the same channels");

/** @brief Global runtime state for the servo controller. */
volatile servos_controller_t servos_controller;

/* Only servoTask performs PCA writes. Producers update state and wake it. */
static volatile uint16_t dirty_channels;
static TickType_t move_tick[SERVO_CHANNEL_COUNT];
static uint32_t demo_tick;
static uint16_t demo_value;
static uint8_t ble_connected;
static uint8_t safe_pose_requested;
static uint8_t safe_pose_deferred;
static uint32_t safe_pose_generation;
static struct {
    const uint8_t *events;
    uint32_t id,count,next,duration,repeat,elapsed,max_late,last_tick;
    uint64_t ticks,cycle_ms;
    uint32_t starts[SERVO_CHANNEL_COUNT];
    volatile uint8_t active,loading,cancelled,leased,finishing;
    const char *state;
} timeline = {.state="IDLE"};
static void timeline_StopLocked(const char *state) {
    timeline.finishing=0U;
    if(timeline.loading) timeline.cancelled=1U;
    if(timeline.active) {timeline.active=0U;timeline.state=state;}
    if(timeline.leased && !timeline.loading) {
        timeline.leased=0U;ImageStore_ReleaseTimeline();
    }
}
static void servo_Wake(void)
{
    if (servoTaskHandle != NULL) (void)osThreadFlagsSet(servoTaskHandle, 1U);
}

/** @brief Optional observer called after commanded servo values change. */
static servo_ChangeCallback_t servo_change_callback = NULL;

static void servo_NotifyChange(void)
{
    servo_ChangeCallback_t callback = servo_change_callback;

    if (callback != NULL)
    {
        callback();
    }
}

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

/* Caller holds the state critical section; hardware writes stay in servoTask. */
static void servo_ApplyDemoValue(void)
{
    for (uint8_t channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++) {
        servos_controller.servo[channel].current_value = demo_value;
        servo_StopTimedMove(&servos_controller.servo[channel]);
    }
    dirty_channels = 0xffffU;
}

static void servo_StopLocked(void)
{
    timeline_StopLocked("STOPPED");
    servos_controller.demo1_active = 0U;
    for (uint8_t channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
        servo_StopTimedMove(&servos_controller.servo[channel]);
}

void servo_SetBleConnected(uint8_t connected)
{
    taskENTER_CRITICAL();
    uint8_t was_connected=ble_connected;
    ble_connected = connected != 0U;
    if (ble_connected) {
        safe_pose_requested=0U;
        safe_pose_deferred=0U;
        BoardIO_SetSafePoseActive(0U);
    } else if(was_connected) {
        /* Recovery or an in-flight COMMIT may publish the first pose later. */
        safe_pose_deferred=1U;
        (void)servo_EnterSafePose();
    } else {
        servo_StopLocked(); /* Initial NUS setup is not a BLE disconnect. */
    }
    taskEXIT_CRITICAL();
}

uint8_t servo_EnterSafePose(void)
{
    uint16_t values[SERVO_CHANNEL_COUNT];
    taskENTER_CRITICAL();
    servo_StopLocked();
    uint8_t valid=ImageStore_GetSafePose(values);
#if DISABLE_I2C_PCA9685
    valid=0U;
#endif
    safe_pose_requested=valid;
    ++safe_pose_generation;
    BoardIO_SetSafePoseActive(0U);
    if (valid) {
        safe_pose_deferred=0U;
        /* A pre-disconnect I2C snapshot must never re-enable stale outputs. */
        BoardIO_PrepareSafePose();
        for (uint8_t channel=0;channel<SERVO_CHANNEL_COUNT;channel++) {
            servos_controller.servo[channel].current_value=values[channel];
            servo_StopTimedMove(&servos_controller.servo[channel]);
        }
        dirty_channels=0xffffU;
    }
    taskEXIT_CRITICAL();
    servo_Wake();
    if(valid) servo_NotifyChange();
    return valid;
}

void servo_Stop(void)
{
    taskENTER_CRITICAL();
    servo_StopLocked();
    taskEXIT_CRITICAL();
    servo_Wake();
}

HAL_StatusTypeDef servo_StartDemo1(void)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    taskENTER_CRITICAL();
    if (!ble_connected || !servos_controller.pca9685_ready) {
        taskEXIT_CRITICAL();
        return HAL_ERROR;
    }
    if (timeline.active || timeline.loading) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    if (servos_controller.demo1_active) {
        taskEXIT_CRITICAL();
        return HAL_OK; /* Repeated requests do not restart the two-second phase. */
    }
    demo_tick = TIMER_IF_GetTimerValue();
    demo_value = SERVO_DEMO1_LOW_US;
    servos_controller.demo1_active = 1U;
    servo_ApplyDemoValue();
    taskEXIT_CRITICAL();
    servo_Wake();
    servo_NotifyChange();
    return HAL_OK;
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

#include "timeline_player.inc"

void servo_Init(void)
{
    uint8_t channel;

    servos_controller.pca9685_ready = 0U;
    servos_controller.demo1_active = 0U;
    ble_connected = 0U;
    safe_pose_requested = 0U;
    safe_pose_deferred = 0U;
    safe_pose_generation = 0U;
    dirty_channels = 0xffffU;

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

void servo_RegisterChangeCallback(servo_ChangeCallback_t callback)
{
    taskENTER_CRITICAL();
    servo_change_callback = callback;
    taskEXIT_CRITICAL();
}

HAL_StatusTypeDef servo_SetServoImmediateValue(uint8_t channel, uint16_t value)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    uint16_t clamped_value;
    volatile servo_state_t *servo;
    uint8_t changed;

    if (channel >= SERVO_CHANNEL_COUNT)
    {
        return HAL_ERROR;
    }

    clamped_value = servo_ClampPulseUs(value);
    servo = &servos_controller.servo[channel];

    taskENTER_CRITICAL();
    if (!ble_connected) {
        taskEXIT_CRITICAL();
        return HAL_ERROR;
    }
    if (timeline.active || timeline.loading) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    if (servos_controller.demo1_active) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    changed = (servo->current_value != clamped_value) ? 1U : 0U;
    servo->current_value = clamped_value;
    servo_StopTimedMove(servo);
    dirty_channels |= (uint16_t)(1U << channel);
    taskEXIT_CRITICAL();
    servo_Wake();

    if (changed != 0U)
    {
        servo_NotifyChange();
    }

    return HAL_OK;
}

HAL_StatusTypeDef servo_SetAllServoImmediateValues(const uint16_t values[SERVO_CHANNEL_COUNT])
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR; /* Binary writes must respect the same switch as text commands. */
#endif
    uint8_t channel;
    uint8_t changed = 0U;
    uint16_t clamped_values[SERVO_CHANNEL_COUNT];
    volatile servo_state_t *servo;

    if (values == NULL)
    {
        return HAL_ERROR;
    }

    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        clamped_values[channel] = servo_ClampPulseUs(values[channel]);
    }

    taskENTER_CRITICAL();
    if (!ble_connected) {
        taskEXIT_CRITICAL();
        return HAL_ERROR;
    }
    if (timeline.active || timeline.loading) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    if (servos_controller.demo1_active) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        servo = &servos_controller.servo[channel];

        if (servo->current_value != clamped_values[channel])
        {
            changed = 1U;
        }
        servo->current_value = clamped_values[channel];
        servo_StopTimedMove(servo);
    }
    dirty_channels = 0xffffU;
    taskEXIT_CRITICAL();
    servo_Wake();

    if (changed != 0U)
    {
        servo_NotifyChange();
    }

    return HAL_OK;
}

void servo_GetAllServoImmediateValues(uint16_t values[SERVO_CHANNEL_COUNT])
{
    uint8_t channel;

    if (values == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        values[channel] = servos_controller.servo[channel].current_value;
    }
    taskEXIT_CRITICAL();
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
    if (!ble_connected) {
        taskEXIT_CRITICAL();
        return HAL_ERROR;
    }
    if (timeline.active || timeline.loading) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    if (servos_controller.demo1_active) {
        taskEXIT_CRITICAL();
        return HAL_BUSY;
    }
    servo->move.active = 1U;
    servo->move.start_value = servo->current_value;
    servo->move.target_value = clamped_target;
    servo->move.duration_ms = duration_ms;
    servo->move.elapsed_ms = 0U;
    move_tick[channel] = xTaskGetTickCount();
    taskEXIT_CRITICAL();
    servo_Wake();
    return HAL_OK;
}

void servo_Task(void)
{
#if DISABLE_I2C_PCA9685
    osDelay(pdMS_TO_TICKS(SERVO_TASK_PERIOD_MS));
    return;
#endif
    uint8_t channel;
    uint8_t task_changed = 0U;
    uint16_t next_value;
    uint16_t frame[SERVO_CHANNEL_COUNT];
    uint8_t pending;
    uint32_t frame_generation;
    volatile servo_state_t *servo;

    (void)osThreadFlagsWait(1U, osFlagsWaitAny, (timeline.active ? 1U : pdMS_TO_TICKS(SERVO_TASK_PERIOD_MS)));

    taskENTER_CRITICAL();
    if(safe_pose_deferred && !ble_connected && ImageStore_GetSafePose(frame))
        (void)servo_EnterSafePose();
    taskEXIT_CRITICAL();

    if (servos_controller.pca9685_ready == 0U)
    {
        servos_controller.pca9685_ready = (pca9685_Init() == HAL_OK) ? 1U : 0U;
        taskENTER_CRITICAL();
        dirty_channels = 0xffffU;
        taskEXIT_CRITICAL();
    }

    if (servos_controller.pca9685_ready == 0U)
    {
        osDelay(pdMS_TO_TICKS(250)); /* A missing PCA must not starve the system. */
        return;
    }

    taskENTER_CRITICAL();
    if (servos_controller.demo1_active && ble_connected) {
        /* Use elapsed RTC time, not the quantized 1 ms scheduler tick. */
        uint32_t period = TIMER_IF_Convert_ms2Tick(SERVO_DEMO1_PERIOD_MS);
        uint32_t phases = (TIMER_IF_GetTimerValue() - demo_tick) / period;
        if (phases != 0U) {
            demo_tick += phases * period;
            if (phases & 1U)
                demo_value = (demo_value == SERVO_DEMO1_LOW_US) ? SERVO_DEMO1_HIGH_US : SERVO_DEMO1_LOW_US;
            servo_ApplyDemoValue();
            task_changed = 1U;
        }
    }
    task_changed |= timeline_AdvanceLocked();
    taskEXIT_CRITICAL();

    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
    {
        servo = &servos_controller.servo[channel];

        taskENTER_CRITICAL();
        if (!timeline.active && servo->move.active) {
            TickType_t now = xTaskGetTickCount();
            uint32_t elapsed = (uint32_t)((uint64_t)(now - move_tick[channel]) * 1000U / configTICK_RATE_HZ);
            if (elapsed) {
                move_tick[channel] = now;
                next_value = servo_ComputeTimedMoveValue(servo, elapsed);
                if (servo->current_value != next_value) {
                    servo->current_value = next_value;
                    dirty_channels |= (uint16_t)(1U << channel);
                    task_changed = 1;
                }
            }
        }
        taskEXIT_CRITICAL();
    }

    /* Capture one coherent frame after interpolation. New commands received
     * during the I2C transfer retain their dirty bits for the next frame. */
    taskENTER_CRITICAL();
    pending = dirty_channels != 0U;
    frame_generation = safe_pose_generation;
    for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
        frame[channel] = servos_controller.servo[channel].current_value;
    dirty_channels = 0U;
    taskEXIT_CRITICAL();
    if (pending) {
        if (pca9685_SetServoPulseFrame(frame) != HAL_OK) {
            servos_controller.pca9685_ready = 0U;
            BoardIO_SetPwmReady(0U);
            taskENTER_CRITICAL();
            if(timeline.active) {servo_StopLocked();timeline.state="FAULT";}
            taskEXIT_CRITICAL();
        } else {
            taskENTER_CRITICAL();
            if(frame_generation==safe_pose_generation) {
                if(safe_pose_requested) BoardIO_SetSafePoseActive(!ble_connected);
                BoardIO_SetPwmReady(1U);
            }
            taskEXIT_CRITICAL();
        }
    }

    taskENTER_CRITICAL();
    if(timeline.active && timeline.finishing) timeline_FinishLocked("COMPLETE");
    taskEXIT_CRITICAL();

    if (task_changed != 0U)
    {
        servo_NotifyChange();
    }
}
