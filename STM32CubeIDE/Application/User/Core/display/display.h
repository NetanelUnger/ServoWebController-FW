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
 * its static stack stays allocated. Start reinitializes the panel and scene.
 * Use these APIs, never externally suspend/delete displayTask mid-transfer. */
osStatus_t Display_Start(void);
osStatus_t Display_Stop(void);
void Display_Task(void *argument);

/* Board diagnostics only: pause under the display lock before writing CS/DC/RST.
 * GPIO_GET is passive. A manual write leaves playback stopped until Start/reset. */
osStatus_t Display_WriteControlPin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);

#endif
