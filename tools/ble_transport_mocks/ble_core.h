#ifndef TEST_BLE_CORE_H
#define TEST_BLE_CORE_H
#include "ble_types.h"
/* Layouts match the checked-in STM32WBA ble_types.h / HCI packet headers. */
#pragma pack(push,1)
typedef struct { uint8_t type; uint8_t data[1]; } hci_uart_pckt;
typedef struct { uint8_t evt, plen; uint8_t data[1]; } hci_event_pckt;
typedef struct { uint16_t ecode; uint8_t data[1]; } evt_blecore_aci;
typedef struct { uint16_t Attr_Handle; } aci_gatt_notification_complete_event_rp0;
typedef struct { uint16_t Connection_Handle; } aci_gatt_server_confirmation_event_rp0;
typedef struct { uint16_t Connection_Handle, Available_Buffers; } aci_gatt_tx_pool_available_event_rp0;
typedef struct { uint16_t Connection_Handle, Attr_Handle, Offset, Attr_Data_Length; uint8_t Attr_Data[245]; } aci_gatt_attribute_modified_event_rp0;
typedef struct { uint16_t Connection_Handle, Server_RX_MTU; } aci_att_exchange_mtu_resp_event_rp0;
#pragma pack(pop)
#define HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE 0xffU
#define ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE 0x0c03U
#define ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE 0x0c01U
#define ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE 0x0c16U
#define ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE 0x0c17U
#define ACI_GATT_NOTIFICATION_COMPLETE_VSEVT_CODE 0x0c1bU
#define GATT_CHAR_UPDATE_LOCAL_ONLY 0U
tBleStatus aci_gatt_update_char_value_ext(uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,uint16_t,uint8_t,const uint8_t *);
tBleStatus aci_gatt_permit_read(uint16_t,uint8_t,uint8_t,uint16_t);
#define aci_gatt_write_resp aci_gatt_permit_write
tBleStatus aci_gatt_permit_write(uint16_t,uint16_t,uint8_t,uint8_t,uint8_t,const uint8_t *);
tBleStatus aci_gap_terminate(uint16_t,uint8_t);
#endif
