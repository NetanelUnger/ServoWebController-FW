#ifndef ADC_DMA_H
#define ADC_DMA_H
#include <stdint.h>
#define ADC_DMA_SAMPLE_HZ 1000U
void ADC_DMA_Init(void);
/* BAT, current 1, current 2. Latest raw sample, without touching the sequencer. */
uint8_t ADC_DMA_Read(unsigned channel, uint16_t *raw);
/* Drain the current window and compute amplitude-weighted mean sum(x*x)/sum(x). */
uint8_t ADC_DMA_Window(uint16_t values[3]);
#endif
