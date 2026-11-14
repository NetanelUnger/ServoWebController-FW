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

/** @brief Response buffer size, including the snprintf terminator. */
#define COMM_TEXT_RESPONSE_MAX_LEN 255U
/** @brief List every command and its syntax. */
#define COMM_CMD_HELP "HELP"

/** @brief Connectivity check command. */
#define COMM_CMD_PING "PING"
#define COMM_CMD_WASM_RUN "WASM_RUN"
#define COMM_CMD_WASM_FILE "WASM_FILE"
#define COMM_CMD_WASM_INFO "WASM_INFO"
#define COMM_CMD_WASM_STOP "WASM_STOP"
#define COMM_CMD_WASM_STATUS "WASM_STATUS"
#define COMM_CMD_VERSION "VERSION"
/** All-channel repeating PWM test and motion cancellation. */
#define COMM_CMD_DEMO1 "DEMO1"
#define COMM_CMD_STOP "STOP"
#define COMM_CMD_SAFE_POSE "SAFE_POSE"
#define COMM_CMD_TIMELINE_PLAY "TIMELINE_PLAY"
#define COMM_CMD_TIMELINE_STOP "TIMELINE_STOP"
#define COMM_CMD_TIMELINE_STATUS "TIMELINE_STATUS"
/** Board diagnostics, using signal names or physical pin names. */
#define COMM_CMD_GPIO_GET "GPIO_GET"
#define COMM_CMD_GPIO_SET "GPIO_SET"
#define COMM_CMD_ADC_READ "ADC_READ"
#define COMM_CMD_PWM_GET "PWM_GET"
/** @brief Immediate servo pulse update command. */
#define COMM_CMD_PCA9685_SET_SERVO_US "SET_DIRECT"
/** @brief Timed servo interpolation command. */
#define COMM_CMD_SERVO_START_MOVE "SET_TIMING"
/** @brief Backward-compatible alias for `SET_DIRECT`. */
#define COMM_CMD_LEGACY_PCA9685_SET_SERVO_US "PCA9685_SET_SERVO_US"
/** @brief Backward-compatible alias for `SET_TIMING`. */
#define COMM_CMD_LEGACY_SERVO_START_MOVE "servo_StartMove"
/** @brief TOF distance encoding range command. */
#define COMM_CMD_TOF_MODE "TOF_MODE"
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
 * @param res_payload Buffer of at least COMM_TEXT_RESPONSE_MAX_LEN bytes.
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
