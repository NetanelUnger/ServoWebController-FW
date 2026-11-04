#include "gc9a01.h"

/* Purchased GC9A01 module wiring differs from the schematic signal labels:
 * schematic RES (PB11) -> actual display DC (0=command, 1=data)
 * schematic D/C (PA4)  -> actual display CS (active low)
 * schematic CS  (PA2)  -> actual display RST (active low).
 * DISPLAY_* names in main.h and in this driver describe actual hardware functions.
 * DISPLAY_RES is retained as the firmware/BLE name for the actual RST signal. */
#include <string.h>
#include "gc9a01_init.h"

/* This buffer replaces a 115200-byte framebuffer. Calls must be serialized. */
static uint8_t scanline[GC9A01_WIDTH * 2U];

static void select_panel(bool selected)
{
    HAL_GPIO_WritePin(DISPLAY_CS_GPIO_Port, DISPLAY_CS_Pin,
                      selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static HAL_StatusTypeDef transmit(const uint8_t *data, uint16_t length)
{
    return HAL_SPI_Transmit(&DISPLAY_SPI, data, length, 100U);
}

/* CS remains asserted throughout each command and its arguments. */
static HAL_StatusTypeDef command(uint8_t cmd, const uint8_t *args, uint16_t count)
{
    select_panel(true);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = transmit(&cmd, 1U);
    if (status == HAL_OK && count != 0U) {
        HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_SET);
        status = transmit(args, count);
    }
    select_panel(false);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
    return status;
}

static bool valid_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    return x < GC9A01_WIDTH && y < GC9A01_HEIGHT && w != 0U && h != 0U
        && w <= GC9A01_WIDTH - x && h <= GC9A01_HEIGHT - y;
}

/* On success, CS stays low and DC high until end_pixels(). */
static HAL_StatusTypeDef begin_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!valid_rect(x, y, w, h)) return HAL_ERROR;
    uint16_t last_x = x + w - 1U, last_y = y + h - 1U;
    uint8_t cols[] = {x >> 8, x & 0xFF, last_x >> 8, last_x & 0xFF};
    uint8_t rows[] = {y >> 8, y & 0xFF, last_y >> 8, last_y & 0xFF};
    HAL_StatusTypeDef status = command(0x2A, cols, sizeof(cols));
    if (status != HAL_OK) return status;
    status = command(0x2B, rows, sizeof(rows));
    if (status != HAL_OK) return status;
    uint8_t ramwr = 0x2C;
    select_panel(true);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
    status = transmit(&ramwr, 1U);
    if (status != HAL_OK) { select_panel(false); return status; }
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_SET);
    return HAL_OK;
}

static void end_pixels(void)
{
    select_panel(false);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef GC9A01_Init(void)
{
    select_panel(false);
    HAL_GPIO_WritePin(DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(150U);
    size_t pos = 0;
    while (pos < sizeof(gc9a01_init_commands)) {
        uint8_t cmd = gc9a01_init_commands[pos++];
        if (cmd == 0U) return HAL_OK;
        if (pos >= sizeof(gc9a01_init_commands)) return HAL_ERROR;
        uint8_t flags = gc9a01_init_commands[pos++];
        uint8_t count = flags & 0x7FU;
        if (count > sizeof(gc9a01_init_commands) - pos) return HAL_ERROR;
        HAL_StatusTypeDef status = command(cmd, &gc9a01_init_commands[pos], count);
        if (status != HAL_OK) return status;
        pos += count;
        if (flags & 0x80U) HAL_Delay(150U);
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef GC9A01_SetRotation(uint8_t quarter_turns)
{
    static const uint8_t madctl[] = {0x48, 0x28, 0x88, 0xE8};
    if (quarter_turns > 3U) return HAL_ERROR;
    return command(0x36, &madctl[quarter_turns], 1U);
}

HAL_StatusTypeDef GC9A01_SetDisplayEnabled(bool enabled)
{
    HAL_StatusTypeDef status = command(enabled ? 0x29 : 0x28, NULL, 0U);
    if (status == HAL_OK) HAL_Delay(20U);
    return status;
}

HAL_StatusTypeDef GC9A01_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!valid_rect(x, y, w, h)) return HAL_ERROR;
    for (uint16_t i = 0; i < w; ++i) {
        scanline[2U * i] = color >> 8;
        scanline[2U * i + 1U] = color & 0xFF;
    }
    HAL_StatusTypeDef status = begin_pixels(x, y, w, h);
    if (status != HAL_OK) return status;
    for (uint16_t row = 0; row < h && status == HAL_OK; ++row)
        status = transmit(scanline, 2U * w);
    end_pixels();
    return status;
}

HAL_StatusTypeDef GC9A01_BlitRGB565(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                const uint16_t *pixels)
{
    if (pixels == NULL || !valid_rect(x, y, w, h)) return HAL_ERROR;
    HAL_StatusTypeDef status = begin_pixels(x, y, w, h);
    if (status != HAL_OK) return status;
    for (uint16_t row = 0; row < h && status == HAL_OK; ++row) {
        for (uint16_t col = 0; col < w; ++col) {
            uint16_t color = *pixels++;
            scanline[2U * col] = color >> 8;
            scanline[2U * col + 1U] = color & 0xFF;
        }
        status = transmit(scanline, 2U * w);
    }
    end_pixels();
    return status;
}

HAL_StatusTypeDef GC9A01_DrawText(uint16_t x, uint16_t y, const char *text,
                               const sFONT *font, uint16_t foreground, uint16_t background)
{
    if (text == NULL || font == NULL || font->table == NULL || font->Width == 0U
        || font->Height == 0U || x >= GC9A01_WIDTH || y >= GC9A01_HEIGHT)
        return HAL_ERROR;
    size_t length = strlen(text);
    if (length == 0U) return HAL_OK;
    if (length > (GC9A01_WIDTH - x) / font->Width || font->Height > GC9A01_HEIGHT - y)
        return HAL_ERROR;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)text[i] < 32U || (unsigned char)text[i] > 126U) return HAL_ERROR;

    uint16_t width = length * font->Width;
    uint16_t glyph_stride = (font->Width + 7U) / 8U;
    HAL_StatusTypeDef status = begin_pixels(x, y, width, font->Height);
    if (status != HAL_OK) return status;
    for (uint16_t row = 0; row < font->Height && status == HAL_OK; ++row) {
        for (uint16_t col = 0; col < width; ++col) {
            uint16_t glyph_col = col % font->Width;
            size_t glyph = (unsigned char)text[col / font->Width] - 32U;
            size_t offset = (glyph * font->Height + row) * glyph_stride + glyph_col / 8U;
            uint16_t color = (font->table[offset] & (0x80U >> (glyph_col % 8U)))
                ? foreground : background;
            scanline[2U * col] = color >> 8;
            scanline[2U * col + 1U] = color & 0xFF;
        }
        status = transmit(scanline, 2U * width);
    }
    end_pixels();
    return status;
}
