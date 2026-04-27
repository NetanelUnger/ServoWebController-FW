/**
 * @file    cli_interface.h
 * @brief   Public interface for the lightweight command-line task.
 */

#ifndef SRC_CLI_INTERFACE_H_
#define SRC_CLI_INTERFACE_H_

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

/** @brief Event bit raised when new CLI data is ready for processing. */
#define CLI_RCV_DATA_EVENT		(1 << 0)

/** @brief Queue payload size reserved for each received CLI message, including the trailing NUL. */
#define CLI_MSG_BUFFER_SIZE 32

/**
 * @brief Queue item used to transfer a single received CLI command to the task.
 */
typedef struct
{
  /** @brief NUL-terminated command payload copied from the transport layer. */
  uint8_t payload[CLI_MSG_BUFFER_SIZE];
} Cli_QueueItem_t;


/**
 * @brief Runtime diagnostics for the CLI module.
 */
typedef struct {
    /** @brief Reserved flag for optional CAN-to-CLI printing support. */
	uint8_t print_can_cli_msg;
    /** @brief Set to non-zero after the module records an internal error. */
    uint8_t error_flag;
    /** @brief Source line captured when `cli_ErrorHandler()` is invoked. */
    uint32_t error_line;
} cli_infoTypeDef;

/** @brief Helper macro for matching three-character command mnemonics. */
#define CMP_COMMAND(x,y)  (memcmp(x, y, 3)==0)

/** @brief Global CLI diagnostics structure. */
extern cli_infoTypeDef cli_info;

/**
 * @brief Initialize CLI runtime state and print the boot banner.
 */
extern void cli_Init(void);
/**
 * @brief Drain the CLI queue and dispatch pending commands.
 */
extern void cli_Task(void);

/**
 * @brief Copy raw transport data into the CLI queue.
 * @param msg Source byte buffer received from the underlying transport.
 * @param len Number of bytes available in @p msg.
 */
extern void cli_rcvHandler(uint8_t *msg, uint16_t len);

/**
 * @brief Record an unrecoverable CLI-side error.
 * @param line Source line associated with the failure.
 */
extern void cli_ErrorHandler(uint32_t line);

#endif //SRC_CLI_INTERFACE_H_
