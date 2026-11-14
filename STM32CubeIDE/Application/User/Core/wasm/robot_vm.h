#ifndef ROBOT_VM_H
#define ROBOT_VM_H
#include <stddef.h>
#include <stdint.h>
/* Single owner task; commands only submit/cancel work. Never delete it. */
void RobotVM_Init(uint8_t *native_stack_bottom);
void RobotVM_Task(void *argument);
void RobotVM_RequestStop(void);
const char *RobotVM_Start(const char *name);
const char *RobotVM_StartFile(uint32_t id);
void RobotVM_Status(char *out, size_t capacity);
#endif
