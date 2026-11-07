#include "display.h"
#include "image_store.h"
#include "gc9a01.h"
#include "config.h"
#include "device_identity.h"
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
static volatile uint8_t ble_connected;
static volatile uint8_t welcome_requested;

void Display_SetBleConnected(uint8_t connected)
{
    ble_connected = connected;
    if (!connected) {
        depth_enabled = 0U;
        welcome_requested = 1U;
        if (displayTaskHandle != NULL) (void)osThreadFlagsSet(displayTaskHandle, DISPLAY_WAKE);
    }
}

void Display_SetDepthEnabled(uint8_t enabled)
{
    if (enabled) depth_clear = 1U;
    depth_enabled = enabled;
}

/* Integer RGB565 red -> yellow -> green, with no color LUT in Flash. */
static uint16_t depth_color(uint16_t mm)
{
    if (mm == UINT16_MAX) return GC9A01_BLACK;
    if (mm > 4000U) mm = 4000U;
    uint16_t red = mm <= 2000U ? 31U : (4000U - mm) * 31U / 2000U;
    uint16_t green = mm >= 2000U ? 63U : (uint32_t)mm * 63U / 2000U;
    return (red << 11) | (green << 5);
}

uint8_t Display_DrawDepthMap(const uint16_t *distance_mm)
{
    if (!distance_mm || !depth_enabled || displayMutexHandle == NULL ||
        osMutexAcquire(displayMutexHandle, 0U) != osOK) return 0x81U;
    if (!ble_connected || welcome_requested || !running || needs_init || g_display_boot_status != HAL_OK) {
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
    char id[DEVICE_ID_DIGITS + 1U];
    DeviceIdentity_Format(id);
    const char *const lines[] = {"NatiLab", "1.0", id};
    const sFONT *font = &Font24;
    const uint16_t line_gap = 12U;
    uint16_t y = (GC9A01_HEIGHT - (3U * font->Height + 2U * line_gap)) / 2U;
    HAL_StatusTypeDef status = GC9A01_Init();
    if (status == HAL_OK) status = GC9A01_SetRotation(DISPLAY_ROTATION);
    if (status == HAL_OK)
        status = GC9A01_FillRect(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, GC9A01_WHITE);
    for (size_t i = 0; i < 3U && status == HAL_OK; ++i) {
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
    if (!ble_connected || welcome_requested || depth_enabled || !running || needs_init || g_display_boot_status != HAL_OK) {
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
    return Display_ShowFile(pixels, IMAGE_STORE_BYTES, 1, 1, 240, 240);
}

uint8_t Display_ShowFile(const uint8_t *pixels, uint32_t length, uint8_t type,
                         uint8_t layout, uint16_t width, uint16_t height)
{
    static uint8_t converted[480]; /* One RGB565 row, protected by displayMutex. */
    if (!pixels || !width || !height || width>240 || height>240 || layout>1 ||
        (type!=1 && type!=2) || (layout==1 && (width!=240 || height!=240)) ||
        length!=(layout==1?IMAGE_STORE_BYTES/2U:(uint32_t)width*height)*(type==1?2U:1U)) return 0x80;
    if(displayMutexHandle==NULL) return 0x81;
    if(osMutexAcquire(displayMutexHandle,0U)!=osOK) return 0x81;
    if(!ble_connected || welcome_requested || depth_enabled || !running || needs_init || g_display_boot_status!=HAL_OK) {osMutexRelease(displayMutexHandle);return 0x81;}
    HAL_StatusTypeDef status=HAL_OK;
    GC9A01_SetTransferTimeout(100U);
    /* Small rectangles are centred; clear stale pixels outside the image. */
    if(layout==0 && (width<240 || height<240)) status=GC9A01_FillRect(0,0,240,240,GC9A01_WHITE);
    for(uint16_t y=0;y<height && status==HAL_OK;y++) {
        uint16_t x=layout==1?ImageStore_RowLeft(y):(240U-width)/2U;
        uint16_t span=layout==1?240U-2U*x:width;
        const uint8_t *wire=pixels;
        if(type==2) {
            for(uint16_t i=0;i<span;i++) {
                uint8_t c=pixels[i],r=c>>5,g=(c>>2)&7,b=c&3;
                uint16_t v=(((r<<2)|(r>>1))<<11)|(((g<<3)|g)<<5)|((b<<3)|(b<<1)|(b>>1));
                converted[2*i]=v>>8;converted[2*i+1]=v;
            }
            wire=converted;
        }
        status=GC9A01_BlitWire(x,y+(layout==0?(240U-height)/2U:0),span,1,wire);
        pixels+=(type==1?2U:1U)*span;
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
        if (welcome_requested) {
            welcome_requested = 0U;
            running = true;
            needs_init = true;
        }
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
void Display_SetBleConnected(uint8_t c) { UNUSED(c); }
void Display_SetDepthEnabled(uint8_t e) { UNUSED(e); }
uint8_t Display_DrawDepthMap(const uint16_t *p) { UNUSED(p); return 0x83U; }
uint8_t Display_ShowStored(const uint8_t *p) { UNUSED(p); return 0x83U; }
uint8_t Display_ShowFile(const uint8_t *p,uint32_t n,uint8_t t,uint8_t l,uint16_t w,uint16_t h) {
    UNUSED(p);UNUSED(n);UNUSED(t);UNUSED(l);UNUSED(w);UNUSED(h);return 0x83U;
}
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
