/**
 * @file    vl53l8cx_task.h
 * @brief   VL53L8CX sensor task and latest-frame runtime state.
 */

#ifndef APPLICATION_USER_CORE_VL53L8CX_VL53L8CX_TASK_H_
#define APPLICATION_USER_CORE_VL53L8CX_VL53L8CX_TASK_H_

#include <stdint.h>

#include "vl53l8cx.h"

#define VL53L8CX_ZONE_COUNT           64U
#define VL53L8CX_BLE_FRAME_SIZE       VL53L8CX_ZONE_COUNT
#define VL53L8CX_DISTANCE_INVALID_MM  0xFFFFU
#define VL53L8CX_BLE_DISTANCE_INVALID 0xFFU
#define VL53L8CX_BLE_DISTANCE_MAX_VALID (VL53L8CX_BLE_DISTANCE_INVALID - 1U)

typedef struct
{
    uint8_t initialized;
    uint8_t ranging_active;
    uint16_t ble_distance_min_mm;
    uint16_t ble_distance_max_mm;
    uint8_t ble_payload[VL53L8CX_BLE_FRAME_SIZE];
    uint16_t distance_mm[VL53L8CX_ZONE_COUNT];
    uint32_t last_frame_tick_ms;
    uint32_t frames;
    uint32_t faults;
    int32_t last_status;
    uint32_t init_attempts;
} vl53l8cx_runtime_t;

extern volatile vl53l8cx_runtime_t g_vl53l8cx_runtime;
/* SWD bring-up control. Zero by default: range only for BLE subscribers. */
extern volatile uint32_t g_vl53l8cx_force_ranging;

void vl53l8cx_TaskInit(void);
void vl53l8cx_Task(void);
void vl53l8cx_GetDistanceEncodingRange(uint16_t *pMinMm, uint16_t *pMaxMm);
uint8_t vl53l8cx_SetDistanceEncodingRange(uint16_t min_mm, uint16_t max_mm);

#endif /* APPLICATION_USER_CORE_VL53L8CX_VL53L8CX_TASK_H_ */
