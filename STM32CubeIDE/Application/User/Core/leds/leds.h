/**
 * @file    leds.h
 * @brief   Simple board LED helpers.
 */

#ifndef LEDS_H
#define LEDS_H

#include "main.h"

/** @brief Electrical level that turns the blue LED off. */
#define LED_OFF_STATE GPIO_PIN_SET
/** @brief Electrical level that turns the blue LED on. */
#define LED_ON_STATE GPIO_PIN_RESET

/** @brief Drive the blue LED into its active state. */
extern void led_on_blue(void);
/** @brief Drive the blue LED into its inactive state. */
extern void led_off_blue(void);
/** @brief Toggle the blue LED state. */
extern void led_blink_blue(void);

#endif /* LEDS_H */
