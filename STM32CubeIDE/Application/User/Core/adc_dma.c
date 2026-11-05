#include "adc_dma.h"
#include "adc_filter.h"
#include "main.h"
#include "app_conf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include <string.h>

#if USE_TEMPERATURE_BASED_RADIO_CALIBRATION == 1
#error "Integrate ADC DMA with the radio ADC controller before sharing ADC4"
#endif

extern DMA_HandleTypeDef handle_GPDMA1_Channel6;
extern TIM_HandleTypeDef htim2;
/* TIM2 triggers six ranks: BAT/BAT, ADC1/ADC1, ADC2/ADC2.
 * Discard the first result after each mux change, as in the polling driver.
 * Half/full callbacks consume 32 complete sequences every 32 ms. */
#define HALF_SAMPLES (32U * 6U)
static _Alignas(4) uint16_t dma_samples[HALF_SAMPLES * 2U];
static DMA_NodeTypeDef node;
static DMA_QListTypeDef queue;
static ADC_Filter accumulators[3];
static volatile uint16_t latest[3];
static volatile uint32_t last_tick, blocks, errors;
/* Debugger-readable: blocks, error count, window samples, last DMA/ADC error. */
volatile uint32_t g_adc_dma_debug[4];

static void consume(unsigned start)
{
    for (unsigned i = start; i < start + HALF_SAMPLES; i += 6U) {
        for (unsigned ch = 0; ch < 3U; ++ch) {
            uint16_t value = dma_samples[i + 2U * ch + 1U];
            latest[ch] = value;
            ADC_FilterAdd(&accumulators[ch], value);
        }
    }
    last_tick = xTaskGetTickCountFromISR();
    g_adc_dma_debug[0] = ++blocks;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc4) consume(0);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc4) consume(HALF_SAMPLES);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc4) {
        g_adc_dma_debug[1] = ++errors;
        g_adc_dma_debug[3] = hadc->ErrorCode;
    }
}

void ADC_DMA_Init(void)
{
    DMA_NodeConfTypeDef config = {0};
    config.NodeType = DMA_GPDMA_LINEAR_NODE;
    config.Init.Request = GPDMA1_REQUEST_ADC4;
    config.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    config.Init.Direction = DMA_PERIPH_TO_MEMORY;
    config.Init.SrcInc = DMA_SINC_FIXED;
    config.Init.DestInc = DMA_DINC_INCREMENTED;
    config.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_HALFWORD;
    config.Init.DestDataWidth = DMA_DEST_DATAWIDTH_HALFWORD;
    config.Init.SrcBurstLength = config.Init.DestBurstLength = 1;
    config.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    config.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    config.Init.Mode = DMA_NORMAL;
    config.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    config.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    config.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    config.SrcAddress = (uint32_t)&ADC4->DR;
    config.DstAddress = (uint32_t)dma_samples;
    config.DataSize = sizeof(dma_samples);
    if (HAL_DMAEx_List_BuildNode(&config, &node) != HAL_OK ||
        HAL_DMAEx_List_InsertNode_Tail(&queue, &node) != HAL_OK ||
        HAL_DMAEx_List_SetCircularMode(&queue) != HAL_OK ||
        HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel6, &queue) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(&hadc4, DMA_Handle, handle_GPDMA1_Channel6);
    HAL_NVIC_SetPriority(GPDMA1_Channel6_IRQn, 7, 0);
    HAL_NVIC_SetPriority(ADC4_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(ADC4_IRQn);
    if (HAL_ADC_Start_DMA(&hadc4, (uint32_t *)dma_samples, HALF_SAMPLES * 2U) != HAL_OK ||
        HAL_TIM_Base_Start(&htim2) != HAL_OK) Error_Handler();
}

uint8_t ADC_DMA_Read(unsigned channel, uint16_t *raw)
{
    if (channel >= 3U || !raw || !blocks || errors ||
        (TickType_t)(xTaskGetTickCount() - last_tick) > pdMS_TO_TICKS(100)) return 0;
    *raw = latest[channel];
    return 1;
}

uint8_t ADC_DMA_Window(uint16_t values[3])
{
    ADC_Filter window[3];
    taskENTER_CRITICAL();
    memcpy(window, accumulators, sizeof(window));
    memset(accumulators, 0, sizeof(accumulators));
    uint8_t valid = blocks && !errors &&
        (TickType_t)(xTaskGetTickCount() - last_tick) <= pdMS_TO_TICKS(100);
    taskEXIT_CRITICAL();
    g_adc_dma_debug[2] = window[0].count;
    if (!valid || !window[0].count) return 0;
    for (unsigned i = 0; i < 3; ++i) values[i] = ADC_FilterValue(&window[i]);
    return 7;
}
