#ifndef ADC_BLE_H
#define ADC_BLE_H

#include <stdint.h>

/* Service 91bad4b0 / RAW 91bad4b1, suffix b950-4226-aa2b-4ede9fa42f59.
 * READ/NOTIFY: version=2, validity bits BAT/ADC1/ADC2, then three RAW LE uint16, then battery millivolts LE uint16.
 * Counts are unscaled, amplitude-weighted RAW window means from adc_dma.
 * READ returns the latest published window. defaultTask publishes at ~5 Hz. */
#define ADC_BLE_RAW_LENGTH 10U
#define ADC_BLE_INTERVAL_MS 200U
void ADC_BLE_Init(void);
void ADC_BLE_Disconnected(uint16_t handle);
uint8_t ADC_BLE_Enabled(void);
void ADC_BLE_Step(void);

#endif
