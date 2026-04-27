/**
  ******************************************************************************
  * @file    tof.h
  * @brief   BLE service definition for VL53L5CX distance-map notifications.
  ******************************************************************************
  */

#ifndef TOF_H
#define TOF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ble_types.h"
#include "ble_core.h"
#include "svc_ctl.h"

typedef enum
{
  TOF_DISTANCE_MAP,
  TOF_CHAROPCODE_LAST
} TOF_CharOpcode_t;

typedef enum
{
  TOF_DISTANCE_NOTIFY_ENABLED_EVT,
  TOF_DISTANCE_NOTIFY_DISABLED_EVT
} TOF_OpcodeEvt_t;

typedef struct
{
  uint8_t *p_Payload;
  uint8_t Length;
} TOF_Data_t;

typedef struct
{
  TOF_OpcodeEvt_t EvtOpcode;
  TOF_Data_t DataTransfered;
  uint16_t ConnectionHandle;
  uint16_t AttributeHandle;
  uint8_t ServiceInstance;
} TOF_NotificationEvt_t;

void TOF_Init(void);
void TOF_Notification(TOF_NotificationEvt_t *p_Notification);
tBleStatus TOF_UpdateValue(TOF_CharOpcode_t CharOpcode, TOF_Data_t *pData);

#ifdef __cplusplus
}
#endif

#endif /* TOF_H */
