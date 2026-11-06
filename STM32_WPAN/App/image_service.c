#include "log_module.h"
#include "image_service.h"
#include "display/image_store.h"
#include "ble_core.h"
#include "svc_ctl.h"
#include "log_module.h"
static uint16_t service, command, data_char, status_char;
static uint8_t snapshot[240];
static SVCCTL_EvtAckStatus_t handler(void *event) {
    hci_event_pckt *p=(void *)((hci_uart_pckt *)event)->data;
    if(p->evt==HCI_DISCONNECTION_COMPLETE_EVT_CODE) {
        hci_disconnection_complete_event_rp0 *d=(void *)p->data;
        ImageStore_Disconnect(d->Connection_Handle);return SVCCTL_EvtNotAck;
    }
    if(p->evt!=HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return SVCCTL_EvtNotAck;
    evt_blecore_aci *v=(void *)p->data;
    if(v->ecode==ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE) {
        aci_gatt_read_permit_req_event_rp0 *r=(void *)v->data;
        if(r->Attribute_Handle!=status_char+1U) return SVCCTL_EvtNotAck;
        uint16_t n=ImageStore_Status(snapshot);
        aci_gatt_update_char_value(service,status_char,0,n,snapshot);
        aci_gatt_allow_read(r->Connection_Handle);
        return SVCCTL_EvtAckFlowEnable;
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
        uint8_t error=ImageStore_Submit(w->Connection_Handle,w->Data,w->Data_Length,w->Attribute_Handle==data_char+1U);
        aci_gatt_write_resp(w->Connection_Handle,w->Attribute_Handle,error!=0,error,w->Data_Length,w->Data);
        return SVCCTL_EvtAckFlowEnable;
    }
    return SVCCTL_EvtNotAck;
}
void IMAGE_SERVICE_Init(void) {
    ImageStore_Init();
    Char_UUID_t uuid;
    const uint8_t base[16]={0x59,0x2f,0xa4,0x9f,0xde,0x4e,0x2b,0xaa,0x26,0x42,0x50,0xb9,0xd0,0xd4,0xba,0x91};
    for(unsigned i=0;i<16;i++) uuid.Char_UUID_128[i]=base[i];
    tBleStatus s=aci_gatt_add_service(UUID_TYPE_128,(Service_UUID_t *)&uuid,PRIMARY_SERVICE,7,&service);
    if(s) goto fail;
    uuid.Char_UUID_128[12]=0xd1;
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,12,CHAR_PROP_WRITE,ATTR_PERMISSION_NONE,GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_VARIABLE,&command);
    if(s) goto fail;
    uuid.Char_UUID_128[12]=0xd2;
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,228,CHAR_PROP_WRITE,ATTR_PERMISSION_NONE,GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_VARIABLE,&data_char);
    if(s) goto fail;
    uuid.Char_UUID_128[12]=0xd3;
    s=aci_gatt_add_char(service,UUID_TYPE_128,&uuid,240,CHAR_PROP_READ,ATTR_PERMISSION_NONE,GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP,0x10,CHAR_VALUE_LEN_VARIABLE,&status_char);
    if(s) goto fail;
    SVCCTL_RegisterSvcHandler(handler);return;
fail: LOG_INFO_BLE("Image store service failed: 0x%02X\n",s);
}
