/**
 * @file    leds.c
 * @brief   Simple helpers for driving the board status LED.
 */

#include "main.h"
#include "leds.h"

/**
 * @brief Turn the blue LED on.
 */
void led_on_blue(void)
{
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, LED_ON_STATE);
}

/**
 * @brief Turn the blue LED off.
 */
void led_off_blue(void)
{
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, LED_OFF_STATE);
}

/**
 * @brief Toggle the blue LED state.
 */
void led_blink_blue(void)
{
    GPIO_PinState state = HAL_GPIO_ReadPin(LED_BLUE_GPIO_Port, LED_BLUE_Pin);
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, (state == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE);
}
