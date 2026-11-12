/* Deterministic tests of the production policy, including all eight links. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define CFG_BLE_NUM_LINK 8U
#define BLE_STATUS_SUCCESS 0U
#define HCI_TX_PHYS_LE_2M_PREF 2U
#define HCI_RX_PHYS_LE_2M_PREF 2U
#define HCI_LL_PROCEDURE_COLLISION_ERR_CODE 0x23U
#define HCI_DIFFERENT_TRANSACTION_COLLISION_ERR_CODE 0x2AU
#define LOG_INFO_BLE(...) ((void)0)
typedef uint8_t tBleStatus;
static unsigned calls[3], total;
static uint16_t last_handle;
static uint8_t result;
static tBleStatus request(unsigned phase, uint16_t handle)
{ ++calls[phase]; ++total; last_handle = handle; return result; }
static tBleStatus aci_l2cap_connection_parameter_update_req(uint16_t h, uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{ assert(a == 12 && b == 12 && c == 0 && d == 400); return request(0, h); }
static tBleStatus hci_le_set_data_length(uint16_t h, uint16_t a, uint16_t b)
{ assert(a == 251 && b == 2120); return request(1, h); }
static tBleStatus hci_le_set_phy(uint16_t h, uint8_t a, uint8_t b, uint8_t c, uint16_t d)
{ assert(a == 0 && b == 2 && c == 2 && d == 0); return request(2, h); }
#include "link_under_test.inc"
/* Compile the real dispatcher too: a timed wake must not dequeue an event. */
#define UNUSED(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define osOK 0
static int HciAsyncEvtSemaphore;
static unsigned waits, events;
static uint32_t tick;
static jmp_buf dispatcher_done;
static uint32_t HAL_GetTick(void) { return tick; }
static int osSemaphoreAcquire(int semaphore, uint32_t timeout)
{
    (void)semaphore;
    if (++waits == 3) longjmp(dispatcher_done, 1);
    assert(timeout == 1000);
    tick += 1000;
    return waits == 1 ? -2 : osOK;
}
static void Ble_UserEvtRx(void) { ++events; }
#include "dispatcher_under_test.inc"
static void reset(void)
{ BLE_LinkInit(); memset(calls, 0, sizeof(calls)); total = 0; result = 0; }
int main(void)
{
    reset(); assert(BLE_LinkWaitMs(0) == UINT32_MAX);
    BLE_LinkConnected(42, 1, 48, 0, 100);
    assert(BLE_LinkWaitMs(100) == 1000);
    BLE_LinkStep(1099); assert(total == 0);
    BLE_LinkStep(1100); assert(calls[0] == 1 && last_handle == 42);
    BLE_LinkConnected(42, 1, 48, 0, 1500); /* duplicate cannot restart negotiation */
    BLE_LinkStep(2100); BLE_LinkStep(3100);
    assert(total == 3 && calls[1] == 1 && calls[2] == 1);
    assert(BLE_LinkWaitMs(3100) == UINT32_MAX);
    BLE_LinkStep(999999); assert(total == 3);

    reset(); BLE_LinkConnected(0, 1, 48, 0, 0); result = 0x0c;
    for (uint32_t t = 1000; t <= 12000; t += 1000) BLE_LinkStep(t);
    assert(total == 9 && calls[0] == 3 && calls[1] == 3 && calls[2] == 3);
    assert(BLE_LinkWaitMs(12000) == UINT32_MAX);

    reset(); BLE_LinkConnected(1, 1, 48, 0, 0); result = 0x64;
    BLE_LinkStep(1000); result = 0; BLE_LinkStep(2000);
    BLE_LinkStep(3000); BLE_LinkStep(4000);
    assert(total == 4 && calls[0] == 2 && calls[1] == 1 && calls[2] == 1);

    reset(); BLE_LinkConnected(7, 1, 48, 0, 0); BLE_LinkDisconnected(7);
    BLE_LinkStep(1000); assert(total == 0);
    BLE_LinkConnected(7, 1, 48, 0, 2000); BLE_LinkStep(3000); assert(calls[0] == 1);

    reset(); BLE_LinkConnected(1, 1, 6, 0, 0); BLE_LinkConnected(2, 0, 48, 0, 0);
    BLE_LinkStep(1000); BLE_LinkStep(2000);
    assert(calls[0] == 0 && calls[1] == 2 && calls[2] == 2);
    BLE_LinkConnected(3, 1, 12, 2, 2000); BLE_LinkStep(3000); assert(calls[0] == 1);

    reset();
    for (uint16_t h = 0; h < 8; ++h) BLE_LinkConnected(h, 1, 48, 0, 0);
    BLE_LinkConnected(99, 1, 48, 0, 0); /* capacity cannot corrupt another link */
    BLE_LinkDisconnected(3); BLE_LinkConnected(9, 1, 48, 0, 0);
    BLE_LinkStep(1000); BLE_LinkStep(2000); BLE_LinkStep(3000);
    assert(calls[0] == 8 && calls[1] == 8 && calls[2] == 8);

    reset(); BLE_LinkConnected(1, 1, 48, 0, UINT32_MAX - 500U);
    BLE_LinkStep(498); assert(total == 0 && BLE_LinkWaitMs(498) == 1);
    BLE_LinkStep(499); assert(total == 1);
    reset(); BLE_LinkConnected(1, 1, 48, 0, 0);
    waits = events = 0; tick = 0;
    if (setjmp(dispatcher_done) == 0) HciAsyncEvt_Task_Entry(NULL);
    assert(events == 1 && calls[0] == 1 && calls[1] == 1);
    reset(); BLE_LinkConnected(1, 1, 12, 0, 0);
    BLE_LinkStep(1000); BLE_LinkStep(2000);
    BLE_LinkPhyComplete(99, 0x2a, 2100); assert(BLE_LinkWaitMs(2100) == UINT32_MAX);
    BLE_LinkPhyComplete(1, 0x2a, 2100); assert(BLE_LinkWaitMs(2100) == 1000);
    BLE_LinkStep(3100); BLE_LinkPhyComplete(1, 0x23, 3200); BLE_LinkStep(4200);
    BLE_LinkPhyComplete(1, 0x2a, 4300); assert(BLE_LinkWaitMs(4300) == UINT32_MAX);
    assert(calls[2] == 3);

    reset(); BLE_LinkConnected(1, 1, 12, 0, 0);
    BLE_LinkStep(1000); BLE_LinkStep(2000); BLE_LinkPhyComplete(1, 0x1a, 2100);
    assert(BLE_LinkWaitMs(2100) == UINT32_MAX && calls[2] == 1);
    BLE_LinkPhyComplete(1, 0x2a, 2200); assert(BLE_LinkWaitMs(2200) == UINT32_MAX);

    reset(); BLE_LinkConnected(1, 1, 12, 0, 0);
    BLE_LinkStep(1000); BLE_LinkStep(2000); BLE_LinkPhyComplete(1, 0x2a, 2100);
    BLE_LinkDisconnected(1); BLE_LinkStep(3100); assert(calls[2] == 1);
    puts("PASS: scheduling, retries, reconnect, roles, eight links and tick wrap");
    return 0;
}
