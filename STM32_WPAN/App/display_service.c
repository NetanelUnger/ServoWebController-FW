/* NatiLab display protocol v1. See display/README.md in the application Core directory. */
#include "log_module.h"
#include "ble_core.h"
#include "svc_ctl.h"
#include "display_service.h"
#include "display/display.h"

static uint16_t service_handle, command_handle;
/* 91bad4c0-b950-4226-aa2b-4ede9fa42f59, ST little-endian UUID storage. */
static const uint8_t base_uuid[16]={0x59,0x2f,0xa4,0x9f,0xde,0x4e,0x2b,0xaa,
                                  0x26,0x42,0x50,0xb9,0xc0,0xd4,0xba,0x91};
static SVCCTL_EvtAckStatus_t handler(void *event)
{
    hci_event_pckt *packet=(hci_event_pckt *)((hci_uart_pckt *)event)->data;
    if (packet->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return SVCCTL_EvtNotAck;
    evt_blecore_aci *aci=(evt_blecore_aci *)packet->data;
    if (aci->ecode == ACI_GATT_PREPARE_WRITE_PERMIT_REQ_VSEVT_CODE) {
        aci_gatt_prepare_write_permit_req_event_rp0 *write=(void *)aci->data;
        if (write->Attribute_Handle != command_handle+1U) return SVCCTL_EvtNotAck;
        /* Long/queued writes are not image fragments and must not alter the panel. */
        (void)aci_gatt_write_resp(write->Connection_Handle,write->Attribute_Handle,
                                1U,0x06U,0U,NULL);
        return SVCCTL_EvtAckFlowEnable;
    }
    if (aci->ecode != ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE) return SVCCTL_EvtNotAck;
    aci_gatt_write_permit_req_event_rp0 *write=(void *)aci->data;
    if (write->Attribute_Handle != command_handle+1U) return SVCCTL_EvtNotAck;
    uint8_t error=Display_WritePacket(write->Data,write->Data_Length);
    (void)aci_gatt_write_resp(write->Connection_Handle,write->Attribute_Handle,
                            error != 0U,error,write->Data_Length,write->Data);
    return SVCCTL_EvtAckFlowEnable;
}
void DISPLAY_SERVICE_Init(void)
{
    Char_UUID_t uuid;
    for (unsigned i=0;i<16U;++i) uuid.Char_UUID_128[i]=base_uuid[i];
    tBleStatus status=aci_gatt_add_service(UUID_TYPE_128,(Service_UUID_t *)&uuid,
                                         PRIMARY_SERVICE,3U,&service_handle);
    if (status != BLE_STATUS_SUCCESS) goto fail;
    uuid.Char_UUID_128[12]=0xc1;
    status=aci_gatt_add_char(service_handle,UUID_TYPE_128,&uuid,247U,
                            CHAR_PROP_WRITE,ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP,0x10,
                            CHAR_VALUE_LEN_VARIABLE,&command_handle);
    if (status != BLE_STATUS_SUCCESS) goto fail;
    SVCCTL_RegisterSvcHandler(handler);
    return;
fail:
    LOG_INFO_BLE("Display service initialization failed: 0x%02X\n",status);
}
