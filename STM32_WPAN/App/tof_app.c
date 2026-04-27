/**
  ******************************************************************************
  * @file    tof_app.c
  * @brief   Application helpers for the VL53L5CX BLE service.
  ******************************************************************************
  */

#include "main.h"
#include "app_common.h"
#include "tof_app.h"
#include "tof.h"

#define TOF_APP_INVALID_CONN_HANDLE 0xFFFFU
#define TOF_APP_DISTANCE_MAP_LEN    64U

typedef enum
{
  TOF_NOTIFICATION_OFF = 0U,
  TOF_NOTIFICATION_ON = 1U
} TOF_APP_SendInformation_t;

typedef struct
{
  TOF_APP_SendInformation_t DistanceNotificationStatus;
  uint16_t ConnectionHandle;
} TOF_APP_Context_t;

static TOF_APP_Context_t TOF_APP_Context;

void TOF_Notification(TOF_NotificationEvt_t *p_Notification)
{
  switch (p_Notification->EvtOpcode)
  {
    case TOF_DISTANCE_NOTIFY_ENABLED_EVT:
      TOF_APP_Context.DistanceNotificationStatus = TOF_NOTIFICATION_ON;
      break;

    case TOF_DISTANCE_NOTIFY_DISABLED_EVT:
      TOF_APP_Context.DistanceNotificationStatus = TOF_NOTIFICATION_OFF;
      break;

    default:
      break;
  }
}

void TOF_APP_EvtRx(TOF_APP_ConnHandleNotEvt_t *p_Notification)
{
  switch (p_Notification->EvtOpcode)
  {
    case TOF_CONN_HANDLE_EVT:
      TOF_APP_Context.ConnectionHandle = p_Notification->ConnectionHandle;
      TOF_APP_Context.DistanceNotificationStatus = TOF_NOTIFICATION_OFF;
      break;

    case TOF_DISCON_HANDLE_EVT:
      TOF_APP_Context.ConnectionHandle = TOF_APP_INVALID_CONN_HANDLE;
      TOF_APP_Context.DistanceNotificationStatus = TOF_NOTIFICATION_OFF;
      break;

    default:
      break;
  }
}

void TOF_APP_Init(void)
{
  TOF_APP_Context.ConnectionHandle = TOF_APP_INVALID_CONN_HANDLE;
  TOF_APP_Context.DistanceNotificationStatus = TOF_NOTIFICATION_OFF;
  TOF_Init();
}

uint8_t TOF_APP_IsNotificationEnabled(void)
{
  if ((TOF_APP_Context.ConnectionHandle == TOF_APP_INVALID_CONN_HANDLE) ||
      (TOF_APP_Context.DistanceNotificationStatus == TOF_NOTIFICATION_OFF))
  {
    return 0U;
  }

  return 1U;
}

tBleStatus TOF_APP_NotifyDistanceMap(const uint8_t *pData, uint8_t length)
{
  TOF_Data_t data;

  if ((pData == NULL) || (length != TOF_APP_DISTANCE_MAP_LEN) || (TOF_APP_IsNotificationEnabled() == 0U))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }

  data.p_Payload = (uint8_t *)pData;
  data.Length = length;

  return TOF_UpdateValue(TOF_DISTANCE_MAP, &data);
}
