#include "ble_tx.h"
#include "adc_ble.h"
#include "adc_dma.h"
#include "battery_calibration.h"
#include "app_conf.h"
#include "cmsis_os2.h"
#include "ble_types.h"
#include "ble_core.h"
#include "svc_ctl.h"
#include <string.h>

static uint16_t service, raw_char;
static uint8_t available;
static volatile uint16_t subscribers[CFG_BLE_NUM_LINK];
static uint32_t last_sample;
static uint8_t streaming;

static void subscribe(uint16_t handle, uint8_t enabled)
{
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
        if (subscribers[i] == handle) {
            if (!enabled) subscribers[i] = 0xffff;
            return;
        }
    }
    if (enabled) {
        for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
            if (subscribers[i] == 0xffff) { subscribers[i] = handle; return; }
        }
    }
}

static SVCCTL_EvtAckStatus_t event_handler(void *event)
{
    hci_event_pckt *p = (void *)(((hci_uart_pckt *)event)->data);
    if (p->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        evt_blecore_aci *v = (void *)p->data;
        if (v->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
            aci_gatt_attribute_modified_event_rp0 *a = (void *)v->data;
            if (a->Attr_Handle == raw_char + 2U && a->Attr_Data_Length >= 2U) {
                subscribe(a->Connection_Handle, a->Attr_Data[0] & 1U);
                return SVCCTL_EvtAckFlowEnable;
            }
        }
    }
    return SVCCTL_EvtNotAck;
}

void ADC_BLE_Init(void)
{
    Char_UUID_t uuid;
    const uint8_t base[16] = {0x59,0x2f,0xa4,0x9f,0xde,0x4e,0x2b,0xaa,
                              0x26,0x42,0x50,0xb9,0xb0,0xd4,0xba,0x91};
    const uint8_t empty[ADC_BLE_RAW_LENGTH] = {2,0,0,0,0,0,0,0,0,0};
    available = streaming = 0;
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) subscribers[i] = 0xffff;
    SVCCTL_RegisterSvcHandler(event_handler);
    memcpy(uuid.Char_UUID_128, base, sizeof(base));
    if (aci_gatt_add_service(UUID_TYPE_128, (Service_UUID_t *)&uuid,
            PRIMARY_SERVICE, 4, &service) != BLE_STATUS_SUCCESS) return;
    uuid.Char_UUID_128[12] = 0xb1;
    if (aci_gatt_add_char(service, UUID_TYPE_128, &uuid, ADC_BLE_RAW_LENGTH,
            CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
            GATT_NOTIFY_ATTRIBUTE_WRITE | GATT_NOTIFY_NOTIFICATION_COMPLETION, 0x10, CHAR_VALUE_LEN_CONSTANT,
            &raw_char) != BLE_STATUS_SUCCESS) return;
    available = 1;
    BLE_TX_Register(BLE_TX_ADC,service,raw_char);
    (void)BLE_TX_Local(service,raw_char,empty,sizeof(empty));
}

/* SVCCTL routes GAP disconnects to app_ble.c, not registered service handlers. */
void ADC_BLE_Disconnected(uint16_t handle) { subscribe(handle, 0); }

uint8_t ADC_BLE_Enabled(void)
{
    if (available) {
        for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i)
            if (subscribers[i] != 0xffff) return 1;
    }
    return 0;
}

void ADC_BLE_Step(void)
{
    uint8_t payload[ADC_BLE_RAW_LENGTH] = {2,0,0,0,0,0,0,0,0,0};
    uint32_t now = osKernelGetTickCount();
    uint32_t interval = (osKernelGetTickFreq() * ADC_BLE_INTERVAL_MS + 999U) / 1000U;
    if ((uint32_t)(now - last_sample) < interval) return;
    last_sample = now;
    /* Drain even when disconnected: every report covers a recent bounded window. */
    uint16_t values[3] = {0};
    uint8_t valid = ADC_DMA_Window(values);
    if (!ADC_BLE_Enabled()) {
        if (streaming) {
            /* Do not retain apparently valid readings from a previous session. */
            (void)BLE_TX_Local(service,raw_char,payload,sizeof(payload));
        }
        streaming = 0;
        return;
    }
    streaming = 1;
    payload[1] = valid;
    for (unsigned i = 0; i < 3; ++i) {
        payload[2 + 2*i] = (uint8_t)values[i];
        payload[3 + 2*i] = (uint8_t)(values[i] >> 8);
    }
    if (valid & 1U) {
        uint16_t battery_mv = Battery_Millivolts(values[0]);
        payload[8] = (uint8_t)battery_mv;
        payload[9] = (uint8_t)(battery_mv >> 8);
    }
    /* On transport congestion, drop this frame and publish the next fresh one. */
    (void)BLE_TX_Latest(BLE_TX_ADC,payload,sizeof(payload));
}
