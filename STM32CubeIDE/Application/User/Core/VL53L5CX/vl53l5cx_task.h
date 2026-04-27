/**
 * @file    vl53l5cx_task.h
 * @brief   VL53L5CX sensor task and latest-frame runtime state.
 */

#ifndef APPLICATION_USER_CORE_VL53L5CX_VL53L5CX_TASK_H_
#define APPLICATION_USER_CORE_VL53L5CX_VL53L5CX_TASK_H_

#include <stdint.h>

#include "vl53l5cx.h"

#define VL53L5CX_ZONE_COUNT           64U
#define VL53L5CX_BLE_FRAME_SIZE       VL53L5CX_ZONE_COUNT
#define VL53L5CX_DISTANCE_INVALID_MM  0xFFFFU
#define VL53L5CX_BLE_DISTANCE_INVALID 0xFFU
#define VL53L5CX_BLE_DISTANCE_MAX_VALID (VL53L5CX_BLE_DISTANCE_INVALID - 1U)

typedef struct
{
    uint8_t initialized;
    uint8_t ranging_active;
    uint16_t ble_distance_min_mm;
    uint16_t ble_distance_max_mm;
    uint8_t ble_payload[VL53L5CX_BLE_FRAME_SIZE];
    uint16_t distance_mm[VL53L5CX_ZONE_COUNT];
    uint32_t last_frame_tick_ms;
} vl53l5cx_runtime_t;

extern volatile vl53l5cx_runtime_t g_vl53l5cx_runtime;

void vl53l5cx_TaskInit(void);
void vl53l5cx_Task(void);
void vl53l5cx_GetDistanceEncodingRange(uint16_t *pMinMm, uint16_t *pMaxMm);
uint8_t vl53l5cx_SetDistanceEncodingRange(uint16_t min_mm, uint16_t max_mm);

#endif /* APPLICATION_USER_CORE_VL53L5CX_VL53L5CX_TASK_H_ */
