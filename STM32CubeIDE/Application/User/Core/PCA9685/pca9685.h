/**
 * @file    pca9685.h
 * @brief   PCA9685 PWM and servo control driver API.
 */

#ifndef APPLICATION_USER_CORE_PCA9685_PCA9685_H_
#define APPLICATION_USER_CORE_PCA9685_PCA9685_H_

#include "main.h"

/** @brief Default 7-bit PCA9685 slave address. */
#define PCA9685_I2C_7BIT_ADDR 0x40U
/** @brief Number of PWM outputs provided by one PCA9685 device. */
#define PCA9685_CHANNEL_COUNT 16U

/** @brief Servo-friendly PWM refresh rate configured on the PCA9685. */
#define PCA9685_SERVO_FREQ_HZ 50U
/** @brief Lower pulse width clamp accepted by the servo abstraction. */
#define PCA9685_SERVO_MIN_PULSE_US 500U
/** @brief Upper pulse width clamp accepted by the servo abstraction. */
#define PCA9685_SERVO_MAX_PULSE_US 2500U
/** @brief Neutral servo pulse used during startup and reset. */
#define PCA9685_SERVO_NEUTRAL_PULSE_US 1500U

/**
 * @brief Detect and initialize the PCA9685 on the shared I2C bus.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef pca9685_Init(void);

/**
 * @brief Program raw PWM on/off tick positions for one output channel.
 * @param channel PCA9685 output channel index in the range `0..15`.
 * @param on_tick Tick index when the output becomes active.
 * @param off_tick Tick index when the output becomes inactive.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef pca9685_SetPwm(uint8_t channel, uint16_t on_tick, uint16_t off_tick);

/**
 * @brief Set one channel using a duty cycle expressed in hundredths of a percent.
 * @param channel PCA9685 output channel index in the range `0..15`.
 * @param duty_x100 Duty cycle in the range `0..10000`.
 * @param off_tick Optional output receiving the computed OFF tick.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef pca9685_SetDutyCycleX100(uint8_t channel, uint16_t duty_x100, uint16_t *off_tick);

/**
 * @brief Set one servo channel by pulse width in microseconds.
 * @param channel PCA9685 output channel index in the range `0..15`.
 * @param pulse_us Requested pulse width in microseconds.
 * @param off_tick Optional output receiving the computed OFF tick.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
extern HAL_StatusTypeDef pca9685_SetServoPulseUs(uint8_t channel, uint16_t pulse_us, uint16_t *off_tick);

#endif /* APPLICATION_USER_CORE_PCA9685_PCA9685_H_ */
