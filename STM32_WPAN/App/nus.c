/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    NUS.c
  * @author  MCD Application Team
  * @brief   NUS definition.
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
#include "log_module.h"
#include "nus.h"

/* USER CODE BEGIN Includes */
#include "app_freertos.h"
#include "nus_app.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

typedef struct{
  uint16_t  NusSvcHdle;                  /**< Nus Service Handle */
  uint16_t  TxCharHdle;                  /**< TX Characteristic Handle */
  uint16_t  RxCharHdle;                  /**< RX Characteristic Handle */
/* USER CODE BEGIN Context */
  /* Place holder for Characteristic Descriptors Handle*/
  uint16_t  AllChannelDcCharHdle;        /**< All-channel direct-control Characteristic Handle */

/* USER CODE END Context */
}NUS_Context_t;

/* Private defines -----------------------------------------------------------*/
#define UUID_128_SUPPORTED  1

#if (UUID_128_SUPPORTED == 1)
#define BM_UUID_LENGTH  UUID_TYPE_128
#else
#define BM_UUID_LENGTH  UUID_TYPE_16
#endif

#define BM_REQ_CHAR_SIZE    (3)

/* USER CODE BEGIN PD */
#define NUS_ALL_CHANNEL_DC_ATT_ERROR 0x80U

/* USER CODE END PD */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/* Private macros ------------------------------------------------------------*/
#define CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET         2
#define CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET              1
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
static const uint16_t SizeTx = 256;
static const uint16_t SizeRx = 256;
static const uint16_t SizeAllChannelDc = 32U;

static NUS_Context_t NUS_Context;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static SVCCTL_EvtAckStatus_t NUS_EventHandler(void *p_pckt);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
/* USER CODE BEGIN PFD */
static void NUS_PermitRead(uint16_t connection,uint16_t attribute,uint8_t servo) {
    BLE_TX_AttTicket ticket;
    if (!BLE_TX_AttBegin(connection,attribute,0U,NULL,0U,&ticket)) return;
    tBleStatus result=servo ? NUS_APP_RefreshAllChannelDcRead() : BLE_STATUS_SUCCESS;
    BLE_TX_AttCommit(&ticket,result==BLE_STATUS_SUCCESS ? 0U : 1U,
                     result==BLE_STATUS_SUCCESS ? 0U : NUS_ALL_CHANNEL_DC_ATT_ERROR);
}
static void NUS_PermitWrite(uint16_t connection,uint16_t attribute,const uint8_t *data,uint16_t length,uint8_t servo) {
    BLE_TX_AttTicket ticket;
    if (!BLE_TX_AttBegin(connection,attribute,1U,data,length,&ticket)) return;
    uint8_t accepted=servo ? NUS_APP_WriteAllChannelDcPayload(data,(uint8_t)length)==BLE_STATUS_SUCCESS :
        NUS_APP_EnqueueRX(connection,data,length)==osOK;
    BLE_TX_AttCommit(&ticket,accepted ? 0U : 1U,accepted ? 0U : servo ? NUS_ALL_CHANNEL_DC_ATT_ERROR : 0x11U);
    if (servo && accepted) (void)NUS_APP_SyncAllChannelDcCharacteristic();
}

/* USER CODE END PFD */

/* Private functions ----------------------------------------------------------*/

#define COPY_UUID_128(uuid_struct, uuid_15, uuid_14, uuid_13, uuid_12, uuid_11, uuid_10, uuid_9, uuid_8, uuid_7, uuid_6, uuid_5, uuid_4, uuid_3, uuid_2, uuid_1, uuid_0) \
do {\
    uuid_struct[0] = uuid_0; uuid_struct[1] = uuid_1; uuid_struct[2] = uuid_2; uuid_struct[3] = uuid_3; \
    uuid_struct[4] = uuid_4; uuid_struct[5] = uuid_5; uuid_struct[6] = uuid_6; uuid_struct[7] = uuid_7; \
    uuid_struct[8] = uuid_8; uuid_struct[9] = uuid_9; uuid_struct[10] = uuid_10; uuid_struct[11] = uuid_11; \
    uuid_struct[12] = uuid_12; uuid_struct[13] = uuid_13; uuid_struct[14] = uuid_14; uuid_struct[15] = uuid_15; \
}while(0)

