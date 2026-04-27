/**
 * @file    comm_interface.c
 * @brief   Text command parser and BLE NUS queue processing.
 */

#include "main.h"
#include "app_freertos.h"
#include "nus_app.h"
#include "config.h"
#include "log_module.h"

#include "comm_interface.h"
#ifdef ENABLE_VL53L5CX
#include "VL53L5CX/vl53l5cx_task.h"
#endif

#include <limits.h>
#include <servo/servo_task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Maximum textual response length returned by the command parser. */
#define COMM_TEXT_RESPONSE_MAX_LEN 50U

/** @brief Shared communication runtime flags. */
volatile comm_infoTypeDef comm_info = {
    .debug_enabled = 0U,
    .pca9685_ready = 0U,
};

/**
 * @brief Copy a short textual response into the transmit payload.
 * @param res_payload Destination payload buffer.
 * @param text NUL-terminated source string.
 * @return Number of bytes copied into @p res_payload.
 */
static packetSize_t comm_WriteResponseText(uint8_t *res_payload, const char *text)
{
    size_t len;

    len = strlen(text);
    if (len >= COMM_TEXT_RESPONSE_MAX_LEN)
    {
        len = COMM_TEXT_RESPONSE_MAX_LEN - 1U;
    }

    memcpy(res_payload, text, len);
    return (packetSize_t)len;
}

/**
 * @brief Format a success reply for commands that take no arguments.
 * @param res_payload Destination payload buffer.
 * @param command Canonical command name to echo back.
 * @return Number of response bytes written.
 */
static packetSize_t comm_WriteOkEchoNoArgs(uint8_t *res_payload, const char *command)
{
    int written;

    written = snprintf((char *)res_payload, COMM_TEXT_RESPONSE_MAX_LEN, "OK %s", command);
    if (written < 0)
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    if ((size_t)written >= COMM_TEXT_RESPONSE_MAX_LEN)
    {
        written = (int)(COMM_TEXT_RESPONSE_MAX_LEN - 1U);
    }

    return (packetSize_t)written;
}

/**
 * @brief Format a success reply for commands with channel and value arguments.
 * @param res_payload Destination payload buffer.
 * @param command Canonical command name to echo back.
 * @param channel Servo channel argument.
 * @param value Servo pulse value argument.
 * @return Number of response bytes written.
 */
static packetSize_t comm_WriteOkEchoChannelValue(uint8_t *res_payload, const char *command, uint8_t channel, uint16_t value)
{
    int written;

    written = snprintf((char *)res_payload, COMM_TEXT_RESPONSE_MAX_LEN, "OK %s %u %u", command, (unsigned int)channel, (unsigned int)value);
    if (written < 0)
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    if ((size_t)written >= COMM_TEXT_RESPONSE_MAX_LEN)
    {
        written = (int)(COMM_TEXT_RESPONSE_MAX_LEN - 1U);
    }

    return (packetSize_t)written;
}

/**
 * @brief Format a success reply for timed-move commands.
 * @param res_payload Destination payload buffer.
 * @param command Canonical command name to echo back.
 * @param channel Servo channel argument.
 * @param value Servo pulse value argument.
 * @param duration_ms Move duration argument.
 * @return Number of response bytes written.
 */
static packetSize_t comm_WriteOkEchoChannelValueU32(uint8_t *res_payload,
                                                    const char *command,
                                                    uint8_t channel,
                                                    uint16_t value,
                                                    uint32_t duration_ms)
{
    int written;

    written = snprintf((char *)res_payload,
                       COMM_TEXT_RESPONSE_MAX_LEN,
                       "OK %s %u %u %lu",
                       command,
                       (unsigned int)channel,
                       (unsigned int)value,
                       (unsigned long)duration_ms);
    if (written < 0)
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    if ((size_t)written >= COMM_TEXT_RESPONSE_MAX_LEN)
    {
        written = (int)(COMM_TEXT_RESPONSE_MAX_LEN - 1U);
    }

    return (packetSize_t)written;
}

