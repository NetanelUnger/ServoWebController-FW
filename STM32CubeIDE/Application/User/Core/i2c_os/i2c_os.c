/**
 * @file    i2c_os.c
 * @brief   RTOS-safe wrappers around asynchronous HAL I2C memory transfers.
 */

#include "i2c_os.h"

#define I2C_OS_TIMEOUT_MIN_MS          100U
#define I2C_OS_TIMEOUT_BASE_MS          20U
#define I2C_OS_TIMEOUT_PER_BYTE_US     200U
#define I2C_OS_TIMEOUT_MAX_MS        10000U

/** @brief Set while an interrupt-driven I2C transfer started by this module is still running. */
static volatile uint8_t _i2c_busy = 0U;

/** @brief Completion status reported by the most recent I2C interrupt callback. */
static volatile HAL_StatusTypeDef _i2c_status = HAL_OK;

/**
 * @brief Compute a conservative timeout for an interrupt-driven memory transfer.
 * @param size Number of data bytes in the transfer.
 * @param mem_add_size Register-address width used by the transfer.
 * @return Timeout budget in milliseconds.
 */
static uint32_t i2c_os_ComputeTimeoutMs(uint16_t size, uint16_t mem_add_size)
{
	uint32_t register_bytes;
	uint32_t total_bytes;
	uint32_t timeout_ms;

	register_bytes = (mem_add_size == I2C_MEMADD_SIZE_16BIT) ? 2U : 1U;
	total_bytes = (uint32_t)size + register_bytes + 2U;
	timeout_ms = I2C_OS_TIMEOUT_BASE_MS
			+ (uint32_t)(((total_bytes * I2C_OS_TIMEOUT_PER_BYTE_US) + 999U) / 1000U);

	if (timeout_ms < I2C_OS_TIMEOUT_MIN_MS)
	{
		timeout_ms = I2C_OS_TIMEOUT_MIN_MS;
	}
	else if (timeout_ms > I2C_OS_TIMEOUT_MAX_MS)
	{
		timeout_ms = I2C_OS_TIMEOUT_MAX_MS;
	}

	return timeout_ms;
}

/**
 * @brief Perform a mutex-protected asynchronous I2C memory write and wait for completion.
 * @param hi2c Target HAL I2C peripheral handle.
 * @param DevAddress 8-bit device address expected by STM32 HAL APIs.
 * @param MemAddress Register or memory address inside the slave device.
 * @param MemAddSize Width of @p MemAddress as defined by STM32 HAL.
 * @param pData Data buffer to transmit.
 * @param Size Number of bytes to transmit.
 * @return `HAL_OK`, `HAL_TIMEOUT`, or `HAL_ERROR` based on the transfer outcome.
 */
HAL_StatusTypeDef i2c_writeOS(I2C_HandleTypeDef *hi2c,
								uint16_t DevAddress,
								uint16_t MemAddress,
								uint16_t MemAddSize,
								uint8_t *pData,
								uint16_t Size)
{
	HAL_StatusTypeDef status;
	uint32_t t_out;

	if ((hi2c == NULL) || ((pData == NULL) && (Size > 0U)))
	{
		return HAL_ERROR;
	}

	t_out = i2c_os_ComputeTimeoutMs(Size, MemAddSize);

	if (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK)
	{
		_i2c_status = HAL_BUSY;
		_i2c_busy = 1U;
		if (HAL_I2C_Mem_Write_IT(hi2c, DevAddress, MemAddress, MemAddSize, pData, Size) != HAL_OK)
		{
			_i2c_status = HAL_ERROR;
			_i2c_busy = 0U;
			(void)osMutexRelease(i2cMutexHandle);

			return HAL_ERROR;
		}

		while ((_i2c_busy == 1U) && (t_out > 0U))
		{
			osDelay(1U);
			t_out -= 1U;
		}

		if (_i2c_busy == 1U)
		{
			_i2c_busy = 0U;
			_i2c_status = HAL_TIMEOUT;
			(void)osMutexRelease(i2cMutexHandle);

			return HAL_TIMEOUT;
		}

		status = _i2c_status;
		(void)osMutexRelease(i2cMutexHandle);
	}
	else
	{
		return HAL_ERROR;
	}

	return status;
}

