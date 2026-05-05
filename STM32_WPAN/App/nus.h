/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    NUS.h
  * @author  MCD Application Team
  * @brief   Header for NUS.c
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef NUS_H
#define NUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "ble_types.h"
#include "ble_core.h"
#include "svc_ctl.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported defines ----------------------------------------------------------*/
/* USER CODE BEGIN ED */

/* USER CODE END ED */

/* Exported types ------------------------------------------------------------*/
typedef enum
{
  NUS_TX,
  NUS_RX,
  /* USER CODE BEGIN Service2_CharOpcode_t */
  NUS_ALL_CHANNEL_DC,

  /* USER CODE END Service2_CharOpcode_t */
  NUS_CHAROPCODE_LAST
} NUS_CharOpcode_t;

typedef enum
{
  NUS_TX_READ_EVT,
  NUS_TX_NOTIFY_ENABLED_EVT,
  NUS_TX_NOTIFY_DISABLED_EVT,
  NUS_TX_INDICATE_ENABLED_EVT,
  NUS_TX_INDICATE_DISABLED_EVT,
  NUS_RX_WRITE_EVT,
  /* USER CODE BEGIN Service2_OpcodeEvt_t */

  /* USER CODE END Service2_OpcodeEvt_t */
  NUS_BOOT_REQUEST_EVT
} NUS_OpcodeEvt_t;

typedef struct
{
  uint8_t *p_Payload;
  uint8_t Length;

  /* USER CODE BEGIN Service2_Data_t */

  /* USER CODE END Service2_Data_t */
} NUS_Data_t;

typedef struct
{
  NUS_OpcodeEvt_t       EvtOpcode;
  NUS_Data_t             DataTransfered;
  uint16_t                ConnectionHandle;
  uint16_t                AttributeHandle;
  uint8_t                 ServiceInstance;
  /* USER CODE BEGIN Service2_NotificationEvt_t */

  /* USER CODE END Service2_NotificationEvt_t */
} NUS_NotificationEvt_t;

/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Exported macros -----------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void NUS_Init(void);
void NUS_Notification(NUS_NotificationEvt_t *p_Notification);
tBleStatus NUS_UpdateValue(NUS_CharOpcode_t CharOpcode, NUS_Data_t *pData);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /*NUS_H */