/**
 * @brief Format a success reply for commands with two uint16 arguments.
 * @param res_payload Destination payload buffer.
 * @param command Canonical command name to echo back.
 * @param first First uint16 argument.
 * @param second Second uint16 argument.
 * @return Number of response bytes written.
 */
static packetSize_t comm_WriteOkEchoValuePair(uint8_t *res_payload,
                                              const char *command,
                                              uint16_t first,
                                              uint16_t second)
{
    int written;

    written = snprintf((char *)res_payload,
                       COMM_TEXT_RESPONSE_MAX_LEN,
                       "OK %s %u %u",
                       command,
                       (unsigned int)first,
                       (unsigned int)second);
    if (written < 0)
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    if ((size_t)written >= COMM_TEXT_RESPONSE_MAX_LEN)
    {
        written = (int)(COMM_TEXT_RESPONSE_MAX_LEN - 1U);
    }

    return (packetSize_t)written;
}

/**
 * @brief Check whether a character is treated as an argument separator.
 * @param c Character to inspect.
 * @return Non-zero when @p c is considered a separator.
 */
static uint8_t comm_IsArgSeparator(char c)
{
    if ((c == ' ') || (c == '\t') || (c == ',') || (c == ':') || (c == ';') || (c == '\r') || (c == '\n'))
    {
        return 1U;
    }

    return 0U;
}

/**
 * @brief Advance past any leading argument separators.
 * @param cursor Input cursor.
 * @return Pointer to the first non-separator character or the NUL terminator.
 */
static char *comm_SkipSeparators(char *cursor)
{
    while ((*cursor != '\0') && (comm_IsArgSeparator(*cursor) != 0U))
    {
        cursor++;
    }

    return cursor;
}

/**
 * @brief Parse one unsigned integer token from the request string.
 * @param cursor In/out pointer to the current parse position.
 * @param max_value Maximum accepted value.
 * @param value Output location for the parsed value.
 * @return Non-zero on success.
 */
static uint8_t comm_ParseUint(char **cursor, uint32_t max_value, uint32_t *value)
{
    char *end_ptr;
    unsigned long parsed_value;

    *cursor = comm_SkipSeparators(*cursor);

    if (**cursor == '\0')
    {
        return 0U;
    }

    parsed_value = strtoul(*cursor, &end_ptr, 0);

    if ((end_ptr == *cursor) || (parsed_value > max_value))
    {
        return 0U;
    }

    *cursor = end_ptr;
    *value = (uint32_t)parsed_value;

    return 1U;
}

/**
 * @brief Parse TOF distance encoding range values.
 * @param args Command argument string.
 * @param min Output minimum distance value.
 * @param max Output maximum distance value.
 * @return Non-zero on success.
 */
static uint8_t comm_ParseTOFValues(char *args, uint16_t *min, uint16_t *max)
{
    char *cursor;
    uint32_t parsed_min;
    uint32_t parsed_max;

    cursor = args;

    if (comm_ParseUint(&cursor, (uint32_t)UINT16_MAX, &parsed_min) == 0U)
    {
        return 0U;
    }

    if (comm_ParseUint(&cursor, (uint32_t)UINT16_MAX, &parsed_max) == 0U)
    {
        return 0U;
    }

    cursor = comm_SkipSeparators(cursor);
    if (*cursor != '\0')
    {
        return 0U;
    }

    *min = (uint16_t)parsed_min;
    *max = (uint16_t)parsed_max;

    return 1U;
}


/**
 * @brief Parse a servo channel followed by a pulse value.
 * @param args Command argument string.
 * @param channel Output channel index.
 * @param value Output pulse value.
 * @return Non-zero on success.
 */
