/**
 * @file    cli_interface.c
 * @brief   Command-line task implementation and startup banner rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "config.h"
#include "log_module.h"
#include "log_module_conf.h"
#include "main.h"
#include "app_freertos.h"
#include "cli_interface.h"
#ifdef ENABLE_VL53L5CX
#include "VL53L5CX/vl53l5cx_task.h"
#endif

/**
 * @brief Handle a decoded CLI command.
 * @param command NUL-terminated command string extracted from the queue.
 */
static void cli_cliHandler(uint8_t *command);
static void cli_PrintHelp(void);
static uint8_t cli_IsArgSeparator(char ch);
static char* cli_SkipSeparators(char *cursor);
static uint8_t cli_ParseInt32(char **cursor, int32_t *value);
#ifdef ENABLE_VL53L5CX
static void cli_HandleTofCommand(char *args);
static void cli_PrintTofRange(void);
#endif

/**
 * @brief Replace all matching characters inside a fixed-length buffer.
 * @param str Buffer to update in place.
 * @param orig Character to replace.
 * @param rep Replacement character.
 * @param len Number of bytes to scan in @p str.
 */
static void cli_replaceChar(uint8_t *str, uint8_t orig, uint8_t rep,
		uint8_t len);

/** @brief Shared CLI runtime diagnostics. */
cli_infoTypeDef cli_info;

void cli_Init(void) {
	cli_info.print_can_cli_msg = 0U;
	cli_info.error_flag = 0U;
	cli_info.error_line = 0U;
}

void cli_Task(void) {
	Cli_QueueItem_t cli_QueueItem;
	uint8_t priority;

	while (osMessageQueueGet(cliQueueHandle, &cli_QueueItem, &priority,
			THREAD_WAIT_TIMEOUT_TICKS) == osOK) {
		switch (cli_QueueItem.payload[0]) {
		case 'c':
			cli_cliHandler(cli_QueueItem.payload);
			break;

		default:
			LOG_INFO_APP("unknown command\r\n");
			LOG_INFO_APP("c - for cli\r\n");
			break;
		}
	}
}

static void cli_replaceChar(uint8_t *str, uint8_t orig, uint8_t rep,
		uint8_t len) {
	for (uint8_t i = 0U; i < len; i++) {
		if (str[i] == orig) {
			str[i] = rep;
		}
	}
}

static void cli_PrintHelp(void) {
	LOG_INFO_APP("|c| commands:\r\n");
#ifdef ENABLE_VL53L5CX
	LOG_INFO_APP("|c|   c tof range\r\n");
	LOG_INFO_APP(
			"|c|   c tof range <min_mm> <max_mm>  (bounds: %u..%u, min < max)\r\n",
			(unsigned int)CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM,
			(unsigned int)CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM);
#else
    LOG_INFO_APP("|c|   no CLI subcommands are enabled\r\n");
#endif
}

static uint8_t cli_IsArgSeparator(char ch) {
	return (uint8_t) ((ch == ' ') || (ch == '\t') || (ch == ',') || (ch == '\r')
			|| (ch == '\n'));
}

static char* cli_SkipSeparators(char *cursor) {
	while ((*cursor != '\0') && (cli_IsArgSeparator(*cursor) != 0U)) {
		cursor++;
	}

	return cursor;
}

static uint8_t cli_ParseInt32(char **cursor, int32_t *value) {
	char *end_ptr;
	long parsed_value;

	*cursor = cli_SkipSeparators(*cursor);

	if (**cursor == '\0') {
		return 0U;
	}

	parsed_value = strtol(*cursor, &end_ptr, 0);
	if (end_ptr == *cursor) {
		return 0U;
	}

	*cursor = end_ptr;
	*value = (int32_t) parsed_value;

	return 1U;
}

#ifdef ENABLE_VL53L5CX
static void cli_PrintTofRange(void) {
	uint16_t min_mm;
	uint16_t max_mm;

	vl53l5cx_GetDistanceEncodingRange(&min_mm, &max_mm);
	LOG_INFO_APP("|c| tof range: min=%u mm, max=%u mm\r\n",
			(unsigned int )min_mm, (unsigned int )max_mm);
	LOG_INFO_APP(
			"|c| valid BLE payload values span 0..%u, 255 stays reserved for invalid/no target\r\n",
			(unsigned int)VL53L5CX_BLE_DISTANCE_MAX_VALID);
}

