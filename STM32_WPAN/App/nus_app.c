/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    NUS_app.c
  * @author  MCD Application Team
  * @brief   NUS_app application definition.
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
#include "main.h"
#include "app_common.h"
#include "log_module.h"
#include "app_ble.h"
#include "ll_sys_if.h"
#include "dbg_trace.h"
#include "nus_app.h"
#include "nus.h"
#include "stm32_rtos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>

#include <servo/servo_task.h>
#include "board_io.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

typedef enum
{
  Tx_NOTIFICATION_OFF = 0U,
  Tx_NOTIFICATION_ON  = 1U,
  Tx_INDICATION_OFF   = 0U,
  Tx_INDICATION_ON    = 1U,
  /* USER CODE BEGIN Service2_APP_SendInformation_t */

  /* USER CODE END Service2_APP_SendInformation_t */
  NUS_APP_SENDINFORMATION_LAST
} NUS_APP_SendInformation_t;

typedef struct
{
  NUS_APP_SendInformation_t     Tx_Notification_Status;
  NUS_APP_SendInformation_t     Tx_Indication_Status;
  /* USER CODE BEGIN Service2_APP_Context_t */
  NUS_APP_SendInformation_t     AllChannelDc_Notification_Status;

  /* USER CODE END Service2_APP_Context_t */
  uint16_t              ConnectionHandle;
} NUS_APP_Context_t;

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUS_APP_INVALID_CONN_HANDLE 0xFFFFU
#define NUS_APP_ALL_CHANNEL_DC_VALUE_SIZE_BYTES 2U
#define NUS_APP_ALL_CHANNEL_DC_PAYLOAD_LEN (SERVO_CHANNEL_COUNT * NUS_APP_ALL_CHANNEL_DC_VALUE_SIZE_BYTES)
/* USER CODE END PD */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define NUS_APP_MIN(a,b) (((a) < (b)) ? (a) : (b))
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static NUS_APP_Context_t NUS_APP_Context;

uint8_t a_NUS_UpdateCharData[247];

/* USER CODE BEGIN PV */
static uint8_t NUS_APP_SuppressServoChangeSync;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void NUS_Tx_SendNotification(void);
static void NUS_Tx_SendIndication(void);

/* USER CODE BEGIN PFP */
static uint16_t NUS_APP_ReadLe16(const uint8_t *pData);
static void NUS_APP_WriteLe16(uint8_t *pData, uint16_t value);
static void NUS_APP_HandleServoValuesChanged(void);

/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void NUS_Notification(NUS_NotificationEvt_t *p_Notification)
{
  /* USER CODE BEGIN Service2_Notification_1 */

  /* USER CODE END Service2_Notification_1 */
  switch(p_Notification->EvtOpcode)
  {
    /* USER CODE BEGIN Service2_Notification_Service2_EvtOpcode */

    /* USER CODE END Service2_Notification_Service2_EvtOpcode */

    case NUS_TX_READ_EVT:
      /* USER CODE BEGIN Service2Char1_READ_EVT */

      /* USER CODE END Service2Char1_READ_EVT */
      break;

    case NUS_TX_NOTIFY_ENABLED_EVT:
      /* USER CODE BEGIN Service2Char1_NOTIFY_ENABLED_EVT */
    	NUS_APP_Context.Tx_Notification_Status = Tx_NOTIFICATION_ON;
    	LOG_INFO_APP("NOTIFY_ENABLED\r\n");
      /* USER CODE END Service2Char1_NOTIFY_ENABLED_EVT */
      break;

    case NUS_TX_NOTIFY_DISABLED_EVT:
      /* USER CODE BEGIN Service2Char1_NOTIFY_DISABLED_EVT */
    	NUS_APP_Context.Tx_Notification_Status = Tx_NOTIFICATION_OFF;
    	LOG_INFO_APP("NOTIFY_DISABLED\r\n");
      /* USER CODE END Service2Char1_NOTIFY_DISABLED_EVT */
      break;

    case NUS_TX_INDICATE_ENABLED_EVT:
      /* USER CODE BEGIN Service2Char1_INDICATE_ENABLED_EVT */
    	NUS_APP_Context.Tx_Indication_Status = Tx_INDICATION_ON;
    	LOG_INFO_APP("INDICATE_ENABLED\r\n");
      /* USER CODE END Service2Char1_INDICATE_ENABLED_EVT */
      break;

    case NUS_TX_INDICATE_DISABLED_EVT:
      /* USER CODE BEGIN Service2Char1_INDICATE_DISABLED_EVT */
    	NUS_APP_Context.Tx_Indication_Status = Tx_INDICATION_OFF;
    	LOG_INFO_APP("INDICATE_DISABLED\r\n");
      /* USER CODE END Service2Char1_INDICATE_DISABLED_EVT */
      break;

    case NUS_RX_WRITE_EVT:
      /* USER CODE BEGIN Service2Char2_WRITE_EVT */

      /* USER CODE END Service2Char2_WRITE_EVT */
      break;

    case NUS_ALL_CHANNEL_DC_NOTIFY_ENABLED_EVT:
      /* USER CODE BEGIN Service2Char3_NOTIFY_ENABLED_EVT */
      NUS_APP_Context.AllChannelDc_Notification_Status = Tx_NOTIFICATION_ON;
      LOG_INFO_APP("ALL_CHANNEL_DC_NOTIFY_ENABLED\r\n");
      /* USER CODE END Service2Char3_NOTIFY_ENABLED_EVT */
      break;

    case NUS_ALL_CHANNEL_DC_NOTIFY_DISABLED_EVT:
      /* USER CODE BEGIN Service2Char3_NOTIFY_DISABLED_EVT */
      NUS_APP_Context.AllChannelDc_Notification_Status = Tx_NOTIFICATION_OFF;
      LOG_INFO_APP("ALL_CHANNEL_DC_NOTIFY_DISABLED\r\n");
      /* USER CODE END Service2Char3_NOTIFY_DISABLED_EVT */
      break;

    default:
      /* USER CODE BEGIN Service2_Notification_default */

      /* USER CODE END Service2_Notification_default */
      break;
  }
  /* USER CODE BEGIN Service2_Notification_2 */

  /* USER CODE END Service2_Notification_2 */
  return;
}

