#ifndef ADC_FILTER_H
#define ADC_FILTER_H
#include <stdint.h>
typedef struct { uint64_t squares; uint32_t sum, count; } ADC_Filter;
static inline void ADC_FilterAdd(ADC_Filter *filter, uint16_t value)
{
    /* Bound sums even if the consumer is temporarily stalled. */
    if (filter->count >= 65535U) return;
    filter->sum += value;
    filter->squares += (uint32_t)value * value;
    ++filter->count;
}
static inline uint16_t ADC_FilterValue(const ADC_Filter *filter)
{
    return filter->sum ? (uint16_t)((filter->squares + filter->sum / 2U) / filter->sum) : 0U;
}
#endif
