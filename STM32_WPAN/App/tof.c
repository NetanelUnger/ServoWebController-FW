#include "ble_tx.h"
/**
  ******************************************************************************
  * @file    tof.c
  * @brief   BLE service definition for VL53L5CX distance-map notifications.
  ******************************************************************************
  */

#include "log_module.h"
#include "tof.h"

typedef struct
{
  uint16_t TofSvcHdle;
  uint16_t DistanceCharHdle;
} TOF_Context_t;

#define TOF_DISTANCE_MAP_LEN 64U
#define CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET 2U

static TOF_Context_t TOF_Context;

static SVCCTL_EvtAckStatus_t TOF_EventHandler(void *p_Event);

#define COPY_UUID_128(uuid_struct, uuid_15, uuid_14, uuid_13, uuid_12, uuid_11, uuid_10, uuid_9, uuid_8, uuid_7, uuid_6, uuid_5, uuid_4, uuid_3, uuid_2, uuid_1, uuid_0) \
do {\
    uuid_struct[0] = uuid_0; uuid_struct[1] = uuid_1; uuid_struct[2] = uuid_2; uuid_struct[3] = uuid_3; \
    uuid_struct[4] = uuid_4; uuid_struct[5] = uuid_5; uuid_struct[6] = uuid_6; uuid_struct[7] = uuid_7; \
    uuid_struct[8] = uuid_8; uuid_struct[9] = uuid_9; uuid_struct[10] = uuid_10; uuid_struct[11] = uuid_11; \
    uuid_struct[12] = uuid_12; uuid_struct[13] = uuid_13; uuid_struct[14] = uuid_14; uuid_struct[15] = uuid_15; \
} while (0)

#define COPY_TOF_UUID(uuid_struct) \
  COPY_UUID_128(uuid_struct, 0x91, 0xBA, 0xD4, 0x92, 0xB9, 0x50, 0x42, 0x26, 0xAA, 0x2B, 0x4E, 0xDE, 0x9F, 0xA4, 0x2F, 0x59)
#define COPY_TOF_DISTANCE_UUID(uuid_struct) \
  COPY_UUID_128(uuid_struct, 0x91, 0xBA, 0xD4, 0x93, 0xB9, 0x50, 0x42, 0x26, 0xAA, 0x2B, 0x4E, 0xDE, 0x9F, 0xA4, 0x2F, 0x59)

static SVCCTL_EvtAckStatus_t TOF_EventHandler(void *p_Event)
{
  SVCCTL_EvtAckStatus_t return_value;
  hci_event_pckt *p_event_pckt;
  evt_blecore_aci *p_blecore_evt;
  aci_gatt_attribute_modified_event_rp0 *p_attribute_modified;
  TOF_NotificationEvt_t notification;

  return_value = SVCCTL_EvtNotAck;
  p_event_pckt = (hci_event_pckt *)(((hci_uart_pckt *)p_Event)->data);

  if (p_event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE)
  {
    return return_value;
  }

  p_blecore_evt = (evt_blecore_aci *)p_event_pckt->data;
  if (p_blecore_evt->ecode != ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE)
  {
    return return_value;
  }

  p_attribute_modified = (aci_gatt_attribute_modified_event_rp0 *)p_blecore_evt->data;
  if (p_attribute_modified->Attr_Handle != (TOF_Context.DistanceCharHdle + CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET))
  {
    return return_value;
  }

  return_value = SVCCTL_EvtAckFlowEnable;
  notification.ConnectionHandle = p_attribute_modified->Connection_Handle;
  notification.AttributeHandle = p_attribute_modified->Attr_Handle;
  notification.DataTransfered.Length = p_attribute_modified->Attr_Data_Length;
  notification.DataTransfered.p_Payload = p_attribute_modified->Attr_Data;

  switch (p_attribute_modified->Attr_Data[0])
  {
    case 0x00:
      notification.EvtOpcode = TOF_DISTANCE_NOTIFY_DISABLED_EVT;
      TOF_Notification(&notification);
      break;

    case GATT_CHAR_UPDATE_SEND_NOTIFICATION:
      notification.EvtOpcode = TOF_DISTANCE_NOTIFY_ENABLED_EVT;
      TOF_Notification(&notification);
      break;

    default:
      break;
  }

  return return_value;
}

void TOF_Init(void)
{
  Char_UUID_t uuid;
  tBleStatus ret;

  SVCCTL_RegisterSvcHandler(TOF_EventHandler);

  COPY_TOF_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_service(UUID_TYPE_128,
                             (Service_UUID_t *)&uuid,
                             PRIMARY_SERVICE,
                             4U,
                             &(TOF_Context.TofSvcHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("Fail: TOF service add 0x%02X\n", ret);
  }

  COPY_TOF_DISTANCE_UUID(uuid.Char_UUID_128);
  ret = aci_gatt_add_char(TOF_Context.TofSvcHdle,
                          UUID_TYPE_128,
                          (Char_UUID_t *)&uuid,
                          TOF_DISTANCE_MAP_LEN,
                          CHAR_PROP_NOTIFY,
                          ATTR_PERMISSION_NONE,
                          GATT_NOTIFY_ATTRIBUTE_WRITE | GATT_NOTIFY_NOTIFICATION_COMPLETION,
                          0x10,
                          CHAR_VALUE_LEN_VARIABLE,
                          &(TOF_Context.DistanceCharHdle));
  if (ret != BLE_STATUS_SUCCESS)
  {
    LOG_INFO_BLE("Fail: TOF char add 0x%02X\n", ret);
  }
  BLE_TX_Register(BLE_TX_TOF,TOF_Context.TofSvcHdle,TOF_Context.DistanceCharHdle);
}

tBleStatus TOF_UpdateValue(TOF_CharOpcode_t CharOpcode, TOF_Data_t *pData)
{
  if ((CharOpcode != TOF_DISTANCE_MAP) || (pData == NULL) || (pData->p_Payload == NULL))
  {
    return BLE_STATUS_INVALID_PARAMS;
  }

  return BLE_TX_Latest(BLE_TX_TOF,pData->p_Payload,pData->Length);
}