void NUS_APP_EvtRx(NUS_APP_ConnHandleNotEvt_t *p_Notification)
{
  /* USER CODE BEGIN Service2_APP_EvtRx_1 */

  /* USER CODE END Service2_APP_EvtRx_1 */

  switch(p_Notification->EvtOpcode)
  {
    /* USER CODE BEGIN Service2_APP_EvtRx_Service2_EvtOpcode */
		
    /* USER CODE END Service2_APP_EvtRx_Service2_EvtOpcode */
    case NUS_CONN_HANDLE_EVT :
      /* USER CODE BEGIN Service2_APP_CONN_HANDLE_EVT */
        BLE_TX_Connected(p_Notification->ConnectionHandle);
        servo_SetBleConnected(1U);
        BoardIO_SetPowerConnected(1U);
		NUS_APP_Context.ConnectionHandle = p_Notification->ConnectionHandle;
		NUS_APP_Context.Tx_Notification_Status = Tx_NOTIFICATION_OFF;
		NUS_APP_Context.Tx_Indication_Status = Tx_INDICATION_OFF;
		NUS_APP_Context.AllChannelDc_Notification_Status = Tx_NOTIFICATION_OFF;
      /* USER CODE END Service2_APP_CONN_HANDLE_EVT */
      break;

    case NUS_DISCON_HANDLE_EVT :
      /* USER CODE BEGIN Service2_APP_DISCON_HANDLE_EVT */
        BLE_TX_Disconnected(p_Notification->ConnectionHandle);
        if (p_Notification->ConnectionHandle != NUS_APP_Context.ConnectionHandle) break;
        BoardIO_SetPowerConnected(0U);
        servo_SetBleConnected(0U);
		NUS_APP_Context.ConnectionHandle = NUS_APP_INVALID_CONN_HANDLE;
    	NUS_APP_Context.Tx_Indication_Status = Tx_INDICATION_OFF;
    	NUS_APP_Context.Tx_Notification_Status = Tx_NOTIFICATION_OFF;
        NUS_APP_Context.AllChannelDc_Notification_Status = Tx_NOTIFICATION_OFF;
      /* USER CODE END Service2_APP_DISCON_HANDLE_EVT */
      break;

    default:
      /* USER CODE BEGIN Service2_APP_EvtRx_default */

      /* USER CODE END Service2_APP_EvtRx_default */
      break;
  }

  /* USER CODE BEGIN Service2_APP_EvtRx_2 */

  /* USER CODE END Service2_APP_EvtRx_2 */

  return;
}

void NUS_APP_Init(void)
{
  BoardIO_SetPowerConnected(0U);
  servo_SetBleConnected(0U);
  UNUSED(NUS_APP_Context);
  NUS_APP_Context.ConnectionHandle = NUS_APP_INVALID_CONN_HANDLE;
  NUS_APP_Context.Tx_Notification_Status = Tx_NOTIFICATION_OFF;
  NUS_APP_Context.Tx_Indication_Status = Tx_INDICATION_OFF;
  NUS_APP_Context.AllChannelDc_Notification_Status = Tx_NOTIFICATION_OFF;
  NUS_APP_SuppressServoChangeSync = 0U;
  NUS_Init();
  servo_RegisterChangeCallback(NUS_APP_HandleServoValuesChanged);
  (void)NUS_APP_RefreshAllChannelDcRead();

  /* USER CODE BEGIN Service2_APP_Init */

  /* USER CODE END Service2_APP_Init */
  return;
}

