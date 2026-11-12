#ifndef BLE_LINK_H
#define BLE_LINK_H
#include <stdint.h>

/* Called only by the HCI event dispatcher (Init before its task starts).
 * Times are monotonic milliseconds. No work is done in connection callbacks. */
void BLE_LinkInit(void);
void BLE_LinkConnected(uint16_t handle, uint8_t role, uint16_t interval,
                       uint16_t latency, uint32_t now);
void BLE_LinkDisconnected(uint16_t handle);
void BLE_LinkStep(uint32_t now);
void BLE_LinkPhyComplete(uint16_t handle, uint8_t status, uint32_t now);
/* UINT32_MAX means no pending requests: the dispatcher may wait forever. */
uint32_t BLE_LinkWaitMs(uint32_t now);
#endif
