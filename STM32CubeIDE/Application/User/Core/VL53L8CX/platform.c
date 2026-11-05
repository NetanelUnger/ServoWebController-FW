/**
  ******************************************************************************
  * @file    platform.h
  * @author  IMG SW Application Team
  * @brief   This file contains all the platform functions prototypes
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

uint8_t VL53L8CX_RdByte(
		VL53L8CX_Platform *p_platform,
		uint16_t RegisterAdress,
		uint8_t *p_value)
{
  return p_platform->Read(p_platform->address, RegisterAdress, p_value, 1U);
}

uint8_t VL53L8CX_WrByte(
		VL53L8CX_Platform *p_platform,
		uint16_t RegisterAdress,
		uint8_t value)
{
  return p_platform->Write(p_platform->address, RegisterAdress, &value, 1U);
}

uint8_t VL53L8CX_WrMulti(
		VL53L8CX_Platform *p_platform,
		uint16_t RegisterAdress,
		uint8_t *p_values,
		uint32_t size)
{
  /* Bound each shared-bus transaction; firmware upload is about 84 KiB. */
  while (size != 0U) {
    uint16_t chunk = size > 256U ? 256U : (uint16_t)size;
    int32_t status = p_platform->Write(p_platform->address, RegisterAdress, p_values, chunk);
    if (status != 0) return (uint8_t)status;
    RegisterAdress += chunk;
    p_values += chunk;
    size -= chunk;
  }
  return 0;
}

uint8_t VL53L8CX_RdMulti(
		VL53L8CX_Platform *p_platform,
		uint16_t RegisterAdress,
		uint8_t *p_values,
		uint32_t size)
{
  /* Bound each shared-bus transaction; firmware upload is about 84 KiB. */
  while (size != 0U) {
    uint16_t chunk = size > 256U ? 256U : (uint16_t)size;
    int32_t status = p_platform->Read(p_platform->address, RegisterAdress, p_values, chunk);
    if (status != 0) return (uint8_t)status;
    RegisterAdress += chunk;
    p_values += chunk;
    size -= chunk;
  }
  return 0;
}

void VL53L8CX_SwapBuffer(
    uint8_t     *buffer,
    uint16_t     size)
{
  uint32_t i, tmp;

  /* Example of possible implementation using <string.h> */
  for(i = 0; i < size; i = i + 4)
  {
    tmp = (
      (uint32_t)buffer[i]<<24)
    |((uint32_t)buffer[i+1]<<16)
    |(buffer[i+2]<<8)
    |(buffer[i+3]);

    memcpy(&(buffer[i]), &tmp, 4);
  }
}

uint8_t VL53L8CX_WaitMs(
		VL53L8CX_Platform *p_platform,
		uint32_t TimeMs)
{
  if (TimeMs == 0U)
  {
    return 0U;
  }

  if (osKernelGetState() == osKernelRunning)
  {
    osDelay(pdMS_TO_TICKS(TimeMs));
  }
  else
  {
    uint32_t tickstart;

    tickstart = p_platform->GetTick();
    while ((p_platform->GetTick() - tickstart) < TimeMs)
    {
    }
  }

  return 0;
}