static void cli_HandleTofCommand(char *args) {
	char *cursor;
	int32_t min_mm;
	int32_t max_mm;

	cursor = cli_SkipSeparators(args);
	if (*cursor == '\0') {
		cli_PrintTofRange();
		cli_PrintHelp();
		return;
	}

	if ((strncmp(cursor, "range", 5) != 0)
			|| ((cursor[5] != '\0') && (cli_IsArgSeparator(cursor[5]) == 0U))) {
		LOG_INFO_APP("|c| unknown tof command\r\n");
		cli_PrintHelp();
		return;
	}

	cursor = cli_SkipSeparators(&cursor[5]);
	if (*cursor == '\0') {
		cli_PrintTofRange();
		return;
	}

	if ((cli_ParseInt32(&cursor, &min_mm) == 0U)
			|| (cli_ParseInt32(&cursor, &max_mm) == 0U)) {
		LOG_INFO_APP("|c| usage: c tof range <min_mm> <max_mm>\r\n");
		return;
	}

	cursor = cli_SkipSeparators(cursor);
	if (*cursor != '\0') {
		LOG_INFO_APP("|c| usage: c tof range <min_mm> <max_mm>\r\n");
		return;
	}

	if (min_mm < (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM) {
		min_mm = (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM;
	} else if (min_mm > (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM) {
		min_mm = (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM;
	}

	if (max_mm < (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM) {
		max_mm = (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM;
	} else if (max_mm > (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM) {
		max_mm = (int32_t) CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM;
	}

	if (min_mm >= max_mm) {
		LOG_INFO_APP(
				"|c| invalid tof range after clamping: min=%ld mm, max=%ld mm\r\n",
				(long )min_mm, (long )max_mm);
		LOG_INFO_APP("|c| expected %u <= min < max <= %u\r\n",
				(unsigned int)CONFIG_VL53L5CX_DISTANCE_LIMIT_MIN_MM,
				(unsigned int)CONFIG_VL53L5CX_DISTANCE_LIMIT_MAX_MM);
		return;
	}

	if (vl53l5cx_SetDistanceEncodingRange((uint16_t) min_mm, (uint16_t) max_mm)
			== 0U) {
		LOG_INFO_APP("|c| failed to update tof range\r\n");
		return;
	}

	LOG_INFO_APP("|c| updated tof range: min=%ld mm, max=%ld mm\r\n",
			(long )min_mm, (long )max_mm);
}
#endif

static void cli_cliHandler(uint8_t *command) {
	char *cursor = (char*) command;

	cursor = cli_SkipSeparators(cursor);
	if (*cursor == 'c') {
		cursor++;
	}

	cursor = cli_SkipSeparators(cursor);
	if ((*cursor == '\0') || (strcmp(cursor, "help") == 0)
			|| ((*cursor == '?') && (cursor[1] == '\0'))) {
		cli_PrintHelp();
		return;
	}

#ifdef ENABLE_VL53L5CX
	if ((strncmp(cursor, "tof", 3) == 0)
			&& ((cursor[3] == '\0') || (cli_IsArgSeparator(cursor[3]) != 0U))) {
		cli_HandleTofCommand(&cursor[3]);
		return;
	}
#endif

	LOG_INFO_APP("|c| unknown command: %s\r\n", command);
	cli_PrintHelp();
}

void cli_rcvHandler(uint8_t *msg, uint16_t len) {
	if (len > 0U) {
		Cli_QueueItem_t cli_QueueItem = { 0 };
		size_t copy_len =
				(len >= CLI_MSG_BUFFER_SIZE) ? (CLI_MSG_BUFFER_SIZE - 1U) : len;

		memcpy(cli_QueueItem.payload, msg, copy_len);
		cli_QueueItem.payload[copy_len] = '\0';

		if (osMessageQueuePut(cliQueueHandle, &cli_QueueItem, 0U, 0U) != osOK) {
			/* Ignore dropped messages when queue is full. */
		}
	}
}

void cli_ErrorHandler(uint32_t line) {
	cli_info.error_flag = 1U;
	cli_info.error_line = line;
}
