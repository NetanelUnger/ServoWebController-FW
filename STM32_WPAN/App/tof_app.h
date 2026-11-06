/**
  ******************************************************************************
  * @file    tof_app.h
  * @brief   Application helpers for the VL53L5CX BLE service.
  ******************************************************************************
  */

#ifndef TOF_APP_H
#define TOF_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ble_types.h"
#include "ble_core.h"

typedef enum
{
  TOF_CONN_HANDLE_EVT,
  TOF_DISCON_HANDLE_EVT,
  TOF_LAST_EVT,
} TOF_APP_OpcodeNotificationEvt_t;

typedef struct
{
  TOF_APP_OpcodeNotificationEvt_t EvtOpcode;
  uint16_t ConnectionHandle;
} TOF_APP_ConnHandleNotEvt_t;

/* 0 off, 1 legacy BLE subscription mode (default), 3 local depth + BLE.
 * Mode 2 is reserved. Mode 3 ranges without a map subscriber while connected. */
uint8_t TOF_APP_SetMode(uint8_t mode);
uint8_t TOF_APP_GetMode(void);
void TOF_APP_Init(void);
void TOF_APP_EvtRx(TOF_APP_ConnHandleNotEvt_t *p_Notification);
uint8_t TOF_APP_IsNotificationEnabled(void);
tBleStatus TOF_APP_NotifyDistanceMap(const uint8_t *pData, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif /* TOF_APP_H */
