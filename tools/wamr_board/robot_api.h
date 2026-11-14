#ifndef ROBOT_WASM_API_H
#define ROBOT_WASM_API_H
#include <stdint.h>
#define ROBOT_IMPORT(name) __attribute__((import_module("robot"), import_name(#name)))
/* Prototype ABI 0, for the embedded qualification guest only.
 * Uploaded ABI 2 programs use the canonical robot.h in companion Web
 * public/wasm-compiler/sdk.js; see WAMR_BOARD.md for lifecycle and helpers.
 * All sensor reads refer to one snapshot captured before step(). Sensors must
 * already be enabled through BLE; samples older than 500 ms are unavailable. */
/* Word index 0..11 in the existing 48-byte little-endian IMU packet. Zero when
 * unavailable/out of range; word 1 is sequence, word 2 is board timestamp. */
ROBOT_IMPORT(imu_word) extern uint32_t imu_word(uint32_t index);
/* Zone 0..63, millimetres; -1 for invalid/unavailable/out of range. */
ROBOT_IMPORT(tof_mm) extern int32_t tof_mm(uint32_t zone);
/* Current commanded target, not a measured shaft position. */
ROBOT_IMPORT(servo_get_us) extern int32_t servo_get_us(uint32_t channel);
/* 0 accepted; -1 invalid channel/pulse, -2 revoked, -3 unavailable/busy.
 * Existing connected/motion policy remains; pulse 500..2500, channel 0..15.
 * HOLD additionally rejects if another producer changed its preceding GET. */
ROBOT_IMPORT(servo_set_us) extern int32_t servo_set_us(uint32_t channel, uint32_t us);
ROBOT_IMPORT(millis) extern uint32_t millis(void);
#endif
