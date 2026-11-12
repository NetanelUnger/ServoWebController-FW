#include "ble_tx.h"
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ota.c
  * @author  MCD Application Team
  * @brief   ota definition.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* ST FUOTA UUIDs, project protocol v2. Legacy unverified uploads are rejected. */
#include "ota.h"
#include "ota_app.h"
#include "log_module.h"
#include <string.h>
static uint16_t service,command,data_char,status_char;
static uint8_t snapshot[20];
static SVCCTL_EvtAckStatus_t handler(void *event) {
    hci_event_pckt *p=(void *)((hci_uart_pckt *)event)->data;
    if(p->evt!=HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return SVCCTL_EvtNotAck;
    evt_blecore_aci *v=(void *)p->data;
    if(v->ecode==ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE) {
        aci_gatt_read_permit_req_event_rp0 *r=(void *)v->data;
        if(r->Attribute_Handle!=status_char+1U) return SVCCTL_EvtNotAck;
        OTA_APP_Status(snapshot);
        aci_gatt_update_char_value(service,status_char,0,20,snapshot);
        aci_gatt_allow_read(r->Connection_Handle);return SVCCTL_EvtAckFlowEnable;
    }
    if(v->ecode==ACI_GATT_PREPARE_WRITE_PERMIT_REQ_VSEVT_CODE) {
        aci_gatt_prepare_write_permit_req_event_rp0 *w=(void *)v->data;
        if(w->Attribute_Handle!=command+1U && w->Attribute_Handle!=data_char+1U) return SVCCTL_EvtNotAck;
        aci_gatt_write_resp(w->Connection_Handle,w->Attribute_Handle,1,0x06,0,NULL);
        return SVCCTL_EvtAckFlowEnable;
    }
    if(v->ecode==ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE) {
        aci_gatt_write_permit_req_event_rp0 *w=(void *)v->data;
        if(w->Attribute_Handle!=command+1U && w->Attribute_Handle!=data_char+1U) return SVCCTL_EvtNotAck;
        uint8_t e=OTA_APP_Submit(w->Connection_Handle,w->Data,w->Data_Length,w->Attribute_Handle==data_char+1U);
        aci_gatt_write_resp(w->Connection_Handle,w->Attribute_Handle,e!=0,e,w->Data_Length,w->Data);
        return SVCCTL_EvtAckFlowEnable;
    }
    return SVCCTL_EvtNotAck;
}
void OTA_Init(void) {
    Char_UUID_t uuid;
    const uint8_t svc[16]={0x8f,0xe5,0xb3,0xd5,0x2e,0x7f,0x4a,0x98,0x2a,0x48,0x7a,0xcc,0x20,0xfe,0,0};
    const uint8_t chr[16]={0x19,0xed,0x82,0xae,0xed,0x21,0x4c,0x9d,0x41,0x45,0x22,0x8e,0x22,0xfe,0,0};
    memcpy(uuid.Char_UUID_128,svc,16);
    tBleStatus s=aci_gatt_add_service(UUID_TYPE_128,(Service_UUID_t *)&uuid,PRIMARY_SERVICE,7,&service);
    if(s) goto fail;
    memcpy(uuid.Char_UUID_128,chr,16);
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,20,CHAR_PROP_WRITE,ATTR_PERMISSION_NONE,
        GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_VARIABLE,&command);
    if(s) goto fail;
    uuid.Char_UUID_128[12]=0x23;
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,20,CHAR_PROP_READ,ATTR_PERMISSION_NONE,
        GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_CONSTANT,&status_char);
    if(s) goto fail;
    uuid.Char_UUID_128[12]=0x24;
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,228,CHAR_PROP_WRITE,ATTR_PERMISSION_NONE,
        GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_VARIABLE,&data_char);
    if(s) goto fail;
    SVCCTL_RegisterSvcHandler(handler);return;
fail: LOG_INFO_BLE("OTA service failed: 0x%02X\n",s);
}
tBleStatus OTA_UpdateValue(OTA_CharOpcode_t op,OTA_Data_t *data) {
    (void)op;(void)data;return BLE_STATUS_INVALID_PARAMS;
}
