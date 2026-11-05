#ifndef NATILAB_IMU_H
#define NATILAB_IMU_H
#include <stdint.h>
#define IMU_RAW_LENGTH 48U
/* Service 91bad4a0; RAW 91bad4a1; suffix b950-4226-aa2b-4ede9fa42f59. */
void IMU_BLE_Init(void);
void IMU_BLE_Disconnected(uint16_t handle);
uint8_t IMU_BLE_Enabled(void);
uint8_t IMU_BLE_Mode(void);
uint32_t IMU_BLE_RequestId(void);
void IMU_BLE_Applied(uint8_t mode);
uint8_t IMU_BLE_Publish(const uint8_t *data);
void IMU_Step(void); /* Called by defaultTask; never from BLE callbacks. */
typedef struct {
    uint32_t frames, errors, tx_drops;
    uint8_t ready, running, whoami, address;
    uint8_t packet[IMU_RAW_LENGTH];
} IMU_Runtime;
extern volatile IMU_Runtime g_imu;
#endif
