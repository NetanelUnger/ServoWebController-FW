/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : FreeRTOS applicative file
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

/* Includes ------------------------------------------------------------------*/
#include <servo/servo_task.h>
#include <VL53L5CX/vl53l5cx_task.h>
#include "app_freertos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
#include "app_ble.h"
#include "config.h"
#include "ll_sys_if.h"
#include "timer_if.h"
#include "stm32wbaxx_hal_iwdg.h"
#include "log_module.h"
#include "log_module_conf.h"
#include "nus_app.h"
#include "comm/comm_interface.h"
#include "display/display.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
typedef StaticQueue_t osStaticMessageQDef_t;
typedef StaticSemaphore_t osStaticMutexDef_t;
typedef StaticSemaphore_t osStaticSemaphoreDef_t;
typedef StaticEventGroup_t osStaticEventGroupDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

volatile uint32_t s_threadWatchdogFlags = WATCHDOG_ALL_FLAGS;
osThreadId_t vl53l5cxTaskHandle;
uint32_t vl53l5cxTaskBuffer[ 2048 ];
osStaticThreadDef_t vl53l5cxTaskBlock;
const osThreadAttr_t vl53l5cxTask_attributes = {
  .name = "vl53l5cxTask",
  .stack_mem = &vl53l5cxTaskBuffer[0],
  .stack_size = sizeof(vl53l5cxTaskBuffer),
  .cb_mem = &vl53l5cxTaskBlock,
  .cb_size = sizeof(vl53l5cxTaskBlock),
  .priority = (osPriority_t) osPriorityAboveNormal,
};

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
uint32_t defaultTaskBuffer[ 4096 ];
osStaticThreadDef_t defaultTaskBlocTask;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_mem = &defaultTaskBuffer[0],
  .stack_size = sizeof(defaultTaskBuffer),
  .cb_mem = &defaultTaskBlocTask,
  .cb_size = sizeof(defaultTaskBlocTask),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for commTask */