static uint8_t comm_ParseChannelAndValue(char *args, uint8_t *channel, uint16_t *value)
{
    char *cursor;
    uint32_t parsed_channel;
    uint32_t parsed_value;

    cursor = args;

    if (comm_ParseUint(&cursor, (uint32_t)UINT8_MAX, &parsed_channel) == 0U)
    {
        return 0U;
    }

    if (comm_ParseUint(&cursor, (uint32_t)UINT16_MAX, &parsed_value) == 0U)
    {
        return 0U;
    }

    cursor = comm_SkipSeparators(cursor);
    if (*cursor != '\0')
    {
        return 0U;
    }

    *channel = (uint8_t)parsed_channel;
    *value = (uint16_t)parsed_value;

    return 1U;
}

/**
 * @brief Parse a servo channel, pulse value, and move duration.
 * @param args Command argument string.
 * @param channel Output channel index.
 * @param value Output pulse value.
 * @param duration_ms Output move duration in milliseconds.
 * @return Non-zero on success.
 */
static uint8_t comm_ParseChannelValueAndDuration(char *args, uint8_t *channel, uint16_t *value, uint32_t *duration_ms)
{
    char *cursor;
    uint32_t parsed_channel;
    uint32_t parsed_value;
    uint32_t parsed_duration_ms;

    cursor = args;

    if (comm_ParseUint(&cursor, (uint32_t)UINT8_MAX, &parsed_channel) == 0U)
    {
        return 0U;
    }

    if (comm_ParseUint(&cursor, (uint32_t)UINT16_MAX, &parsed_value) == 0U)
    {
        return 0U;
    }

    if (comm_ParseUint(&cursor, UINT32_MAX, &parsed_duration_ms) == 0U)
    {
        return 0U;
    }

    cursor = comm_SkipSeparators(cursor);
    if (*cursor != '\0')
    {
        return 0U;
    }

    *channel = (uint8_t)parsed_channel;
    *value = (uint16_t)parsed_value;
    *duration_ms = parsed_duration_ms;

    return 1U;
}

/**
 * @brief Split a request string into command and argument substrings.
 * @param req_text Mutable request buffer.
 * @param command Output pointer receiving the command token.
 * @param args Output pointer receiving the remaining argument string.
 * @return Non-zero on success.
 */
static uint8_t comm_ExtractCommandAndArgs(char *req_text, char **command, char **args)
{
    char *cursor;
    char *cmd_start;

    cursor = comm_SkipSeparators(req_text);
    if (*cursor == '\0')
    {
        return 0U;
    }

    cmd_start = cursor;
    while ((*cursor != '\0') && (comm_IsArgSeparator(*cursor) == 0U))
    {
        cursor++;
    }

    if (*cursor != '\0')
    {
        *cursor = '\0';
        cursor++;
    }

    *command = cmd_start;
    *args = comm_SkipSeparators(cursor);

    return 1U;
}

/**
 * @brief Test a command token against canonical and legacy spellings.
 * @param command Parsed request command token.
 * @param primary Canonical command spelling.
 * @param legacy Backward-compatible legacy spelling.
 * @return Non-zero when either spelling matches.
 */
static uint8_t comm_CommandMatches(const char *command, const char *primary, const char *legacy)
{
    return (uint8_t)((strcmp(command, primary) == 0) || ((legacy != NULL) && (strcmp(command, legacy) == 0)));
}

/**
 * @brief Flush all queued NUS transmit work items.
 */
static void comm_ProcessTxQueue(void)
{
    NUS_APP_QueueItem_t item;

    while (osMessageQueueGet(commTxQueueHandle, &item, NULL, 0U) == osOK)
    {
        NusTxTask(&item);
        vPortFree(item.Payload);
    }
}

/**
 * @brief Flush all queued NUS receive work items.
 */
static void comm_ProcessRxQueue(void)
{
    NUS_APP_QueueItem_t item;

    while (osMessageQueueGet(commRxQueueHandle, &item, NULL, 0U) == osOK)
    {
        if (comm_info.debug_enabled != 0U)
        {
            LOG_INFO_APP("will handle %u data\r\n", (unsigned int)item.Length);
            LogHexArray("data: ", item.Payload, item.Length);
        }

        NusRxTask(&item);
        vPortFree(item.Payload);
    }
}

