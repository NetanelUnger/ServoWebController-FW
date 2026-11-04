#include "board_io.h"
#include "app_conf.h"
#include "display/display.h"
#include <string.h>

/* Radio temperature calibration is disabled in this project. If enabled later,
 * ADC access must first be integrated with ST's shared ADC controller. */
#if USE_TEMPERATURE_BASED_RADIO_CALIBRATION == 1
#error "Integrate board_io with the radio ADC controller before sharing ADC4"
#endif

typedef struct {
    const char *name;
    const char *pin_name;
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t writable;
} DigitalPin;

static const DigitalPin digital_pins[] = {
    {"EN1", "PB5", EN1_GPIO_Port, EN1_Pin, 1},
    {"EN2", "PB4", EN2_GPIO_Port, EN2_Pin, 1},
    {"DIAG_EN1", "PA10", DIAG_EN1_GPIO_Port, DIAG_EN1_Pin, 1},
    {"DIAG_EN2", "PA11", DIAG_EN2_GPIO_Port, DIAG_EN2_Pin, 1},
    {"OE", "PA9", OE_GPIO_Port, OE_Pin, 1},
    /* Signal names follow the real module, not the schematic labels. */
    {"DISPLAY_RES", "PA2", DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, 1},
    {"DISPLAY_CS", "PA4", DISPLAY_CS_GPIO_Port, DISPLAY_CS_Pin, 1},
    {"DISPLAY_DC", "PB11", DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, 1},
    {"INT_TOF", "PC13", INT_TOF_GPIO_Port, INT_TOF_Pin, 0},
};

static volatile uint32_t tof_interrupt_count;

static const DigitalPin *find_digital_pin(const char *name)
{
    for (size_t i = 0; i < sizeof(digital_pins) / sizeof(digital_pins[0]); ++i) {
        if (!strcmp(name, digital_pins[i].name) || !strcmp(name, digital_pins[i].pin_name)) {
            return &digital_pins[i];
        }
    }
    return NULL;
}

BoardIO_Status BoardIO_ReadGPIO(const char *name, const char **canonical, uint8_t *level)
{
    const DigitalPin *pin = find_digital_pin(name);
    if (pin == NULL) return BOARD_IO_UNKNOWN_PIN;
    *canonical = pin->name;
    *level = (uint8_t)HAL_GPIO_ReadPin(pin->port, pin->pin);
    return BOARD_IO_OK;
}

BoardIO_Status BoardIO_WriteGPIO(const char *name, uint8_t level, const char **canonical)
{
    const DigitalPin *pin = find_digital_pin(name);
    if (pin == NULL) return BOARD_IO_UNKNOWN_PIN;
    if (!pin->writable) return BOARD_IO_READ_ONLY;
    *canonical = pin->name;
    GPIO_PinState state = level ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if ((pin->port == DISPLAY_CS_GPIO_Port && pin->pin == DISPLAY_CS_Pin)
        || (pin->port == DISPLAY_DC_GPIO_Port && pin->pin == DISPLAY_DC_Pin)
        || (pin->port == DISPLAY_RES_GPIO_Port && pin->pin == DISPLAY_RES_Pin)) {
        /* Wait for the current drawing transaction, then pause animation and
         * write under the same lock. It stays paused until Display_Start/reset. */
        if (Display_WriteControlPin(pin->port, pin->pin, state) != osOK) Error_Handler();
    } else {
        HAL_GPIO_WritePin(pin->port, pin->pin, state);
    }
    return BOARD_IO_OK;
}

BoardIO_Status BoardIO_ReadADC(const char *name, const char **canonical, uint16_t *raw)
{
    static const struct { const char *name; const char *pin; uint32_t channel; } inputs[] = {
        {"ADC1", "PA3", ADC_CHANNEL_6},
        {"ADC2", "PB9", ADC_CHANNEL_10},
        {"ADC_BAT", "PA5", ADC_CHANNEL_4},
    };
    ADC_ChannelConfTypeDef config = {0};
    size_t i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        if (!strcmp(name, inputs[i].name) || !strcmp(name, inputs[i].pin)) break;
    }
    if (i == sizeof(inputs) / sizeof(inputs[0])) return BOARD_IO_UNKNOWN_PIN;
    *canonical = inputs[i].name;
    config.Channel = inputs[i].channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if (HAL_ADC_ConfigChannel(&hadc4, &config) != HAL_OK) return BOARD_IO_ADC_ERROR;

    /* Discard the first conversion after selecting the input; sample again
     * with the long acquisition time for the battery/current-sense networks. */
    for (unsigned sample = 0; sample < 2; ++sample) {
        if (HAL_ADC_Start(&hadc4) != HAL_OK || HAL_ADC_PollForConversion(&hadc4, 5U) != HAL_OK) {
            (void)HAL_ADC_Stop(&hadc4);
            return BOARD_IO_ADC_ERROR;
        }
        *raw = (uint16_t)HAL_ADC_GetValue(&hadc4);
        if (HAL_ADC_Stop(&hadc4) != HAL_OK) return BOARD_IO_ADC_ERROR;
    }
    return BOARD_IO_OK;
}

/* ISR: count edges only. Sensor work remains in its task. */
void BoardIO_TofInterrupt(void) { ++tof_interrupt_count; }
uint32_t BoardIO_TofInterruptCount(void) { return tof_interrupt_count; }
