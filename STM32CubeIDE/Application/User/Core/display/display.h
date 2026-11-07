#ifndef DISPLAY_H
#define DISPLAY_H

#include "main.h"
#include "cmsis_os2.h"
/* HAL status of startup SPI transfers; not confirmation that the panel is lit. */
extern volatile HAL_StatusTypeDef g_display_boot_status;
extern volatile HAL_StatusTypeDef g_display_frame_status;
extern volatile uint32_t g_display_frame_count;

/* Task-context APIs only. Stop waits for the in-flight draw, then prevents any
 * more SPI work and leaves the last image visible. The task blocks indefinitely;
 * its static stack stays allocated. Start reinitializes the panel and startup mark.
 * Use these APIs, never externally suspend/delete displayTask mid-transfer. */
/* Callback-safe: schedule the disconnected welcome view on the display task. */
void Display_SetBleConnected(uint8_t connected);
osStatus_t Display_Start(void);
osStatus_t Display_Stop(void);
void Display_Task(void *argument);
/* Packet is borrowed until return; result is a protocol ATT error or zero. */
/* Depth mode owns the panel; no framebuffer or retained distance pointer.
 * Set is callback-safe; disabling cancels at the next tile boundary. */
void Display_SetDepthEnabled(uint8_t enabled);
uint8_t Display_DrawDepthMap(const uint16_t *distance_mm);
uint8_t Display_ShowStored(const uint8_t *pixels);
uint8_t Display_ShowFile(const uint8_t *pixels, uint32_t length, uint8_t type,
                         uint8_t layout, uint16_t width, uint16_t height);
uint8_t Display_WritePacket(const uint8_t *packet, uint16_t length);

/* Board diagnostics only: pause under the display lock before writing CS/DC/RST.
 * GPIO_GET is passive. A manual write leaves playback stopped until Start/reset. */
osStatus_t Display_WriteControlPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);

#endif