/*
 The following 128bits UUIDs have been generated from the random UUID
 generator:
 6E400001B5A3F393E0A9E50E24DCCA9E: Service 128bits UUID
 6E400003B5A3F393E0A9E50E24DCCA9E: Characteristic 128bits UUID
 6E400002B5A3F393E0A9E50E24DCCA9E: Characteristic 128bits UUID
 6E400004B5A3F393E0A9E50E24DCCA9E: Characteristic 128bits UUID
 */
#define COPY_NUS_UUID(uuid_struct)       COPY_UUID_128(uuid_struct,0x6e,0x40,0x00,0x01,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e)
#define COPY_TX_UUID(uuid_struct)       COPY_UUID_128(uuid_struct,0x6e,0x40,0x00,0x03,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e)
#define COPY_RX_UUID(uuid_struct)       COPY_UUID_128(uuid_struct,0x6e,0x40,0x00,0x02,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e)
#define COPY_ALL_CHANNEL_DC_UUID(uuid_struct)       COPY_UUID_128(uuid_struct,0x6e,0x40,0x00,0x04,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e)

/* USER CODE BEGIN PF */

/* USER CODE END PF */

/**
 * @brief  Event handler
 * @param  p_Event: Address of the buffer holding the p_Event
 * @retval Ack: Return whether the p_Event has been managed or not
 */
