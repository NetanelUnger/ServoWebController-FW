#include "display.h"
#include "image_store.h"
#include "gc9a01.h"
#include "config.h"
#include "app_freertos.h"

#include <string.h>

volatile HAL_StatusTypeDef g_display_boot_status = HAL_ERROR;
volatile HAL_StatusTypeDef g_display_frame_status = HAL_ERROR;
volatile uint32_t g_display_frame_count;

#if ENABLE_DISPLAY
#define DISPLAY_WAKE 1U
/* State and all panel writes are protected by displayMutex. */
static bool running = true;
static bool needs_init = true;
static volatile uint8_t depth_enabled;
static volatile uint8_t depth_clear;

void Display_SetDepthEnabled(uint8_t enabled)
{
    if (enabled) depth_clear = 1U;
    depth_enabled = enabled;
}

/* Integer RGB565 red -> yellow -> green, with no color LUT in Flash. */
static uint16_t depth_color(uint16_t mm)
{
    if (mm == UINT16_MAX) return GC9A01_BLACK;
    if (mm > 8800U) mm = 8800U;
    uint16_t red = mm <= 4400U ? 31U : (8800U - mm) * 31U / 4400U;
    uint16_t green = mm >= 4400U ? 63U : (uint32_t)mm * 63U / 4400U;
    return (red << 11) | (green << 5);
}

uint8_t Display_DrawDepthMap(const uint16_t *distance_mm)
{
    if (!distance_mm || !depth_enabled || displayMutexHandle == NULL ||
        osMutexAcquire(displayMutexHandle, 0U) != osOK) return 0x81U;
    if (!running || needs_init || g_display_boot_status != HAL_OK) {
        osMutexRelease(displayMutexHandle); return 0x81U;
    }
    HAL_StatusTypeDef status = HAL_OK;
    GC9A01_SetTransferTimeout(100U);
    if (depth_clear) {
        status = GC9A01_FillRect(0, 0, 240, 240, GC9A01_BLACK);
        if (status == HAL_OK) depth_clear = 0U;
    }
    /* 168x168 at (36,36): every corner fits inside the round 240px panel.
     * Reuse the driver's bounded solid-fill scratch space, 64 calls per frame. */
    for (uint8_t zone = 0; zone < 64U && status == HAL_OK && depth_enabled; ++zone)
        status = GC9A01_FillRect(36U + (zone % 8U) * 21U,
                                36U + (zone / 8U) * 21U,
                                21U, 21U, depth_color(distance_mm[zone]));
    GC9A01_SetTransferTimeout(1000U);
    g_display_frame_status = status;
    if (status == HAL_OK && depth_enabled) ++g_display_frame_count;
    osMutexRelease(displayMutexHandle);
    return status == HAL_OK ? 0U : 0x82U;
}

static HAL_StatusTypeDef show_welcome(void)
{
    static const char *const lines[] = {"NatiLab", "1.0"};
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

static osStatus_t lock_display(void)
{
    if (displayMutexHandle == NULL || displayTaskHandle == NULL) return osErrorResource;
    return osMutexAcquire(displayMutexHandle, osWaitForever);
}

osStatus_t Display_Start(void)
{
    osStatus_t status = lock_display();
    if (status != osOK) return status;
    if (!running) { needs_init = true; running = true; }
    (void)osThreadFlagsSet(displayTaskHandle, DISPLAY_WAKE);
    return osMutexRelease(displayMutexHandle);
}

osStatus_t Display_Stop(void)
{
    osStatus_t status = lock_display();
    if (status != osOK) return status;
    running = false;
    (void)osThreadFlagsSet(displayTaskHandle, DISPLAY_WAKE);
    return osMutexRelease(displayMutexHandle);
}

osStatus_t Display_WriteControlPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    osStatus_t status = lock_display();
    if (status != osOK) return status;
    running = false;
    HAL_GPIO_WritePin(port, pin, level);
    (void)osThreadFlagsSet(displayTaskHandle, DISPLAY_WAKE);
    return osMutexRelease(displayMutexHandle);
}

/* Synchronous borrowed-buffer operation. Never wait behind startup/diagnostics
 * in the BLE dispatcher; caller retries BUSY. No pointer escapes this call. */
