#ifndef DISPLAY_H
#define DISPLAY_H

#include "main.h"
/* HAL status of startup SPI transfers; not confirmation that the panel is lit. */
extern volatile HAL_StatusTypeDef g_display_boot_status;
HAL_StatusTypeDef Display_ShowWelcome(void);

#endif
