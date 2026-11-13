/** Board bring-up I/O. Task calls are serialized by commTask. */
#ifndef BOARD_IO_H
#define BOARD_IO_H

#include "main.h"

typedef enum {
    BOARD_IO_OK,
    BOARD_IO_UNKNOWN_PIN,
    BOARD_IO_READ_ONLY,
    BOARD_IO_ADC_ERROR
} BoardIO_Status;

BoardIO_Status BoardIO_ReadGPIO(const char *name, const char **canonical, uint8_t *level);
BoardIO_Status BoardIO_WriteGPIO(const char *name, uint8_t level, const char **canonical);
/* Latest 12-bit DMA sample; never reconfigure ADC4 or interrupt its scan. */
BoardIO_Status BoardIO_ReadADC(const char *name, const char **canonical, uint16_t *raw);
void BoardIO_TofInterrupt(void);
uint32_t BoardIO_TofInterruptCount(void);
/* BLE link owns MPQ enable/diagnostic pins and gates PWM; no blocking calls.
 * Disconnect disables PWM before EN/DIAG_EN; the servo safe-pose worker can
 * restore power after writing its frame. Init may not bypass this gate. */
void BoardIO_SetPowerConnected(uint8_t connected);
void BoardIO_SetPwmReady(uint8_t ready);
/* Only servoTask may enable disconnected power, after a safe frame succeeds. */
void BoardIO_SetSafePoseActive(uint8_t active);
void BoardIO_PrepareSafePose(void);

#endif