/* USER CODE BEGIN FD */
tBleStatus NUS_APP_WriteAllChannelDcPayload(const uint8_t *pData, uint8_t length)
{
  uint8_t channel;
  HAL_StatusTypeDef servo_status;
  uint16_t values[SERVO_CHANNEL_COUNT];

  if ((pData == NULL) || (length != NUS_APP_ALL_CHANNEL_DC_PAYLOAD_LEN))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }

  for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
  {
    values[channel] = NUS_APP_ReadLe16(&pData[channel * NUS_APP_ALL_CHANNEL_DC_VALUE_SIZE_BYTES]);
  }

  NUS_APP_SuppressServoChangeSync = 1U;
  servo_status = servo_SetAllServoImmediateValues(values);
  NUS_APP_SuppressServoChangeSync = 0U;

  if (servo_status != HAL_OK)
  {
    return BLE_STATUS_FAILED;
  }

  return BLE_STATUS_SUCCESS;
}

tBleStatus NUS_APP_SyncAllChannelDcCharacteristic(void)
{
  uint8_t channel;
  uint16_t values[SERVO_CHANNEL_COUNT];
  uint8_t payload[NUS_APP_ALL_CHANNEL_DC_PAYLOAD_LEN];

  servo_GetAllServoImmediateValues(values);

  for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
  {
    NUS_APP_WriteLe16(&payload[channel * NUS_APP_ALL_CHANNEL_DC_VALUE_SIZE_BYTES], values[channel]);
  }


  return BLE_TX_Latest(BLE_TX_SERVO, payload, sizeof(payload));
}
tBleStatus NUS_APP_RefreshAllChannelDcRead(void)
{
  uint8_t channel;
  uint16_t values[SERVO_CHANNEL_COUNT];
  uint8_t payload[NUS_APP_ALL_CHANNEL_DC_PAYLOAD_LEN];
  NUS_Data_t data;

  servo_GetAllServoImmediateValues(values);

  for (channel = 0U; channel < SERVO_CHANNEL_COUNT; channel++)
  {
    NUS_APP_WriteLe16(&payload[channel * NUS_APP_ALL_CHANNEL_DC_VALUE_SIZE_BYTES], values[channel]);
  }

  data.p_Payload = payload;
  data.Length = NUS_APP_ALL_CHANNEL_DC_PAYLOAD_LEN;

  return NUS_UpdateValue(NUS_ALL_CHANNEL_DC, &data);
}
/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
__USED void NUS_Tx_SendNotification(void) /* Property Notification */
{
  NUS_APP_SendInformation_t notification_on_off = Tx_NOTIFICATION_OFF;
  NUS_Data_t nus_notification_data;

  nus_notification_data.p_Payload = (uint8_t*)a_NUS_UpdateCharData;
  nus_notification_data.Length = 0;

  /* USER CODE BEGIN Service2Char1_NS_1 */

  /* USER CODE END Service2Char1_NS_1 */

  if (notification_on_off != Tx_NOTIFICATION_OFF)
  {
    NUS_UpdateValue(NUS_TX, &nus_notification_data);
  }

  /* USER CODE BEGIN Service2Char1_NS_Last */

  /* USER CODE END Service2Char1_NS_Last */

  return;
}

__USED void NUS_Tx_SendIndication(void) /* Property Indication */
{
  NUS_APP_SendInformation_t indication_on_off = Tx_INDICATION_OFF;
  NUS_Data_t nus_indication_data;

  nus_indication_data.p_Payload = (uint8_t*)a_NUS_UpdateCharData;
  nus_indication_data.Length = 0;

  /* USER CODE BEGIN Service2Char1_IS_1 */

  /* USER CODE END Service2Char1_IS_1 */

  if (indication_on_off != Tx_INDICATION_OFF)
  {
    NUS_UpdateValue(NUS_TX, &nus_indication_data);
  }

  /* USER CODE BEGIN Service2Char1_IS_Last */

  /* USER CODE END Service2Char1_IS_Last */

  return;
}

/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */
static uint16_t NUS_APP_ReadLe16(const uint8_t *pData)
{
  return (uint16_t)((uint16_t)pData[0] | ((uint16_t)pData[1] << 8));
}

static void NUS_APP_WriteLe16(uint8_t *pData, uint16_t value)
{
  pData[0] = (uint8_t)(value & 0xFFU);
  pData[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void NUS_APP_HandleServoValuesChanged(void)
{
  if ((NUS_APP_SuppressServoChangeSync != 0U) ||
      (NUS_APP_Context.ConnectionHandle == NUS_APP_INVALID_CONN_HANDLE))
  {
    return;
  }

  (void)NUS_APP_SyncAllChannelDcCharacteristic();
}

void NusRxTask(NUS_APP_QueueItem_t *item)
{
	  uint8_t res[COMM_TEXT_RESPONSE_MAX_LEN];
	  packetSize_t res_len = 0;

	  if (!BLE_TX_SessionCurrent(item)) { BLE_TX_Release(item); return; }
	  res_len = parser_ProcessOne(item->Payload, item->Length, res);

	  if (res_len > 0)
	  {
		  BLE_TX_Item reply = *item;
          reply.Length = res_len;
          memcpy(reply.Payload,res,res_len);
          if (BLE_TX_Enqueue(&reply,0)!=osOK) BLE_TX_Release(item);
	  }
      else BLE_TX_Release(item);
}
/* USER CODE END FD_LOCAL_FUNCTIONS */
