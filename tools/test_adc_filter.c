#include <assert.h>
#include <stdio.h>
#include "../STM32CubeIDE/Application/User/Core/adc_filter.h"
int main(void)
{
    ADC_Filter zero = {0}, steady = {0}, spike = {0}, full = {0};
    assert(ADC_FilterValue(&zero) == 0);
    for (unsigned i = 0; i < 100; ++i) {
        ADC_FilterAdd(&zero, 0);
        ADC_FilterAdd(&steady, 1234);
        ADC_FilterAdd(&spike, i == 99 ? 2000 : 100);
    }
    assert(zero.count == 100 && ADC_FilterValue(&zero) == 0);
    assert(ADC_FilterValue(&steady) == 1234);
    /* Arithmetic mean would be 119; amplitude weighting preserves the pulse. */
    assert(ADC_FilterValue(&spike) == 419);
    for (unsigned i = 0; i < 70000; ++i) ADC_FilterAdd(&full, 4095);
    assert(full.count == 65535 && ADC_FilterValue(&full) == 4095);
    puts("PASS zero, constant signal, brief high pulse, 12-bit maximum and accumulator bounds");
    return 0;
}
