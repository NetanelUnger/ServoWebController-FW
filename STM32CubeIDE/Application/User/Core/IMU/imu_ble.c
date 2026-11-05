#include "imu.h"
#include "log_module.h"
#include "ble_types.h"
#include "ble_core.h"
#include "svc_ctl.h"
#include <string.h>

static uint16_t service, raw_char, mode_char;
static volatile uint8_t requested_mode, applied_mode;
static volatile uint32_t request_id;
static volatile uint16_t subscriber = 0xffff;
static volatile uint16_t mode_owner = 0xffff;
static volatile uint8_t available;
/* Debugger-visible transport diagnostics: count, handle, length, response status. */
volatile uint32_t g_imu_control_debug[4];

static SVCCTL_EvtAckStatus_t event_handler(void *event)
{
    hci_event_pckt *p = (hci_event_pckt *)(((hci_uart_pckt *)event)->data);
    if (p->evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE) {
        hci_disconnection_complete_event_rp0 *d = (void *)p->data;
        IMU_BLE_Disconnected(d->Connection_Handle);
    }
    if (p->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        evt_blecore_aci *v = (void *)p->data;
        if (v->ecode == ACI_GATT_READ_PERMIT_REQ_VSEVT_CODE) {
            aci_gatt_read_permit_req_event_rp0 *r = (void *)v->data;
            if (r->Attribute_Handle == mode_char + 1U) {
                IMU_BLE_Applied(applied_mode);
                (void)aci_gatt_permit_read(r->Connection_Handle,0,0,0);
                return SVCCTL_EvtAckFlowEnable;
            }
        }
        if (v->ecode == ACI_GATT_WRITE_PERMIT_REQ_VSEVT_CODE) {
            aci_gatt_write_permit_req_event_rp0 *w = (void *)v->data;
            ++g_imu_control_debug[0]; g_imu_control_debug[1]=w->Attribute_Handle;
            g_imu_control_debug[2]=w->Data_Length;
            if (w->Attribute_Handle == mode_char + 1U) {
                uint8_t valid = w->Data_Length == 1U && w->Data[0] <= 2U;
                /* ATT accepts the request; the worker publishes applied/error status. */
                g_imu_control_debug[3]=aci_gatt_write_resp(w->Connection_Handle, w->Attribute_Handle,
                    valid ? 0 : 1, valid ? 0 : 0x80, w->Data_Length, w->Data);
                if (valid && g_imu_control_debug[3]==BLE_STATUS_SUCCESS) {
                    mode_owner = w->Connection_Handle;
                    requested_mode = w->Data[0]; ++request_id;
                }
                return SVCCTL_EvtAckFlowEnable;
            }
        }
        if (v->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
            aci_gatt_attribute_modified_event_rp0 *a = (void *)v->data;
            if (a->Attr_Handle == raw_char + 2U && a->Attr_Data_Length >= 2U) {
                subscriber = (a->Attr_Data[0] & 1U) ? a->Connection_Handle : 0xffff;
                if (subscriber == 0xffff) requested_mode = 0;
                ++request_id;
                return SVCCTL_EvtAckFlowEnable;
            }
        }
    }
    return SVCCTL_EvtNotAck;
}

void IMU_BLE_Init(void)
{
    Char_UUID_t uuid;
    const uint8_t base[16] = {0x59,0x2f,0xa4,0x9f,0xde,0x4e,0x2b,0xaa,
                              0x26,0x42,0x50,0xb9,0xa0,0xd4,0xba,0x91};
    available = 0;
    subscriber = 0xffff;
    mode_owner = 0xffff;
    requested_mode = applied_mode = 0; request_id = 0;
    SVCCTL_RegisterSvcHandler(event_handler);
    memcpy(uuid.Char_UUID_128, base, 16);
    if (aci_gatt_add_service(UUID_TYPE_128, (Service_UUID_t *)&uuid,
            PRIMARY_SERVICE, 6, &service) != BLE_STATUS_SUCCESS) return;
    uuid.Char_UUID_128[12] = 0xa1;
    if (aci_gatt_add_char(service, UUID_TYPE_128, &uuid, IMU_RAW_LENGTH,
            CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
            GATT_NOTIFY_ATTRIBUTE_WRITE, 0x10, CHAR_VALUE_LEN_CONSTANT,
            &raw_char) != BLE_STATUS_SUCCESS) return;
    uuid.Char_UUID_128[12] = 0xa3;
    if (aci_gatt_add_char(service, UUID_TYPE_128, &uuid, 1,
            CHAR_PROP_READ | CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
            GATT_NOTIFY_WRITE_REQ_AND_WAIT_FOR_APPL_RESP | GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP, 0x10,
            CHAR_VALUE_LEN_CONSTANT, &mode_char) != BLE_STATUS_SUCCESS) return;
    available = 1;
}
uint8_t IMU_BLE_Enabled(void) { return available && subscriber != 0xffff; }
uint8_t IMU_BLE_Mode(void) { return IMU_BLE_Enabled() ? requested_mode : 0; }
uint32_t IMU_BLE_RequestId(void) { return request_id; }
void IMU_BLE_Applied(uint8_t mode)
{
    applied_mode=mode;
    if (available) (void)aci_gatt_update_char_value(service,mode_char,0,1,&mode);
}
void IMU_BLE_Disconnected(uint16_t handle)
{
    /* A mode can be written between CCCD stop/start. Its owner must still be
     * cleared if the link drops during that interval, or the next subscriber
     * would silently restart a mode left over from the previous connection. */
    if (subscriber == handle || mode_owner == handle) {
        subscriber = mode_owner = 0xffff;
        requested_mode=0;
        ++request_id;
    }
}
uint8_t IMU_BLE_Publish(const uint8_t *data)
{
    if (!available) return 1;
    return aci_gatt_update_char_value(service, raw_char, 0, IMU_RAW_LENGTH, (uint8_t *)data);
}
