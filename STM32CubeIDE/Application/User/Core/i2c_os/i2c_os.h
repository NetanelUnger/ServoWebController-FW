/**
 * @file    i2c_os.h
 * @brief   RTOS-safe wrappers around asynchronous HAL I2C memory transfers.
 */

#ifndef APPLICATION_USER_CORE_I2C_OS_I2C_OS_H_
#define APPLICATION_USER_CORE_I2C_OS_I2C_OS_H_

#include "main.h"
#include "app_freertos.h"

/**
 * @brief Perform a mutex-protected I2C memory write and wait for completion.
 * @param hi2c Target HAL I2C peripheral handle.
 * @param DevAddress 8-bit device address expected by STM32 HAL APIs.
 * @param MemAddress Register or memory address inside the slave device.
 * @param MemAddSize Width of @p MemAddress as defined by STM32 HAL.
 * @param pData Data buffer to transmit.
 * @param Size Number of bytes to transmit.
 * @return `HAL_OK`, `HAL_TIMEOUT`, or `HAL_ERROR` based on the transfer outcome.
 */
extern HAL_StatusTypeDef i2c_writeOS(I2C_HandleTypeDef *hi2c,
										uint16_t DevAddress,
										uint16_t MemAddress,
										uint16_t MemAddSize,
										uint8_t *pData,
										uint16_t Size);

/**
 * @brief Perform a mutex-protected I2C memory read and wait for completion.
 * @param hi2c Target HAL I2C peripheral handle.
 * @param DevAddress 8-bit device address expected by STM32 HAL APIs.
 * @param MemAddress Register or memory address inside the slave device.
 * @param MemAddSize Width of @p MemAddress as defined by STM32 HAL.
 * @param pData Destination buffer for the received data.
 * @param Size Number of bytes to receive.
 * @return `HAL_OK`, `HAL_TIMEOUT`, or `HAL_ERROR` based on the transfer outcome.
 */
extern HAL_StatusTypeDef i2c_readOS(I2C_HandleTypeDef *hi2c,
										uint16_t DevAddress,
										uint16_t MemAddress,
										uint16_t MemAddSize,
										uint8_t *pData,
										uint16_t Size);

/**
 * @brief Probe an I2C device while holding the shared bus mutex.
 * @param hi2c Target HAL I2C peripheral handle.
 * @param DevAddress 8-bit device address expected by STM32 HAL APIs.
 * @param Trials Number of ready-check attempts to perform.
 * @param Timeout Timeout for each attempt, in milliseconds.
 * @return `HAL_OK` when the device acknowledges, otherwise a HAL error code.
 */
extern HAL_StatusTypeDef i2c_isDeviceReadyOS(I2C_HandleTypeDef *hi2c,
												uint16_t DevAddress,
												uint32_t Trials,
												uint32_t Timeout);
#endif /* APPLICATION_USER_CORE_I2C_OS_I2C_OS_H_ */
