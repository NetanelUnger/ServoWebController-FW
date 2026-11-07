/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.h
  * Description        : FreeRTOS applicative header file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_FREERTOS_H
#define __APP_FREERTOS_H

#ifdef __cplusplus
extern "C" {
#endif
/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
extern volatile uint32_t s_threadWatchdogFlags;
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
//#define WATCHDOG_FLAG_DEFAULT      (1UL << 0)
//#define WATCHDOG_FLAG_ADVERTISING  (1UL << 1)
//#define WATCHDOG_FLAG_MEASUREMENT  (1UL << 2)
//#define WATCHDOG_FLAG_IMU		   (1UL << 3)
//#define WATCHDOG_FLAG_ANALOG	   (1UL << 4)
//#define WATCHDOG_FLAG_MEMORY	   (1UL << 6)
//#define WATCHDOG_FLAG_LOGGER	   (1UL << 7)
//#define WATCHDOG_ALL_FLAGS (WATCHDOG_FLAG_DEFAULT | WATCHDOG_FLAG_ADVERTISING | WATCHDOG_FLAG_MEASUREMENT | WATCHDOG_FLAG_IMU | WATCHDOG_FLAG_ANALOG)
//#define THREAD_WAIT_TIMEOUT_TICKS  (pdMS_TO_TICKS(1000u))
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
extern osStatus_t NUS_APP_EnqueueRX(uint16_t connection, const uint8_t *pData, uint16_t length);
extern osThreadId_t bleTxTaskHandle;
extern osEventFlagsId_t bleTxEventHandle;
extern void WatchdogFlagClear(uint32_t flagMask);
/* USER CODE END PD */

/* Exported macro -------------------------------------------------------------*/
/* USER CODE BEGIN EM */
extern osThreadId_t displayTaskHandle;
extern osMutexId_t displayMutexHandle;
extern osSemaphoreId_t FMWriteSemaphoreHandle;
extern osSemaphoreId_t FMEraseSemaphoreHandle;
/* USER CODE END EM */
extern osThreadId_t defaultTaskHandle;
extern osThreadId_t commTaskHandle;
extern osThreadId_t servoTaskHandle;
extern osThreadId_t vl53l8cxTaskHandle;
extern osMutexId_t i2cMutexHandle;
extern osMessageQueueId_t commTxQueueHandle;
extern osMessageQueueId_t commRxQueueHandle;
extern osMessageQueueId_t memoryQueueHandle;
extern osSemaphoreId_t FMEraseSemaphoreHandle;
extern osSemaphoreId_t FMWriteSemaphoreHandle;
extern osEventFlagsId_t commEventHandle;
extern osEventFlagsId_t loggerOutEventsHandle;
extern osEventFlagsId_t servoEventHandle;

/* Exported function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void displayTask_Entry(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void commTask_Entry(void *argument);
void servoTask_Entry(void *argument);
void vl53l8cxTask_Entry(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName);
void vApplicationGetRandomHeapCanary( portPOINTER_SIZE_TYPE * pxHeapCanary);
/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void LogHexArray(const char *prefix, const uint8_t *bytes, size_t length);
/* USER CODE END Application */

#ifdef __cplusplus
}
#endif
#endif /* __APP_FREERTOS_H */
