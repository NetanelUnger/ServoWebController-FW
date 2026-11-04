/** Color face playback. Called only by the display owner, using elapsed ms. */
#ifndef FACE_ANIMATION_H
#define FACE_ANIMATION_H

#include "gc9a01.h"

HAL_StatusTypeDef FaceAnimation_Init(uint32_t now_ms);
/* Unchanged poses generate no SPI traffic. No RTOS calls or heap allocation. */
HAL_StatusTypeDef FaceAnimation_Update(uint32_t now_ms);

#endif
