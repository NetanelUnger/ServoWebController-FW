#ifndef TEST_CMSIS_OS2_H
#define TEST_CMSIS_OS2_H
#include <stddef.h>
#include <stdint.h>
typedef void *osMessageQueueId_t;
typedef void *osEventFlagsId_t;
typedef void *osThreadId_t;
typedef int32_t osStatus_t;
#define osOK 0
#define osError -1
#define osErrorTimeout -2
#define osErrorResource -3
#define osErrorParameter -4
#define osErrorNoMemory -5
#define osWaitForever UINT32_MAX
#define osFlagsWaitAny 0U
uint32_t osKernelGetTickFreq(void);
uint32_t osKernelGetTickCount(void);
osStatus_t osMessageQueuePut(osMessageQueueId_t,const void *,uint8_t,uint32_t);
osStatus_t osMessageQueueGet(osMessageQueueId_t,void *,uint8_t *,uint32_t);
uint32_t osMessageQueueGetCount(osMessageQueueId_t);
uint32_t osMessageQueueGetSpace(osMessageQueueId_t);
uint32_t osEventFlagsSet(osEventFlagsId_t,uint32_t);
uint32_t osEventFlagsWait(osEventFlagsId_t,uint32_t,uint32_t,uint32_t);
void test_enter_critical(void);
void test_exit_critical(void);
#define taskENTER_CRITICAL() test_enter_critical()
#define taskEXIT_CRITICAL() test_exit_critical()
#endif
