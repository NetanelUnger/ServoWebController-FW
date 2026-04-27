/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    DIS_app.c
  * @author  MCD Application Team
  * @brief   DIS_app application definition.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "dis_app.h"
#include "dis.h"
#include "stm32_rtos.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
	const uint8_t ManufacturerNameString[] = BLE_ManufacturerNameString;
	const uint8_t ModelNumberString [] = BLE_ModelNumberString;
	const uint8_t SerialNumberString[] = BLE_SerialNumberString;
	const uint8_t HardwareRevisionString[] = BLE_HardwareRevisionString;
	const uint8_t FirmwareRevisionString[] = BLE_FirmwareRevisionString;
	const uint8_t SoftwareRevisionString[] = BLE_SoftwareRevisionString;
	const uint8_t SystemID[] = BLE_SystemID;
/* USER CODE END PTD */

typedef enum
{
  /* USER CODE BEGIN Service3_APP_SendInformation_t */

  /* USER CODE END Service3_APP_SendInformation_t */
  DIS_APP_SENDINFORMATION_LAST
} DIS_APP_SendInformation_t;

typedef struct
{
  /* USER CODE BEGIN Service3_APP_Context_t */

  /* USER CODE END Service3_APP_Context_t */
  uint16_t              ConnectionHandle;
} DIS_APP_Context_t;

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static DIS_APP_Context_t DIS_APP_Context;

uint8_t a_DIS_UpdateCharData[247];

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void DIS_Notification(DIS_NotificationEvt_t *p_Notification)
{
  /* USER CODE BEGIN Service3_Notification_1 */

  /* USER CODE END Service3_Notification_1 */
  switch(p_Notification->EvtOpcode)
  {
    /* USER CODE BEGIN Service3_Notification_Service3_EvtOpcode */

    /* USER CODE END Service3_Notification_Service3_EvtOpcode */

    case DIS_MANS_READ_EVT:
      /* USER CODE BEGIN Service3Char1_READ_EVT */

      /* USER CODE END Service3Char1_READ_EVT */
      break;

    case DIS_MONS_READ_EVT:
      /* USER CODE BEGIN Service3Char2_READ_EVT */

      /* USER CODE END Service3Char2_READ_EVT */
      break;

    case DIS_SNS_READ_EVT:
      /* USER CODE BEGIN Service3Char3_READ_EVT */

      /* USER CODE END Service3Char3_READ_EVT */
      break;

    case DIS_HRS_READ_EVT:
      /* USER CODE BEGIN Service3Char4_READ_EVT */

      /* USER CODE END Service3Char4_READ_EVT */
      break;

    case DIS_FRS_READ_EVT:
      /* USER CODE BEGIN Service3Char5_READ_EVT */

      /* USER CODE END Service3Char5_READ_EVT */
      break;

    case DIS_SRS_READ_EVT:
      /* USER CODE BEGIN Service3Char6_READ_EVT */

      /* USER CODE END Service3Char6_READ_EVT */
      break;

    case DIS_SYID_READ_EVT:
      /* USER CODE BEGIN Service3Char7_READ_EVT */

      /* USER CODE END Service3Char7_READ_EVT */
      break;

    default:
      /* USER CODE BEGIN Service3_Notification_default */

      /* USER CODE END Service3_Notification_default */
      break;
  }
  /* USER CODE BEGIN Service3_Notification_2 */

  /* USER CODE END Service3_Notification_2 */
  return;
}

void DIS_APP_EvtRx(DIS_APP_ConnHandleNotEvt_t *p_Notification)
{
  /* USER CODE BEGIN Service3_APP_EvtRx_1 */

  /* USER CODE END Service3_APP_EvtRx_1 */

  switch(p_Notification->EvtOpcode)
  {
    /* USER CODE BEGIN Service3_APP_EvtRx_Service3_EvtOpcode */

    /* USER CODE END Service3_APP_EvtRx_Service3_EvtOpcode */
    case DIS_CONN_HANDLE_EVT :
      /* USER CODE BEGIN Service3_APP_CONN_HANDLE_EVT */

      /* USER CODE END Service3_APP_CONN_HANDLE_EVT */
      break;

    case DIS_DISCON_HANDLE_EVT :
      /* USER CODE BEGIN Service3_APP_DISCON_HANDLE_EVT */

      /* USER CODE END Service3_APP_DISCON_HANDLE_EVT */
      break;

    default:
      /* USER CODE BEGIN Service3_APP_EvtRx_default */

      /* USER CODE END Service3_APP_EvtRx_default */
      break;
  }

  /* USER CODE BEGIN Service3_APP_EvtRx_2 */

  /* USER CODE END Service3_APP_EvtRx_2 */

  return;
}

void DIS_APP_Init(void)
{
  UNUSED(DIS_APP_Context);
  DIS_Init();

  /* USER CODE BEGIN Service3_APP_Init */
	DIS_Data_t dis_information_data;

	static const uint16_t SizeMans = 6;
	static const uint16_t SizeMons = 4;
	static const uint16_t SizeSns = 7;
	static const uint16_t SizeHrs = 4;
	static const uint16_t SizeFrs = 4;
	static const uint16_t SizeSrs = 4;
	static const uint16_t SizeSyid = 7;

	dis_information_data.p_Payload = (uint8_t*)ManufacturerNameString;
	dis_information_data.Length = SizeMans;
	DIS_UpdateValue(DIS_MANS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)ModelNumberString;
	dis_information_data.Length = SizeMons;
	DIS_UpdateValue(DIS_MONS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)SerialNumberString;
	dis_information_data.Length = SizeSns;
	DIS_UpdateValue(DIS_SNS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)HardwareRevisionString;
	dis_information_data.Length = SizeHrs;
	DIS_UpdateValue(DIS_HRS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)FirmwareRevisionString;
	dis_information_data.Length = SizeFrs;
	DIS_UpdateValue(DIS_FRS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)SoftwareRevisionString;
	dis_information_data.Length = SizeSrs;
	DIS_UpdateValue(DIS_SRS, &dis_information_data);

	dis_information_data.p_Payload = (uint8_t*)SystemID;
	dis_information_data.Length = SizeSyid;
	DIS_UpdateValue(DIS_SYID, &dis_information_data);

  /* USER CODE END Service3_APP_Init */
  return;
}

/* USER CODE BEGIN FD */

/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/

/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */

/* USER CODE END FD_LOCAL_FUNCTIONS */