uint8_t Display_WritePacket(const uint8_t *p, uint16_t length)
{
    if (p == NULL || length < 5U || length > 247U) return 0x80U;
    uint16_t x = p[1], y = p[2], w, h;
    const uint8_t *pixels;
    if (p[0] == 3U) { /* WRITE_ROW: opcode, x, y, then RGB565 BE pairs. */
        if ((length - 3U) % 2U != 0U) return 0x80U;
        w = (length - 3U) / 2U;
        h = 1U;
        pixels = p + 3;
    } else {
        if (length < 7U) return 0x80U;
        w = p[3];
        h = p[4];
        uint32_t count = (uint32_t)w * h;
        if ((p[0] == 2U && count > 120U) ||
            (p[0] == 1U && length != 7U) ||
            (p[0] == 2U && length != 5U + 2U * count) ||
            (p[0] != 1U && p[0] != 2U)) return 0x80U;
        pixels = p + 5;
    }
    /* Never wrap to the next row or clip silently. Validate before any SPI. */
    if (w == 0U || h == 0U || x + w > 240U || y + h > 240U) return 0x80U;
    if (displayMutexHandle == NULL || osMutexAcquire(displayMutexHandle, 0U) != osOK)
        return 0x81U;
    if (depth_enabled || !running || needs_init || g_display_boot_status != HAL_OK) {
        (void)osMutexRelease(displayMutexHandle);
        return 0x81U;
    }
    HAL_StatusTypeDef status;
    /* HAL tick advances in 10 ms steps (100 Hz). A 10 ms deadline can
     * expire on the next tick even for a sub-millisecond SPI transfer. */
    GC9A01_SetTransferTimeout(100U);
    if (p[0] == 1U)
        status=GC9A01_FillRect(x,y,w,h,((uint16_t)p[5]<<8)|p[6]);
    else
        status=GC9A01_BlitWire(x,y,w,h,pixels);
    GC9A01_SetTransferTimeout(1000U);
    g_display_frame_status=status;
    if (status == HAL_OK) ++g_display_frame_count;
    /* HAL closes failed transfers; the next packet sets a fresh address window.
     * Do not permanently disable remote drawing after a transient SPI fault. */
    (void)osMutexRelease(displayMutexHandle);
    return status == HAL_OK ? 0U : 0x82U;
}

uint8_t Display_ShowStored(const uint8_t *pixels)
{
    if(osMutexAcquire(displayMutexHandle,0U)!=osOK) return 0x81;
    if(depth_enabled || !running || needs_init || g_display_boot_status!=HAL_OK) {osMutexRelease(displayMutexHandle);return 0x81;}
    HAL_StatusTypeDef status=HAL_OK;
    GC9A01_SetTransferTimeout(100U);
    for(uint16_t y=0;y<240 && status==HAL_OK;y++) {
        uint16_t x=ImageStore_RowLeft(y), width=240U-2U*x;
        status=GC9A01_BlitWire(x,y,width,1,pixels);pixels+=2U*width;
    }
    GC9A01_SetTransferTimeout(1000U);
    g_display_frame_status=status;if(status==HAL_OK) ++g_display_frame_count;
    osMutexRelease(displayMutexHandle);
    return status==HAL_OK?0:0x82;
}

void Display_Task(void *argument)
{
    UNUSED(argument);
    for (;;) {
        if (osMutexAcquire(displayMutexHandle, osWaitForever) != osOK) Error_Handler();
        if (running && needs_init) {
            g_display_boot_status = show_welcome();
            g_display_frame_status = g_display_boot_status;
            needs_init = false;
            if (g_display_boot_status != HAL_OK) running = false;
        }
        (void)osMutexRelease(displayMutexHandle);
        ImageStore_Process();
        (void)osThreadFlagsWait(DISPLAY_WAKE, osFlagsWaitAny, osWaitForever);
    }
}
#else
void Display_SetDepthEnabled(uint8_t e) { UNUSED(e); }
uint8_t Display_DrawDepthMap(const uint16_t *p) { UNUSED(p); return 0x83U; }
uint8_t Display_ShowStored(const uint8_t *p) { UNUSED(p); return 0x83U; }
uint8_t Display_WritePacket(const uint8_t *p, uint16_t n) { UNUSED(p); UNUSED(n); return 0x83U; }
osStatus_t Display_Start(void) { return osErrorResource; }
osStatus_t Display_Stop(void) { return osOK; }
osStatus_t Display_WriteControlPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    HAL_GPIO_WritePin(port, pin, level);
    return osOK;
}
void Display_Task(void *argument) { UNUSED(argument); }
#endif
