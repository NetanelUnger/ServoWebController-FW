#include "display.h"
#include "gc9a01.h"
#include "config.h"
#include "app_freertos.h"
#include "face_animation.h"
#include <string.h>

volatile HAL_StatusTypeDef g_display_boot_status = HAL_ERROR;
volatile HAL_StatusTypeDef g_display_frame_status = HAL_ERROR;
volatile uint32_t g_display_frame_count;

#if ENABLE_DISPLAY
#define DISPLAY_WAKE 1U
/* State and all panel writes are protected by displayMutex. */
static bool running = true;
static bool needs_init = true;

static HAL_StatusTypeDef show_welcome(void)
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

void Display_Task(void *argument)
{
    UNUSED(argument);
    bool welcome = false;
    uint32_t welcome_start = 0;
    for (;;) {
        uint32_t wait_ticks = osWaitForever;
        if (osMutexAcquire(displayMutexHandle, osWaitForever) != osOK) Error_Handler();
        if (running) {
            uint32_t frame_start = osKernelGetTickCount();
            HAL_StatusTypeDef status = HAL_OK;
            if (needs_init) {
                /* HAL RTC timebase is ready only after MX_APPE_Init/scheduler. */
                status = show_welcome();
                g_display_boot_status = status;
                needs_init = false;
                welcome = true;
                welcome_start = HAL_GetTick();
            } else if (welcome) {
                if ((uint32_t)(HAL_GetTick() - welcome_start) >= DISPLAY_WELCOME_MS) {
                    status = FaceAnimation_Init(HAL_GetTick());
                    welcome = false;
                    if (status == HAL_OK) ++g_display_frame_count;
                }
            } else {
                status = FaceAnimation_Update(HAL_GetTick());
                if (status == HAL_OK) ++g_display_frame_count;
            }
            g_display_frame_status = status;
            if (status != HAL_OK) {
                /* No retry loop that could waste CPU or interfere with BLE.
                 * Start() retries initialization after the fault is resolved. */
                running = false;
            } else if (welcome) {
                uint32_t elapsed = HAL_GetTick() - welcome_start;
                uint32_t remaining = elapsed < DISPLAY_WELCOME_MS ? DISPLAY_WELCOME_MS - elapsed : 1U;
                wait_ticks = pdMS_TO_TICKS(remaining);
                if (wait_ticks == 0U) wait_ticks = 1U;
            } else {
                uint32_t elapsed = osKernelGetTickCount() - frame_start;
                uint32_t period = pdMS_TO_TICKS(DISPLAY_FRAME_PERIOD_MS);
                wait_ticks = elapsed < period ? period - elapsed : 1U;
            }
        }
        (void)osMutexRelease(displayMutexHandle);
        /* Wake flags also interrupt the welcome delay. A stopped task never polls. */
        (void)osThreadFlagsWait(DISPLAY_WAKE, osFlagsWaitAny, wait_ticks);
    }
}
#else
osStatus_t Display_Start(void) { return osErrorResource; }
osStatus_t Display_Stop(void) { return osOK; }
osStatus_t Display_WriteControlPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    HAL_GPIO_WritePin(port, pin, level);
    return osOK;
}
void Display_Task(void *argument) { UNUSED(argument); }
#endif