osThreadId_t commTaskHandle;
uint32_t commTaskBuffer[ 2048 ];
osStaticThreadDef_t commTaskBlock;
const osThreadAttr_t commTask_attributes = {
  .name = "commTask",
  .stack_mem = &commTaskBuffer[0],
  .stack_size = sizeof(commTaskBuffer),
  .cb_mem = &commTaskBlock,
  .cb_size = sizeof(commTaskBlock),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for servoTask */
osThreadId_t servoTaskHandle;
uint32_t servoTaskBuffer[ 1024 ];
osStaticThreadDef_t servoTaskBlock;
const osThreadAttr_t servoTask_attributes = {
  .name = "servoTask",
  .stack_mem = &servoTaskBuffer[0],
  .stack_size = sizeof(servoTaskBuffer),
  .cb_mem = &servoTaskBlock,
  .cb_size = sizeof(servoTaskBlock),
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for i2cMutex */
osMutexId_t i2cMutexHandle;
osStaticMutexDef_t i2cMutexControlBlock;
const osMutexAttr_t i2cMutex_attributes = {
  .name = "i2cMutex",
  .cb_mem = &i2cMutexControlBlock,
  .cb_size = sizeof(i2cMutexControlBlock),
};
/* Definitions for commTxQueue */
osMessageQueueId_t commTxQueueHandle;
uint8_t NusTxQueueBuffer[ 5 * sizeof( NUS_APP_QueueItem_t ) ];
osStaticMessageQDef_t NusTxQueueControlBlock;
const osMessageQueueAttr_t commTxQueue_attributes = {
  .name = "commTxQueue",
  .cb_mem = &NusTxQueueControlBlock,
  .cb_size = sizeof(NusTxQueueControlBlock),
  .mq_mem = &NusTxQueueBuffer,
  .mq_size = sizeof(NusTxQueueBuffer)
};
/* Definitions for commRxQueue */
osMessageQueueId_t commRxQueueHandle;
uint8_t NusRxQueueBuffer[ 5 * sizeof( NUS_APP_QueueItem_t ) ];
osStaticMessageQDef_t NusRxQueueControlBlock;
const osMessageQueueAttr_t commRxQueue_attributes = {
  .name = "commRxQueue",
  .cb_mem = &NusRxQueueControlBlock,
  .cb_size = sizeof(NusRxQueueControlBlock),
  .mq_mem = &NusRxQueueBuffer,
  .mq_size = sizeof(NusRxQueueBuffer)
};
/* Definitions for memoryQueue */

/* Definitions for FMEraseSemaphore */
osSemaphoreId_t FMEraseSemaphoreHandle;
osStaticSemaphoreDef_t FMEraseSemaphoreControlBlock01;
const osSemaphoreAttr_t FMEraseSemaphore_attributes = {
  .name = "FMEraseSemaphore",
  .cb_mem = &FMEraseSemaphoreControlBlock01,
  .cb_size = sizeof(FMEraseSemaphoreControlBlock01),
};
/* Definitions for FMWriteSemaphore */
osSemaphoreId_t FMWriteSemaphoreHandle;
osStaticSemaphoreDef_t FMWriteSemaphoreControlBlock02;
const osSemaphoreAttr_t FMWriteSemaphore_attributes = {
  .name = "FMWriteSemaphore",
  .cb_mem = &FMWriteSemaphoreControlBlock02,
  .cb_size = sizeof(FMWriteSemaphoreControlBlock02),
};
/* Definitions for commEvent */
osEventFlagsId_t commEventHandle;
osStaticEventGroupDef_t commEventControlBlock;
const osEventFlagsAttr_t commEvent_attributes = {
  .name = "commEvent",
  .cb_mem = &commEventControlBlock,
  .cb_size = sizeof(commEventControlBlock),
};
/* Definitions for loggerOutEvents */
osEventFlagsId_t loggerOutEventsHandle;
osStaticEventGroupDef_t loggerOutEventControlBlock;
const osEventFlagsAttr_t loggerOutEvents_attributes = {
  .name = "loggerOutEvents",
  .cb_mem = &loggerOutEventControlBlock,
  .cb_size = sizeof(loggerOutEventControlBlock),
};
/* Definitions for servoEvent */
osEventFlagsId_t servoEventHandle;
osStaticEventGroupDef_t servoEventControlBlock;
const osEventFlagsAttr_t servoEvent_attributes = {
  .name = "servoEvent",
  .cb_mem = &servoEventControlBlock,
  .cb_size = sizeof(servoEventControlBlock),
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* Exported by the selected FreeRTOS GCC Cortex-M33 port. BLE can issue
 * ISR-safe RTOS calls during MX_APPE_Init, before osKernelStart. */
extern void vPortConfigureInterruptPriorities(void);
/* USER CODE END FunctionPrototypes */

/* USER CODE BEGIN 2 */
void vApplicationIdleHook( void )
{
   /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
   task. It is essential that code added to this hook function never attempts
   to block in any way (for example, call xQueueReceive() with a block time
   specified, or call vTaskDelay()). If the application makes use of the
   vTaskDelete() API function (as this demo application does) then it is also
   important that vApplicationIdleHook() is permitted to return to its calling
   function, because it is the responsibility of the idle task to clean up
   memory allocated by the kernel to any task that has since been deleted. */
}
/* USER CODE END 2 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
  Error_Handler();
}
/* USER CODE END 4 */

/* USER CODE BEGIN ENABLE_HEAP_PROTECTOR */
void vApplicationGetRandomHeapCanary( portPOINTER_SIZE_TYPE * pxHeapCanary)
{
}
/* USER CODE END ENABLE_HEAP_PROTECTOR */

/* USER CODE BEGIN VPORT_SUPPORT_TICKS_AND_SLEEP */
__weak void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime )
{
  // Generated when configUSE_TICKLESS_IDLE == 2.
  // Function called in tasks.c (in portTASK_FUNCTION).
  // TO BE COMPLETED or TO BE REPLACED by a user one, overriding that weak one.
}
/* USER CODE END VPORT_SUPPORT_TICKS_AND_SLEEP */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  /* Configure ISR validation before WPAN enables radio interrupts.
   * The port also repeats this initialization when the scheduler starts. */
  vPortConfigureInterruptPriorities();
  servo_Init();
#ifdef ENABLE_VL53L5CX
  vl53l5cx_TaskInit();
#endif /* ENABLE_VL53L5CX */

  /* USER CODE END Init */
  /* creation of i2cMutex */
  i2cMutexHandle = osMutexNew(&i2cMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */
  /* creation of FMEraseSemaphore */
  FMEraseSemaphoreHandle = osSemaphoreNew(1, 1, &FMEraseSemaphore_attributes);

  /* creation of FMWriteSemaphore */
  FMWriteSemaphoreHandle = osSemaphoreNew(1, 1, &FMWriteSemaphore_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */
  /* creation of commTxQueue */
  commTxQueueHandle = osMessageQueueNew (5, sizeof(NUS_APP_QueueItem_t), &commTxQueue_attributes);
  /* creation of commRxQueue */
  commRxQueueHandle = osMessageQueueNew (5, sizeof(NUS_APP_QueueItem_t), &commRxQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of commTask */
  commTaskHandle = osThreadNew(commTask_Entry, NULL, &commTask_attributes);

  /* creation of servoTask */
#if !DISABLE_I2C_PCA9685
  servoTaskHandle = osThreadNew(servoTask_Entry, NULL, &servoTask_attributes);
#endif

  /* USER CODE BEGIN RTOS_THREADS */
#ifdef ENABLE_VL53L5CX
  vl53l5cxTaskHandle = osThreadNew(vl53l5cxTask_Entry, NULL, &vl53l5cxTask_attributes);
  if (vl53l5cxTaskHandle == NULL)
  {
    Error_Handler();
  }
#endif /* ENABLE_VL53L5CX */
  /* USER CODE END RTOS_THREADS */

  /* creation of commEvent */
  commEventHandle = osEventFlagsNew(&commEvent_attributes);

  /* creation of loggerOutEvents */
  loggerOutEventsHandle = osEventFlagsNew(&loggerOutEvents_attributes);

  /* creation of servoEvent */
  servoEventHandle = osEventFlagsNew(&servoEvent_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}
/* USER CODE BEGIN Header_StartDefaultTask */
/**
* @brief Function implementing the defaultTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN defaultTask */
	for(;;)
	{
		osDelay(THREAD_WAIT_TIMEOUT_TICKS);

		WatchdogFlagClear(WATCHDOG_FLAG_DEFAULT);
	}
  /* USER CODE END defaultTask */
}

/* USER CODE BEGIN Header_commTask_Entry */
/**
* @brief Function implementing the commTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_commTask_Entry */
void commTask_Entry(void *argument)
{
  /* USER CODE BEGIN commTask */
#if ENABLE_DISPLAY
  /* HAL's RTC timebase starts in MX_APPE_Init, so display reset/sleep delays
   * must run here, after platform initialization and scheduler startup.
   * This also serializes startup drawing with later GPIO diagnostic commands. */
  g_display_boot_status = Display_ShowWelcome();
#endif
#ifdef ENABLE_COMM
	comm_Init();
#endif //ENABLE_COMM
  /* Infinite loop */
  for(;;)
  {
	#ifdef ENABLE_COMM
	  comm_Task();
	#else
	  osDelay(THREAD_WAIT_TIMEOUT_TICKS);
	#endif //ENABLE_COMM

	WatchdogFlagClear(WATCHDOG_FLAG_COMM);
  }
  /* USER CODE END commTask */
}

/* USER CODE BEGIN Header_servoTask_Entry */
/**
* @brief Function implementing the servoTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_servoTask_Entry */
void servoTask_Entry(void *argument)
{
  /* USER CODE BEGIN servoTask */
  /* Infinite loop */
  for(;;)
  {
      servo_Task();
  }
  /* USER CODE END servoTask */
}

void vl53l5cxTask_Entry(void *argument)
{
  UNUSED(argument);

  for (;;)
  {
    vl53l5cx_Task();
    WatchdogFlagClear(WATCHDOG_FLAG_VL53L5CX);
  }
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void WatchdogFlagClear(uint32_t flagMask)
{
  s_threadWatchdogFlags &= ~flagMask;
}


osStatus_t NUS_APP_EnqueueRX(const uint8_t *pData, uint8_t length)
{
	osStatus_t state = osOK;
	NUS_APP_QueueItem_t item;
	item.Length = length;

	item.Payload = pvPortMalloc(length);

	if (item.Payload)
	{
		memcpy(item.Payload, pData, length);

		if ( (state = osMessageQueuePut(commRxQueueHandle, &item, 0U, 100)) != osOK)
		{
			#ifdef CONFIG_DEBUG_RX
				LOG_INFO_APP("cant osMessageQueuePut to RX\r\n");
			#endif //CONFIG_DEBUG_RX
		}
		else
		{
			#ifdef CONFIG_DEBUG_RX
				//LogHexArray("NUS_APP_EnqueueRX Success:", item.Payload, item.Length);
			#endif //CONFIG_DEBUG_RX

			osEventFlagsSet(commEventHandle, COMM_NUS_RX_EVENT);
		}
	}
	else
	{
		state = osErrorNoMemory;
		#ifdef CONFIG_DEBUG_RX
		LOG_INFO_APP("cant acllocate osMessageQueuePut RX\r\n");
		#endif //CONFIG_DEBUG_RX
	}

	return state;
}

osStatus_t NUS_APP_EnqueueTX(const uint8_t *pData, uint8_t length)
{
	osStatus_t state = osOK;
	NUS_APP_QueueItem_t item;
	item.Length = length;

	item.Payload = pvPortMalloc(length);

	if (item.Payload)
	{
		memcpy(item.Payload, pData, length);

		if ( (state = osMessageQueuePut(commTxQueueHandle, &item, 0U, 100)) != osOK)
		{
			#ifdef CONFIG_DEBUG_TX
				LOG_INFO_APP("cant osMessageQueuePut to TX\r\n");
			#endif //CONFIG_DEBUG_TX
		}
		else
		{
			#ifdef CONFIG_DEBUG_TX
				LogHexArray("NUS_APP_EnqueueTX Success:", item.Payload, item.Length);
			#endif //CONFIG_DEBUG_TX

		    osEventFlagsSet(commEventHandle, COMM_NUS_TX_EVENT);
		}
	}
	else
	{
		state = osErrorNoMemory;
		#ifdef CONFIG_DEBUG_TX
			LOG_INFO_APP("cant acllocate osMessageQueuePut TX\r\n");
		#endif //CONFIG_DEBUG_TX
	}

	return state;
}


void LogHexArray(const char *prefix, const uint8_t *bytes, size_t length)
{
    LOG_INFO_APP("%s %u bytes\r\n",
                 (prefix && *prefix) ? prefix : "Buffer",
                 (unsigned)length);

    if (length == 0) {
        return;
    }

    char line[128]; // 16 * "0xNN," + null fits comfortably
    size_t i = 0;

    while (i < length) {
        const size_t chunk = (length - i > CONFIG_BYTES_PER_LINE) ? CONFIG_BYTES_PER_LINE : (length - i);
        size_t pos = 0;

        for (size_t j = 0; j < chunk; j++) {
            // Last element in the line has no trailing comma
            const int wrote = snprintf(&line[pos], sizeof(line) - pos,
                                       (j + 1 == chunk) ? "0x%02X" : "0x%02X,",
                                       bytes[i + j]);
            if (wrote <= 0 || (size_t)wrote >= (sizeof(line) - pos)) {
                // Flush what we have and retry this byte on next line
                line[pos] = '\0';
                LOG_INFO_APP("%s\r\n", line);
                pos = 0;
                // redo this j on an empty line
                j--;
                continue;
            }
            pos += (size_t)wrote;
        }

        line[pos] = '\0';
        LOG_INFO_APP("%s\r\n", line);
        i += chunk;
    }
}


/* USER CODE END Application */

