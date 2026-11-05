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
/* Raw 12-bit result; no assumption about VDDA, divider ratio or current scaling. */
BoardIO_Status BoardIO_ReadADC(const char *name, const char **canonical, uint16_t *raw);
void BoardIO_TofInterrupt(void);
uint32_t BoardIO_TofInterruptCount(void);
/* BLE link owns MPQ enable/diagnostic pins; no bus access or blocking calls. */
void BoardIO_SetPowerConnected(uint8_t connected);

#endif
