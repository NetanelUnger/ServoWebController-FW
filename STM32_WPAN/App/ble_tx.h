#ifndef BLE_TX_H
#define BLE_TX_H
#include <stdint.h>
#include "main.h"
#include "cmsis_os2.h"
#include "ble_types.h"
typedef enum { BLE_TX_REPLY, BLE_TX_SERVO, BLE_TX_TOF, BLE_TX_ADC, BLE_TX_IMU, BLE_TX_OTA, BLE_TX_STREAM_COUNT } BLE_TX_Stream;
#define BLE_TX_PAYLOAD_MAX 255U
typedef struct {
    uint32_t Session, Subscription;
    uint16_t Connection;
    uint8_t Stream, Length, Reserved;
    uint8_t Payload[BLE_TX_PAYLOAD_MAX];
} BLE_TX_Item;
void BLE_TX_Register(BLE_TX_Stream stream, uint16_t service, uint16_t characteristic);
void BLE_TX_Event(void *event);
void BLE_TX_EventsDrained(void);
void BLE_TX_Connected(uint16_t handle);
void BLE_TX_Disconnected(uint16_t handle);
void BLE_TX_Task(void *argument);
uint32_t BLE_TX_Run(void); /* one nonblocking state-machine step; returns wait ticks */
uint8_t BLE_TX_Capture(uint16_t handle, BLE_TX_Stream stream, BLE_TX_Item *item);
uint8_t BLE_TX_SessionCurrent(const BLE_TX_Item *item);
uint8_t BLE_TX_ReserveRx(BLE_TX_Item *item);
void BLE_TX_Release(const BLE_TX_Item *item);
uint8_t BLE_TX_ConfirmationIsOta(uint16_t connection);
uint8_t BLE_TX_Current(const BLE_TX_Item *item);
osStatus_t BLE_TX_Enqueue(const BLE_TX_Item *item, uint32_t wait);
tBleStatus BLE_TX_Latest(BLE_TX_Stream stream, const uint8_t *data, uint16_t length);
tBleStatus BLE_TX_Ordered(BLE_TX_Stream stream, const uint8_t *data, uint16_t length);
tBleStatus BLE_TX_Local(uint16_t service, uint16_t characteristic, const uint8_t *data, uint16_t length);
void BLE_TX_Stats(char *out, unsigned size);
void BLE_TX_RxResult(uint8_t accepted);
/* Reserve before admitting a write or applying servo side effects. Commit only
 * publishes the saved permission decision; the TX owner calls the controller. */
typedef struct { uint32_t Session, Serial; uint16_t Connection; } BLE_TX_AttTicket;
uint8_t BLE_TX_AttBegin(uint16_t connection, uint16_t attribute, uint8_t write,
                       const uint8_t *data, uint16_t length, BLE_TX_AttTicket *ticket);
void BLE_TX_AttCommit(const BLE_TX_AttTicket *ticket, uint8_t status, uint8_t error);
void BLE_TX_AttResult(tBleStatus result);
void BLE_TX_AttResultFor(const char *operation, uint16_t attribute, tBleStatus result);
#endif
