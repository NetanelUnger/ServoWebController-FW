/* Shared advisory link policy for commands, telemetry, display, files and OTA. */
#include "ble_link.h"
#include "app_conf.h"
#include "ble_core.h"
#include "log_module.h"
#include <string.h>

enum { LINK_INTERVAL, LINK_LENGTH, LINK_PHY, LINK_DONE };
typedef struct {
    uint32_t due;
    uint16_t handle;
    uint8_t active, phase, attempts, phy_pending, phy_retries;
} LinkRequest;
static LinkRequest links[CFG_BLE_NUM_LINK];

void BLE_LinkInit(void)
{
    memset(links, 0, sizeof(links));
}

void BLE_LinkConnected(uint16_t handle, uint8_t role, uint16_t interval,
                       uint16_t latency, uint32_t now)
{
    LinkRequest *free_link = NULL;
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
        if (links[i].active && links[i].handle == handle) return;
        if (!links[i].active && free_link == NULL) free_link = &links[i];
    }
    if (free_link == NULL) return;
    *free_link = (LinkRequest){ .due = now + 1000U, .handle = handle, .active = 1U,
        /* L2CAP update is peripheral-only. Keep an already fast, zero-latency link. */
        .phase = (role == 1U && (interval > 12U || latency != 0U))
                 ? LINK_INTERVAL : LINK_LENGTH };
}

void BLE_LinkDisconnected(uint16_t handle)
{
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i)
        if (links[i].active && links[i].handle == handle) links[i].active = 0U;
}

/* A simultaneous central procedure may collide with our accepted PHY request.
 * Retry that transient failure only; unsupported PHY / peer preferences stand. */
void BLE_LinkPhyComplete(uint16_t handle, uint8_t status, uint32_t now)
{
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
        LinkRequest *link = &links[i];
        if (!link->active || link->handle != handle || !link->phy_pending) continue;
        link->phy_pending = 0U;
        if ((status == HCI_LL_PROCEDURE_COLLISION_ERR_CODE ||
             status == HCI_DIFFERENT_TRANSACTION_COLLISION_ERR_CODE) && link->phy_retries < 2U) {
            ++link->phy_retries;
            link->phase = LINK_PHY;
            link->due = now + 1000U;
        }
    }
}

uint32_t BLE_LinkWaitMs(uint32_t now)
{
    uint32_t wait = UINT32_MAX;
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
        LinkRequest *link = &links[i];
        if (!link->active || link->phase == LINK_DONE) continue;
        int32_t remaining = (int32_t)(link->due - now);
        uint32_t delay = remaining > 0 ? (uint32_t)remaining : 0U;
        if (delay < wait) wait = delay;
    }
    return wait;
}

void BLE_LinkStep(uint32_t now)
{
    for (unsigned i = 0; i < CFG_BLE_NUM_LINK; ++i) {
        LinkRequest *link = &links[i];
        if (!link->active || link->phase == LINK_DONE || (int32_t)(now - link->due) < 0) continue;
        tBleStatus status;
        switch (link->phase) {
        case LINK_INTERVAL:
            /* 15 ms, latency 0, 4 s timeout. RF/Flash arbitration stays enabled. */
            status = aci_l2cap_connection_parameter_update_req(link->handle, 12U, 12U, 0U, 400U);
            break;
        case LINK_LENGTH:
            status = hci_le_set_data_length(link->handle, 251U, 2120U);
            break;
        default:
            status = hci_le_set_phy(link->handle, 0U, HCI_TX_PHYS_LE_2M_PREF,
                                   HCI_RX_PHYS_LE_2M_PREF, 0U);
            break;
        }
        if (link->phase == LINK_PHY && status == BLE_STATUS_SUCCESS) link->phy_pending = 1U;
        LOG_INFO_BLE("Link 0x%04X request %u status=0x%02X\n", link->handle, link->phase, status);
        /* Retry rejected submissions at most three times. Once accepted, the
         * peer owns negotiation; never repeatedly fight its chosen settings. */
        if (status == BLE_STATUS_SUCCESS || ++link->attempts >= 3U) {
            ++link->phase;
            link->attempts = 0U;
        }
        link->due = now + 1000U;
    }
}
