#include "display.h"
#include "gc9a01.h"
#include "config.h"
#include <string.h>

volatile HAL_StatusTypeDef g_display_boot_status = HAL_ERROR;

HAL_StatusTypeDef Display_ShowWelcome(void)
{
    static const char *const lines[] = {"Welcome To", "NatiLab 1.0"};
    const sFONT *font = &Font24;
    const uint16_t line_gap = 12U;
    uint16_t y = (GC9A01_HEIGHT - (2U * font->Height + line_gap)) / 2U;
    HAL_StatusTypeDef status = GC9A01_Init();
    if (status == HAL_OK) status = GC9A01_SetRotation(DISPLAY_ROTATION);
    if (status == HAL_OK)
        status = GC9A01_FillRect(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, GC9A01_WHITE);
    for (size_t i = 0; i < 2U && status == HAL_OK; ++i) {
        uint16_t x = (GC9A01_WIDTH - strlen(lines[i]) * font->Width) / 2U;
        status = GC9A01_DrawText(x, y, lines[i], font, GC9A01_BLACK, GC9A01_WHITE);
        y += font->Height + line_gap;
    }
    if (status == HAL_OK) status = GC9A01_SetDisplayEnabled(true);
    return status;
}
