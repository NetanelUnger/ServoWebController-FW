/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32wbaxx_hal.h"
#include "app_conf.h"
#include "app_entry.h"
#include "app_common.h"
#include "app_debug.h"

#include "stm32wbaxx_ll_icache.h"
#include "stm32wbaxx_ll_bus.h"
#include "stm32wbaxx_ll_cortex.h"
#include "stm32wbaxx_ll_rcc.h"
#include "stm32wbaxx_ll_system.h"
#include "stm32wbaxx_ll_utils.h"
#include "stm32wbaxx_ll_pwr.h"
#include "stm32wbaxx_ll_gpio.h"
#include "stm32wbaxx_ll_dma.h"

#include "stm32wbaxx_ll_exti.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cmsis_os2.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_GPIO_Init(void);
void MX_GPDMA1_Init(void);
void MX_RAMCFG_Init(void);
void MX_RTC_Init(void);
void MX_USART1_UART_Init(void);
void MX_CRC_Init(void);
void MX_RNG_Init(void);
void MX_ICACHE_Init(void);

/* USER CODE BEGIN EFP */
void ConfigureStandbyWakeupPins(void);
void APP_SetPerformanceMaximum(void);
void APP_SetPerformanceMinimum(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define I2C_SDA_Pin GPIO_PIN_7
#define I2C_SDA_GPIO_Port GPIOA
#define I2C_SCL_Pin GPIO_PIN_6
#define I2C_SCL_GPIO_Port GPIOA
#define EN2_Pin GPIO_PIN_4
#define EN2_GPIO_Port GPIOB
#define EN1_Pin GPIO_PIN_5
#define EN1_GPIO_Port GPIOB
#define SPI_MOSI_Pin GPIO_PIN_8
#define SPI_MOSI_GPIO_Port GPIOB
#define CURRENT_SENSE2_Pin GPIO_PIN_9
#define CURRENT_SENSE2_GPIO_Port GPIOB
#define DISPLAY_RES_Pin GPIO_PIN_2
#define DISPLAY_RES_GPIO_Port GPIOA
#define INT_TOF_Pin GPIO_PIN_13
#define INT_TOF_GPIO_Port GPIOC
#define SPI_SCK_Pin GPIO_PIN_0
#define SPI_SCK_GPIO_Port GPIOA
#define DISPLAY_CS_Pin GPIO_PIN_4
#define DISPLAY_CS_GPIO_Port GPIOA
#define CURRENT_SENSE1_Pin GPIO_PIN_3
#define CURRENT_SENSE1_GPIO_Port GPIOA
#define DISPLAY_DC_Pin GPIO_PIN_11
#define DISPLAY_DC_GPIO_Port GPIOB
#define ADC_BAT_Pin GPIO_PIN_5
#define ADC_BAT_GPIO_Port GPIOA
#define OE_Pin GPIO_PIN_9
#define OE_GPIO_Port GPIOA
#define DIAG_EN2_Pin GPIO_PIN_11
#define DIAG_EN2_GPIO_Port GPIOA
#define DIAG_EN1_Pin GPIO_PIN_10
#define DIAG_EN1_GPIO_Port GPIOA
#define MUX_SEL1_Pin GPIO_PIN_1
#define MUX_SEL1_GPIO_Port GPIOA
#define LED_GREEN_Pin GPIO_PIN_6
#define LED_GREEN_GPIO_Port GPIOB
#define LED_BLUE_Pin GPIO_PIN_7
#define LED_BLUE_GPIO_Port GPIOB
#define BAT_ON_Pin GPIO_PIN_12
#define BAT_ON_GPIO_Port GPIOA
#define CHRG_CONN_Pin GPIO_PIN_15
#define CHRG_CONN_GPIO_Port GPIOB
#define INT_TOF_EXTI_IRQn EXTI13_IRQn
#define CHRG_CONN_EXTI_IRQn EXTI15_IRQn

/* USER CODE BEGIN Private defines */
/* Purchased GC9A01 module wiring differs from the schematic signal labels:
 * schematic RES (PB11) -> actual display DC (0=command, 1=data)
 * schematic D/C (PA4)  -> actual display CS (active low)
 * schematic CS  (PA2)  -> actual display RST (active low).
 * DISPLAY_* names describe actual hardware functions.
 * DISPLAY_RES is retained as the firmware/BLE name for the actual RST signal. */
extern RTC_HandleTypeDef hrtc;
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern DMA_NodeTypeDef GPIONode;
extern DMA_QListTypeDef GPIOQueue;
extern DMA_NodeTypeDef ADCNode;
extern DMA_QListTypeDef ADCQueue;
extern I2C_HandleTypeDef hi2c3;
extern ADC_HandleTypeDef hadc4;
extern SPI_HandleTypeDef hspi3;

#define MUX_SEL_DMA_CHAN	handle_GPDMA1_Channel5
#define ADC_DMA_CHAN		handle_GPDMA1_Channel6
#define ADC_DMA_TIMER		htim1
#define SAMPLING_ADC		hadc4
#define I2C_BUS              hi2c3
#define DISPLAY_SPI          hspi3
#define LOGGER_TIM			htim2

extern uint32_t LPTime_GetElapsedMs(void);

extern uint16_t g_ms_accum;

extern HAL_StatusTypeDef MX_ADCQueue_Config_fixed(void);
extern HAL_StatusTypeDef MX_GPIOQueue_Config_fixed(void);


#define BREAK_UINT32( var, ByteNum ) \
          (uint8_t)((uint32_t)(((var) >>((ByteNum) * 8)) & 0x00FF))

#define BUILD_UINT32(Byte0, Byte1, Byte2, Byte3) \
          ((uint32_t)((uint32_t)((Byte0) & 0x00FF) \
          + ((uint32_t)((Byte1) & 0x00FF) << 8) \
          + ((uint32_t)((Byte2) & 0x00FF) << 16) \
          + ((uint32_t)((Byte3) & 0x00FF) << 24)))

#define BUILD_UINT64(Byte0, Byte1, Byte2, Byte3, Byte4, Byte5, Byte6, Byte7) \
    ((uint64_t)((uint64_t)((Byte0) & 0x00FF) \
    + ((uint64_t)((Byte1) & 0x00FF) << 8) \
    + ((uint64_t)((Byte2) & 0x00FF) << 16) \
    + ((uint64_t)((Byte3) & 0x00FF) << 24) \
    + ((uint64_t)((Byte4) & 0x00FF) << 32) \
    + ((uint64_t)((Byte5) & 0x00FF) << 40) \
    + ((uint64_t)((Byte6) & 0x00FF) << 48) \
    + ((uint64_t)((Byte7) & 0x00FF) << 56)))

#define BUILD_UINT16(loByte, hiByte) \
          ((uint16_t)(((loByte) & 0x00FF) + (((hiByte) & 0x00FF) << 8)))

#define HI_UINT16(a) (((a) >> 8) & 0xFF)
#define LO_UINT16(a) ((a) & 0xFF)


#define PUT_U16(buf, val)                 \
    do {                                          \
        *(uint16_t *)(buf) = (uint16_t)(val);     \
    } while (0)

#define PUT_U32(buf, val)                  \
    do {                                   \
        *(uint32_t *)(buf) = (uint32_t)(val); \
    } while (0)

// Read uint16 from buffer (little-endian)
#define GET_U16(buf) \
    (*(const uint16_t *)(buf))

// Read uint32 from buffer (little-endian)
#define GET_U32(buf) \
    (*(const uint32_t *)(buf))

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