/**
 * @brief Perform a mutex-protected asynchronous I2C memory read and wait for completion.
 * @param hi2c Target HAL I2C peripheral handle.
 * @param DevAddress 8-bit device address expected by STM32 HAL APIs.
 * @param MemAddress Register or memory address inside the slave device.
 * @param MemAddSize Width of @p MemAddress as defined by STM32 HAL.
 * @param pData Destination buffer for the received data.
 * @param Size Number of bytes to receive.
 * @return `HAL_OK`, `HAL_TIMEOUT`, or `HAL_ERROR` based on the transfer outcome.
 */
HAL_StatusTypeDef i2c_readOS(I2C_HandleTypeDef *hi2c,
								uint16_t DevAddress,
								uint16_t MemAddress,
								uint16_t MemAddSize,
								uint8_t *pData,
								uint16_t Size)
{
	HAL_StatusTypeDef status;
	uint32_t t_out;

	if ((hi2c == NULL) || ((pData == NULL) && (Size > 0U)))
	{
		return HAL_ERROR;
	}

	t_out = i2c_os_ComputeTimeoutMs(Size, MemAddSize);

	if (osMutexAcquire(i2cMutexHandle, osWaitForever) == osOK)
	{
		_i2c_status = HAL_BUSY;
		_i2c_busy = 1U;
		if (HAL_I2C_Mem_Read_IT(hi2c, DevAddress, MemAddress, MemAddSize, pData, Size) != HAL_OK)
		{
			_i2c_status = HAL_ERROR;
			_i2c_busy = 0U;
			(void)osMutexRelease(i2cMutexHandle);

			return HAL_ERROR;
		}

		while ((_i2c_busy == 1U) && (t_out > 0U))
		{
			osDelay(1U);
			t_out -= 1U;
		}

		if (_i2c_busy == 1U)
		{
			_i2c_busy = 0U;
			_i2c_status = HAL_TIMEOUT;
			(void)osMutexRelease(i2cMutexHandle);

			return HAL_TIMEOUT;
		}

		status = _i2c_status;
		(void)osMutexRelease(i2cMutexHandle);
	}
	else
	{
		return HAL_ERROR;
	}

	return status;
}

HAL_StatusTypeDef i2c_isDeviceReadyOS(I2C_HandleTypeDef *hi2c,
										uint16_t DevAddress,
										uint32_t Trials,
										uint32_t Timeout)
{
	HAL_StatusTypeDef status;

	if (hi2c == NULL)
	{
		return HAL_ERROR;
	}

	if (osMutexAcquire(i2cMutexHandle, osWaitForever) != osOK)
	{
		return HAL_ERROR;
	}

	status = HAL_I2C_IsDeviceReady(hi2c, DevAddress, Trials, Timeout);
	(void)osMutexRelease(i2cMutexHandle);

	return status;
}

/**
 * @brief HAL callback fired when an interrupt-driven I2C memory write completes.
 * @param hi2c HAL I2C handle that raised the callback.
 */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	(void)hi2c;

	if (_i2c_busy == 1U)
	{
		_i2c_status = HAL_OK;
		_i2c_busy = 0U;
	}
}

/**
 * @brief HAL callback fired when an interrupt-driven I2C memory read completes.
 * @param hi2c HAL I2C handle that raised the callback.
 */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
	(void)hi2c;

	if (_i2c_busy == 1U)
	{
		_i2c_status = HAL_OK;
		_i2c_busy = 0U;
	}
}

/**
 * @brief HAL callback fired when an interrupt-driven I2C memory transfer fails.
 * @param hi2c HAL I2C handle that raised the callback.
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
	(void)hi2c;

	if (_i2c_busy == 1U)
	{
		_i2c_status = HAL_ERROR;
		_i2c_busy = 0U;
	}
}
