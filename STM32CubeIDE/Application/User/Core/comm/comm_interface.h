/**
 * @file    comm_interface.h
 * @brief   Text command parser and BLE NUS queue integration.
 */

#ifndef COMM_INTERFACE_H
#define COMM_INTERFACE_H

#include <stdint.h>

/** @brief Event bit used to wake the communication task. */
#define COMM_WAKE_EVENT (1U << 0)
/** @brief Event bit indicating pending BLE NUS transmit work. */
#define COMM_NUS_TX_EVENT (1U << 1)
/** @brief Event bit indicating pending BLE NUS receive work. */
#define COMM_NUS_RX_EVENT (1U << 2)

/** @brief Connectivity check command. */
#define COMM_CMD_PING "PING"
/** @brief Immediate servo pulse update command. */
#define COMM_CMD_PCA9685_SET_SERVO_US "SET_DIRECT"
/** @brief Timed servo interpolation command. */
#define COMM_CMD_SERVO_START_MOVE "SET_TIMING"
/** @brief Backward-compatible alias for `SET_DIRECT`. */
#define COMM_CMD_LEGACY_PCA9685_SET_SERVO_US "PCA9685_SET_SERVO_US"
/** @brief Backward-compatible alias for `SET_TIMING`. */
#define COMM_CMD_LEGACY_SERVO_START_MOVE "servo_StartMove"
/** @brief TOF distance encoding range command. */
#define COMM_CMD_TOF_SET_MIN_MAX "TOF_SET_MIN_MAX"

/** @brief Serialized packet length type used by the command parser. */
typedef uint8_t packetSize_t;

/**
 * @brief Runtime status flags for the communication module.
 */
typedef struct
{
    /** @brief Enables verbose logging of NUS traffic when non-zero. */
    uint8_t debug_enabled;
    /** @brief Reserved readiness flag for higher-level communication features. */
    uint8_t pca9685_ready;
} comm_infoTypeDef;

/** @brief Global communication runtime state. */
extern volatile comm_infoTypeDef comm_info;

/**
 * @brief Parse one textual request and build the textual response payload.
 * @param payload Input request bytes.
 * @param req_len Number of valid bytes in @p payload.
 * @param res_payload Output buffer that receives the response body.
 * @return Number of bytes written into @p res_payload.
 */
extern packetSize_t parser_ProcessOne(uint8_t *payload, packetSize_t req_len, uint8_t *res_payload);

/**
 * @brief Initialize communication runtime flags.
 */
extern void comm_Init(void);

/**
 * @brief Process pending communication events and NUS queue traffic.
 */
extern void comm_Task(void);

#endif /* COMM_INTERFACE_H */