packetSize_t parser_ProcessOne(uint8_t *payload, packetSize_t req_len, uint8_t *res_payload)
{
    char req_text[256];
    char *command;
    char *args;
    uint8_t channel;
    uint16_t value;

    if ((payload == NULL) || (res_payload == NULL) || (req_len == 0U))
    {
        return 0U;
    }

    if ((size_t)req_len >= sizeof(req_text))
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    memcpy(req_text, payload, req_len);
    req_text[req_len] = '\0';

    if (comm_ExtractCommandAndArgs(req_text, &command, &args) == 0U)
    {
        return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
    }

    if (comm_CommandMatches(command, COMM_CMD_PING, NULL) != 0U)
    {
        if (*args != '\0')
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        return comm_WriteOkEchoNoArgs(res_payload, COMM_CMD_PING);
    }

    if (comm_CommandMatches(command, COMM_CMD_PCA9685_SET_SERVO_US, COMM_CMD_LEGACY_PCA9685_SET_SERVO_US) != 0U)
    {
        if (comm_ParseChannelAndValue(args, &channel, &value) == 0U)
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        if (servo_SetServoImmediateValue(channel, value) != HAL_OK)
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        return comm_WriteOkEchoChannelValue(res_payload, COMM_CMD_PCA9685_SET_SERVO_US, channel, value);
    }

    if (comm_CommandMatches(command, COMM_CMD_SERVO_START_MOVE, COMM_CMD_LEGACY_SERVO_START_MOVE) != 0U)
    {
        uint32_t duration_ms;

        if (comm_ParseChannelValueAndDuration(args, &channel, &value, &duration_ms) == 0U)
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        if (servo_StartTimedMove(channel, value, duration_ms) != HAL_OK)
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        return comm_WriteOkEchoChannelValueU32(res_payload, COMM_CMD_SERVO_START_MOVE, channel, value, duration_ms);
    }

#ifdef ENABLE_VL53L5CX
    if (comm_CommandMatches(command, COMM_CMD_TOF_SET_MIN_MAX, COMM_CMD_TOF_SET_MIN_MAX) != 0U)
    {
        uint16_t min_mm, max_mm;
        uint16_t applied_min_mm, applied_max_mm;

        if (comm_ParseTOFValues(args, &min_mm, &max_mm) == 0U)
        {
            return comm_WriteResponseText(res_payload, "- BAD ARGUMENTS");
        }

        if (vl53l5cx_SetDistanceEncodingRange(min_mm, max_mm) == 0U)
        {
            return comm_WriteResponseText(res_payload, "- BAD VALUES");
        }

        vl53l5cx_GetDistanceEncodingRange(&applied_min_mm, &applied_max_mm);

        return comm_WriteOkEchoValuePair(res_payload, COMM_CMD_TOF_SET_MIN_MAX, applied_min_mm, applied_max_mm);
    }
#endif

    return comm_WriteResponseText(res_payload, "- NOT SUPPORTED");
}

void comm_Init(void)
{
    comm_info.debug_enabled = 0U;
    comm_info.pca9685_ready = 0U;
}

void comm_Task(void)
{
    uint32_t event_stat;

    event_stat = osEventFlagsWait(
        commEventHandle,
        COMM_WAKE_EVENT | COMM_NUS_TX_EVENT | COMM_NUS_RX_EVENT,
        osFlagsWaitAny,
        THREAD_WAIT_TIMEOUT_TICKS);

    if (event_stat != osErrorTimeout)
    {
        if ((event_stat & COMM_NUS_TX_EVENT) == COMM_NUS_TX_EVENT)
        {
            comm_ProcessTxQueue();
        }

        if ((event_stat & COMM_NUS_RX_EVENT) == COMM_NUS_RX_EVENT)
        {
            comm_ProcessRxQueue();
        }
    }
}
