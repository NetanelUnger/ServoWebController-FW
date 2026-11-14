#ifndef ROBOT_WASM_PROFILE_H
#define ROBOT_WASM_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
#define ROBOT_WASM_FILE_TYPE 5U
#define ROBOT_WASM_MAX_BYTES 2048U
/* Bounded preflight before entering WAMR. WAMR subsequently validates bytecode.
 * No start/constructor, imported memory, populated table, WASI or unknown host
 * functions. The linker's one empty fixed table slot is accepted. */
bool RobotWasm_Validate(const uint8_t *bytes, uint32_t length);
#endif
