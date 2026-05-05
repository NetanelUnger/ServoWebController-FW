/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    NUS_app.h
  * @author  MCD Application Team
  * @brief   Header for NUS_app.c
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
#ifndef NUS_APP_H
#define NUS_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "ble_types.h"
#include "ble_core.h"
/* USER CODE BEGIN Includes */
#include "config.h"
#include "comm/comm_interface.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
typedef enum
{
  NUS_CONN_HANDLE_EVT,
  NUS_DISCON_HANDLE_EVT,

  /* USER CODE BEGIN Service2_OpcodeNotificationEvt_t */

  /* USER CODE END Service2_OpcodeNotificationEvt_t */

  NUS_LAST_EVT,
} NUS_APP_OpcodeNotificationEvt_t;

typedef struct
{
  NUS_APP_OpcodeNotificationEvt_t          EvtOpcode;
  uint16_t                                 ConnectionHandle;

  /* USER CODE BEGIN NUS_APP_ConnHandleNotEvt_t */

  /* USER CODE END NUS_APP_ConnHandleNotEvt_t */
} NUS_APP_ConnHandleNotEvt_t;
/* USER CODE BEGIN ET */
typedef struct
{
  packetSize_t Length;
  uint8_t *Payload;
} NUS_APP_QueueItem_t;
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
void NUS_APP_Init(void);
void NUS_APP_EvtRx(NUS_APP_ConnHandleNotEvt_t *p_Notification);
/* USER CODE BEGIN EFP */
extern void NusTxTask(NUS_APP_QueueItem_t *item);
extern void NusRxTask(NUS_APP_QueueItem_t *item);
extern tBleStatus NUS_APP_WriteAllChannelDcPayload(const uint8_t *pData, uint8_t length);
extern tBleStatus NUS_APP_SyncAllChannelDcCharacteristic(void);
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /*NUS_APP_H */
