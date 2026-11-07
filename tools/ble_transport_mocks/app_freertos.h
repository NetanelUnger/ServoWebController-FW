#ifndef TEST_APP_FREERTOS_H
#define TEST_APP_FREERTOS_H
#include "cmsis_os2.h"
extern osMessageQueueId_t commTxQueueHandle, commRxQueueHandle;
extern osEventFlagsId_t bleTxEventHandle, commEventHandle;
extern osThreadId_t bleTxTaskHandle;
typedef uint32_t StackType_t;
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
uint32_t uxTaskGetStackHighWaterMark(osThreadId_t);
#define COMM_NUS_RX_EVENT 1U
#endif