static SVCCTL_EvtAckStatus_t NUS_EventHandler(void *p_Event)
{
  SVCCTL_EvtAckStatus_t return_value;
  hci_event_pckt *p_event_pckt;
  evt_blecore_aci *p_blecore_evt;
  aci_gatt_attribute_modified_event_rp0 *p_attribute_modified;
  aci_gatt_write_permit_req_event_rp0   *p_write_perm_req;
  aci_gatt_read_permit_req_event_rp0    *p_read_req;
  NUS_NotificationEvt_t                 notification;
  /* USER CODE BEGIN Service2_EventHandler_1 */

  /* USER CODE END Service2_EventHandler_1 */

  return_value = SVCCTL_EvtNotAck;
  p_event_pckt = (hci_event_pckt *)(((hci_uart_pckt*)p_Event)->data);

  switch(p_event_pckt->evt)
  {
    case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
      p_blecore_evt = (evt_blecore_aci*)p_event_pckt->data;
      switch(p_blecore_evt->ecode)
      {
        case ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE:
        {
          /* USER CODE BEGIN EVT_BLUE_GATT_ATTRIBUTE_MODIFIED_BEGIN */

          /* USER CODE END EVT_BLUE_GATT_ATTRIBUTE_MODIFIED_BEGIN */
          p_attribute_modified = (aci_gatt_attribute_modified_event_rp0*)p_blecore_evt->data;
          notification.ConnectionHandle         = p_attribute_modified->Connection_Handle;
          notification.AttributeHandle          = p_attribute_modified->Attr_Handle;
          notification.DataTransfered.Length    = p_attribute_modified->Attr_Data_Length;
          notification.DataTransfered.p_Payload = p_attribute_modified->Attr_Data;
          if(p_attribute_modified->Attr_Handle == (NUS_Context.TxCharHdle + CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;
            /* USER CODE BEGIN Service2_Char_1 */

            /* USER CODE END Service2_Char_1 */

            switch(p_attribute_modified->Attr_Data[0])
            {
              /* USER CODE BEGIN Service2_Char_1_attribute_modified  */

              /* USER CODE END Service2_Char_1_attribute_modified  */

              /* Disabled Notification and Indication management */
              case (0x00):
                /* USER CODE BEGIN Service2_Char_1_Disabled_BEGIN  */

                /* USER CODE END Service2_Char_1_Disabled_BEGIN  */
                notification.EvtOpcode = NUS_TX_NOTIFY_DISABLED_EVT;
                NUS_Notification(&notification);
                notification.EvtOpcode = NUS_TX_INDICATE_DISABLED_EVT;
                NUS_Notification(&notification);
                /* USER CODE BEGIN Service2_Char_1_Disabled_END */

                /* USER CODE END Service2_Char_1_Disabled_END */
                break;

              /* Enabled Notification management */
              case GATT_CHAR_UPDATE_SEND_NOTIFICATION:
                /* USER CODE BEGIN Service2_Char_1_COMSVC_Notification_BEGIN */

                /* USER CODE END Service2_Char_1_COMSVC_Notification_BEGIN */
                notification.EvtOpcode = NUS_TX_NOTIFY_ENABLED_EVT;
                NUS_Notification(&notification);
                /* USER CODE BEGIN Service2_Char_1_COMSVC_Notification_END */

                /* USER CODE END Service2_Char_1_COMSVC_Notification_END */
                break;

              /* Enabled Indication management */
              case GATT_CHAR_UPDATE_SEND_INDICATION:
                /* USER CODE BEGIN Service2_Char_1_COMSVC_Indication_BEGIN */

                /* USER CODE END Service2_Char_1_COMSVC_Indication_BEGIN */
                notification.EvtOpcode = NUS_TX_INDICATE_ENABLED_EVT;
                NUS_Notification(&notification);
                /* USER CODE BEGIN Service2_Char_1_COMSVC_Indication_END */

                /* USER CODE END Service2_Char_1_COMSVC_Indication_END */
                break;

              default:
                /* USER CODE BEGIN Service2_Char_1_default */

                /* USER CODE END Service2_Char_1_default */
                break;
            }
          }
          else if(p_attribute_modified->Attr_Handle == (NUS_Context.AllChannelDcCharHdle + CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;

            switch(p_attribute_modified->Attr_Data[0])
            {
              case (0x00):
                notification.EvtOpcode = NUS_ALL_CHANNEL_DC_NOTIFY_DISABLED_EVT;
                NUS_Notification(&notification);
                break;

              case GATT_CHAR_UPDATE_SEND_NOTIFICATION:
                notification.EvtOpcode = NUS_ALL_CHANNEL_DC_NOTIFY_ENABLED_EVT;
                NUS_Notification(&notification);
                break;

              default:
                break;
            }
          }

          /* USER CODE BEGIN EVT_BLUE_GATT_ATTRIBUTE_MODIFIED_END */

          /* USER CODE END EVT_BLUE_GATT_ATTRIBUTE_MODIFIED_END */
          break;/* ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE */
        }
        case ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE :
        {
          /* USER CODE BEGIN EVT_BLUE_GATT_READ_PERMIT_REQ_BEGIN */

          /* USER CODE END EVT_BLUE_GATT_READ_PERMIT_REQ_BEGIN */
          p_read_req = (aci_gatt_read_permit_req_event_rp0*)p_blecore_evt->data;
          if(p_read_req->Attribute_Handle == (NUS_Context.TxCharHdle + CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;
            /*USER CODE BEGIN Service2_Char_1_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE_1 */

            /*USER CODE END Service2_Char_1_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE_1*/

            /*USER CODE BEGIN Service2_Char_1_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE_2 */
            NUS_PermitRead(p_read_req->Connection_Handle,p_read_req->Attribute_Handle,0U);
            /*USER CODE END Service2_Char_1_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE_2*/
          }
          else if(p_read_req->Attribute_Handle == (NUS_Context.AllChannelDcCharHdle + CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;
            /* USER CODE BEGIN Service2_Char_3_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE */
            NUS_PermitRead(p_read_req->Connection_Handle,p_read_req->Attribute_Handle,1U);
            /* USER CODE END Service2_Char_3_ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE */
          }

          /* USER CODE BEGIN EVT_BLUE_GATT_READ_PERMIT_REQ_END */

          /* USER CODE END EVT_BLUE_GATT_READ_PERMIT_REQ_END */
          break;/* ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE */
        }
        case ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE:
        {
          /* USER CODE BEGIN EVT_BLUE_GATT_WRITE_PERMIT_REQ_BEGIN */

          /* USER CODE END EVT_BLUE_GATT_WRITE_PERMIT_REQ_BEGIN */
          p_write_perm_req = (aci_gatt_write_permit_req_event_rp0*)p_blecore_evt->data;
          if(p_write_perm_req->Attribute_Handle == (NUS_Context.RxCharHdle + CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;
            /*USER CODE BEGIN Service2_Char_2_ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE */
            NUS_PermitWrite(p_write_perm_req->Connection_Handle,p_write_perm_req->Attribute_Handle,
                            p_write_perm_req->Data,p_write_perm_req->Data_Length,0U);
            /*USER CODE END Service2_Char_2_ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE*/
          }
          else if(p_write_perm_req->Attribute_Handle == (NUS_Context.AllChannelDcCharHdle + CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET))
          {
            return_value = SVCCTL_EvtAckFlowEnable;
            /* USER CODE BEGIN Service2_Char_3_ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE */
            NUS_PermitWrite(p_write_perm_req->Connection_Handle,p_write_perm_req->Attribute_Handle,
                            p_write_perm_req->Data,p_write_perm_req->Data_Length,1U);
            /* USER CODE END Service2_Char_3_ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE */
          }

          /* USER CODE BEGIN EVT_BLUE_GATT_WRITE_PERMIT_REQ_END */

          /* USER CODE END EVT_BLUE_GATT_WRITE_PERMIT_REQ_END */
          break;/* ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE */
        }
        case ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE:
        {
          aci_gatt_tx_pool_available_event_rp0 *p_tx_pool_available_event;
          p_tx_pool_available_event = (aci_gatt_tx_pool_available_event_rp0 *) p_blecore_evt->data;
          UNUSED(p_tx_pool_available_event);

          /* USER CODE BEGIN ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE */

          /* USER CODE END ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE */
          break;/* ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE*/
        }
        case ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE:
        {
          aci_att_exchange_mtu_resp_event_rp0 *p_exchange_mtu;
          p_exchange_mtu = (aci_att_exchange_mtu_resp_event_rp0 *)  p_blecore_evt->data;
          UNUSED(p_exchange_mtu);

          /* USER CODE BEGIN ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE */

          /* USER CODE END ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE */
          break;/* ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE */
        }
        case ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE:
        {
          aci_gatt_server_confirmation_event_rp0 *p_server_confirmation;
          p_server_confirmation = (aci_gatt_server_confirmation_event_rp0 *)  p_blecore_evt->data;
          UNUSED(p_server_confirmation);

          /* USER CODE BEGIN ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE */

          /* USER CODE END ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE */
          break;/* ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE */
        }
        /* USER CODE BEGIN BLECORE_EVT */

        /* USER CODE END BLECORE_EVT */
        default:
          /* USER CODE BEGIN EVT_DEFAULT */

          /* USER CODE END EVT_DEFAULT */
          break;
      }
      /* USER CODE BEGIN EVT_VENDOR */

      /* USER CODE END EVT_VENDOR */
      break; /* HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE */

      /* USER CODE BEGIN EVENT_PCKT_CASES */

      /* USER CODE END EVENT_PCKT_CASES */

    default:
      /* USER CODE BEGIN EVENT_PCKT */

      /* USER CODE END EVENT_PCKT */
      break;
  }

  /* USER CODE BEGIN Service2_EventHandler_2 */

  /* USER CODE END Service2_EventHandler_2 */

  return(return_value);
}/* end NUS_EventHandler */

/* Public functions ----------------------------------------------------------*/

/**
 * @brief  Service initialization
 * @param  None
 * @retval None
 */
void NUS_Init(void)
{
  Char_UUID_t  uuid;
  tBleStatus ret;
  uint8_t max_attr_record;

  /* USER CODE BEGIN SVCCTL_InitService2Svc_1 */

  /* USER CODE END SVCCTL_InitService2Svc_1 */

  /**
   *  Register the event handler to the BLE controller
   */
  SVCCTL_RegisterSvcHandler(NUS_EventHandler);

  /**
   * NUS
   *
   * Max_Attribute_Records = 1 + 2*2 + 1*no_of_char_with_notify_or_indicate_property + 1*no_of_char_with_broadcast_property
   * service_max_attribute_record = 1 for NUS +
   *                                2 for TX +
   *                                2 for RX +
   *                                2 for ALL_CHANNEL_DC +
   *                                1 for TX configuration descriptor +
   *                                1 for ALL_CHANNEL_DC configuration descriptor +
   *                              = 9
   * This value doesn't take into account number of descriptors manually added
   * In case of descriptors added, please update the max_attr_record value accordingly in the next SVCCTL_InitService User Section
   */
  max_attr_record = 9;

  /* USER CODE BEGIN SVCCTL_InitService */
  /* max_attr_record to be updated if descriptors have been added */

  /* USER CODE END SVCCTL_InitService */

  COPY_NUS_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_service(UUID_TYPE_128,
                             (Service_UUID_t *) &uuid,
                             PRIMARY_SERVICE,
                             max_attr_record,
                             &(NUS_Context.NusSvcHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("  Fail   : aci_gatt_add_service command: NUS, error code: 0x%02X\n", ret);
  }
  else
  {
    LOG_INFO_BLE("  Success: aci_gatt_add_service command: NusSvcHdle = 0x%04X\n",NUS_Context.NusSvcHdle);
  }

  /* USER CODE BEGIN SVCCTL_InitService_2 */

  /* USER CODE END SVCCTL_InitService_2 */

  /**
   * TX
   */
  COPY_TX_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_char(NUS_Context.NusSvcHdle,
                          UUID_TYPE_128,
                          (Char_UUID_t *) &uuid,
                          SizeTx,
                          CHAR_PROP_READ | CHAR_PROP_NOTIFY | CHAR_PROP_INDICATE,
                          ATTR_PERMISSION_AUTHEN_READ,
                          GATT_NOTIFY_ATTRIBUTE_WRITE | GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP | GATT_NOTIFY_NOTIFICATION_COMPLETION,
                          0x10,
                          CHAR_VALUE_LEN_VARIABLE,
                          &(NUS_Context.TxCharHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("  Fail   : aci_gatt_add_char command   : TX, error code: 0x%02X\n", ret);
  }
  else
  {
    LOG_INFO_BLE("  Success: aci_gatt_add_char command   : TxCharHdle = 0x%04X\n",NUS_Context.TxCharHdle);
  }

  /* USER CODE BEGIN SVCCTL_InitService2Char1 */
  /* Place holder for Characteristic Descriptors */

  /* USER CODE END SVCCTL_InitService2Char1 */

  /**
   * RX
   */
  COPY_RX_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_char(NUS_Context.NusSvcHdle,
                          UUID_TYPE_128,
                          (Char_UUID_t *) &uuid,
                          SizeRx,
                          CHAR_PROP_WRITE,
                          ATTR_PERMISSION_NONE,
                          GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,
                          0x10,
                          CHAR_VALUE_LEN_VARIABLE,
                          &(NUS_Context.RxCharHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("  Fail   : aci_gatt_add_char command   : RX, error code: 0x%02X\n", ret);
  }
  else
  {
    LOG_INFO_BLE("  Success: aci_gatt_add_char command   : RxCharHdle = 0x%04X\n",NUS_Context.RxCharHdle);
  }

  /* USER CODE BEGIN SVCCTL_InitService2Char2 */
  /* Place holder for Characteristic Descriptors */

  /* USER CODE END SVCCTL_InitService2Char2 */

  /**
   * ALL_CHANNEL_DC
   */
  COPY_ALL_CHANNEL_DC_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_char(NUS_Context.NusSvcHdle,
                          UUID_TYPE_128,
                          (Char_UUID_t *) &uuid,
                          SizeAllChannelDc,
                          CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_NOTIFY,
                          ATTR_PERMISSION_NONE,
                          GATT_NOTIFY_ATTRIBUTE_WRITE | GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP | GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP | GATT_NOTIFY_NOTIFICATION_COMPLETION,
                          0x10,
                          CHAR_VALUE_LEN_CONSTANT,
                          &(NUS_Context.AllChannelDcCharHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("  Fail   : aci_gatt_add_char command   : ALL_CHANNEL_DC, error code: 0x%02X\n", ret);
  }
  else
  {
    LOG_INFO_BLE("  Success: aci_gatt_add_char command   : AllChannelDcCharHdle = 0x%04X\n",NUS_Context.AllChannelDcCharHdle);
  }

  /* USER CODE BEGIN SVCCTL_InitService2Char3 */
  /* Place holder for Characteristic Descriptors */

  /* USER CODE END SVCCTL_InitService2Char3 */

  /* USER CODE BEGIN SVCCTL_InitService2Svc_2 */
  BLE_TX_Register(BLE_TX_REPLY,NUS_Context.NusSvcHdle,NUS_Context.TxCharHdle);
  BLE_TX_Register(BLE_TX_SERVO,NUS_Context.NusSvcHdle,NUS_Context.AllChannelDcCharHdle);

  /* USER CODE END SVCCTL_InitService2Svc_2 */

  return;
}

/**
 * @brief  Characteristic update
 * @param  CharOpcode: Characteristic identifier
 * @param  pData: Structure holding data to update
 *
 */
tBleStatus NUS_UpdateValue(NUS_CharOpcode_t CharOpcode, NUS_Data_t *pData)
{
  if (!pData || !pData->p_Payload) return BLE_STATUS_INVALID_PARAMS;
  if (CharOpcode==NUS_TX) return BLE_TX_Ordered(BLE_TX_REPLY,pData->p_Payload,pData->Length);
  uint16_t handle=CharOpcode==NUS_ALL_CHANNEL_DC ? NUS_Context.AllChannelDcCharHdle : NUS_Context.RxCharHdle;
  return BLE_TX_Local(NUS_Context.NusSvcHdle,handle,pData->p_Payload,pData->Length);
}
