#include "board_io.h"
#include "app_conf.h"
#include "adc_dma.h"
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
    /* Readable for diagnostics, but only BLE lifecycle may change power. */
    {"EN1", "PB5", EN1_GPIO_Port, EN1_Pin, 0},
    {"EN2", "PB4", EN2_GPIO_Port, EN2_Pin, 0},
    {"DIAG_EN1", "PA10", DIAG_EN1_GPIO_Port, DIAG_EN1_Pin, 0},
    {"DIAG_EN2", "PA11", DIAG_EN2_GPIO_Port, DIAG_EN2_Pin, 0},
    {"OE", "PA9", OE_GPIO_Port, OE_Pin, 1},
    /* Signal names follow the real module, not the schematic labels. */
    {"DISPLAY_RES", "PA2", DISPLAY_RES_GPIO_Port, DISPLAY_RES_Pin, 1},
    {"DISPLAY_CS", "PA4", DISPLAY_CS_GPIO_Port, DISPLAY_CS_Pin, 1},
    {"DISPLAY_DC", "PB11", DISPLAY_DC_GPIO_Port, DISPLAY_DC_Pin, 1},
    {"INT_TOF", "PC13", INT_TOF_GPIO_Port, INT_TOF_Pin, 0},
};

static volatile uint32_t tof_interrupt_count;
static uint8_t power_connected;
static uint8_t pwm_ready;
static uint8_t pwm_requested = 1U;
static uint8_t safe_pose_active;

/* Called with interrupts masked: a lower-priority init/diagnostic command must
 * never re-enable PWM after disconnect unless servoTask has applied a safe pose.
 * Q6 inverts PA9; MCU low disables PCA outputs (MODE2 OUTNE=00 drives low). */
static void apply_pwm_enable(void)
{
    HAL_GPIO_WritePin(OE_GPIO_Port, OE_Pin,
        (power_connected || safe_pose_active) && pwm_ready && pwm_requested ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* A disconnected safe pose deliberately keeps the servo supply and PWM alive.
 * servoTask enables this only after writing a complete validated safe frame. */
static void apply_servo_power(void)
{
    GPIO_PinState level=(power_connected || safe_pose_active)?GPIO_PIN_SET:GPIO_PIN_RESET;
    if(level==GPIO_PIN_RESET) apply_pwm_enable();
    HAL_GPIO_WritePin(DIAG_EN1_GPIO_Port, DIAG_EN1_Pin, level);
    HAL_GPIO_WritePin(DIAG_EN2_GPIO_Port, DIAG_EN2_Pin, level);
    HAL_GPIO_WritePin(EN1_GPIO_Port, EN1_Pin, level);
    HAL_GPIO_WritePin(EN2_GPIO_Port, EN2_Pin, level);
    apply_pwm_enable();
}

void BoardIO_SetSafePoseActive(uint8_t active)
{
    uint32_t saved_primask=__get_PRIMASK();
    __disable_irq();
    safe_pose_active=active!=0U;
    if(active) pwm_requested=1U;
    apply_servo_power();
    __set_PRIMASK(saved_primask);
}

void BoardIO_SetPwmReady(uint8_t ready)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    pwm_ready = ready != 0U;
    apply_pwm_enable();
    __set_PRIMASK(saved_primask);
}

void BoardIO_PrepareSafePose(void)
{
    uint32_t saved_primask=__get_PRIMASK();
    __disable_irq();
    pwm_ready=0U;
    pwm_requested=1U; /* Safety overrides a diagnostic OE disable. */
    apply_pwm_enable();
    __set_PRIMASK(saved_primask);
}

void BoardIO_SetPowerConnected(uint8_t connected)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    if (connected) {
        if (!power_connected) pwm_requested = 1U;
        power_connected = 1U;
    } else {
        power_connected = 0U;
    }
    apply_servo_power();
    __set_PRIMASK(saved_primask);
}

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
    if (pin->port == OE_GPIO_Port && pin->pin == OE_Pin) {
        uint32_t saved_primask = __get_PRIMASK();
        __disable_irq();
        if (level && (!power_connected || !pwm_ready)) {
            __set_PRIMASK(saved_primask);
            return BOARD_IO_READ_ONLY;
        }
        pwm_requested = level != 0U;
        apply_pwm_enable();
        __set_PRIMASK(saved_primask);
        return BOARD_IO_OK;
    }
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
    static const struct { const char *name; const char *pin; } inputs[] = {
        {"ADC_BAT", "PA5"}, {"ADC1", "PA3"}, {"ADC2", "PB9"},
    };
    size_t i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        if (!strcmp(name, inputs[i].name) || !strcmp(name, inputs[i].pin)) break;
    }
    if (i == sizeof(inputs) / sizeof(inputs[0])) return BOARD_IO_UNKNOWN_PIN;
    *canonical = inputs[i].name;
    return ADC_DMA_Read(i, raw) ? BOARD_IO_OK : BOARD_IO_ADC_ERROR;
}

/* ISR: count edges only. Sensor work remains in its task. */
void BoardIO_TofInterrupt(void) { ++tof_interrupt_count; }
uint32_t BoardIO_TofInterruptCount(void) { return tof_interrupt_count; }
