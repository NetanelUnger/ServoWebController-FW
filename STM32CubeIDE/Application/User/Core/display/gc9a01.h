/** GC9A01 240x240, four-wire SPI, RGB565. Single caller; no framebuffer. */
#ifndef GC9A01_H
#define GC9A01_H

#include "main.h"
#include "fonts.h"
#include <stdbool.h>

#define GC9A01_WIDTH 240U
#define GC9A01_HEIGHT 240U
#define GC9A01_BLACK 0x0000U
#define GC9A01_WHITE 0xFFFFU

/* Init leaves the display off so its first frame can be drawn before enabling. */
HAL_StatusTypeDef GC9A01_Init(void);
HAL_StatusTypeDef GC9A01_SetRotation(uint8_t quarter_turns);
HAL_StatusTypeDef GC9A01_SetDisplayEnabled(bool enabled);
HAL_StatusTypeDef GC9A01_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
HAL_StatusTypeDef GC9A01_DrawText(uint16_t x, uint16_t y, const char *text,
                               const sFONT *font, uint16_t foreground, uint16_t background);
/* Pixels are row-major native uint16_t RGB565; the driver sends high byte first.
 * The caller provides at least w*h pixels. Invalid/out-of-bounds rectangles fail. */
HAL_StatusTypeDef GC9A01_BlitRGB565(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                const uint16_t *pixels);

#endif
