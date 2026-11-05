/**
 * @file    pca9685.c
 * @brief   PCA9685 PWM and servo driver implementation.
 */

#include "pca9685.h"
#include "config.h"
#include "main.h"
#include "../board_io.h"
#include "../i2c_os/i2c_os.h"

#define PCA9685_I2C_DEFAULT_ADDR_8BIT ((uint16_t)(PCA9685_I2C_7BIT_ADDR << 1))
#define PCA9685_I2C_SCAN_MIN_7BIT 0x40U
#define PCA9685_I2C_SCAN_MAX_7BIT 0x77U

#define PCA9685_REG_MODE1 0x00U
#define PCA9685_REG_MODE2 0x01U
#define PCA9685_REG_LED0_ON_L 0x06U
#define PCA9685_REG_PRESCALE 0xFEU

#define PCA9685_MODE1_ALLCALL 0x01U
#define PCA9685_MODE1_SLEEP 0x10U
#define PCA9685_MODE1_AI 0x20U
#define PCA9685_MODE1_RESTART 0x80U

#define PCA9685_MODE2_OUTDRV 0x04U
#define PCA9685_MODE2_OCH 0x08U

#define PCA9685_OSC_HZ 25000000UL
#define PCA9685_PWM_STEPS 4096UL

#define PCA9685_I2C_HANDLE hi2c3

/**
 * @brief Write one PCA9685 register.
 * @param reg Register address.
 * @param value Register value.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR` or `HAL_TIMEOUT`.
 */
static HAL_StatusTypeDef pca9685_writeReg(uint8_t reg, uint8_t value);

/**
 * @brief Read one PCA9685 register.
 * @param reg Register address.
 * @param value Output byte location.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR` or `HAL_TIMEOUT`.
 */
static HAL_StatusTypeDef pca9685_readReg(uint8_t reg, uint8_t *value);

/**
 * @brief Read a consecutive block of PCA9685 registers.
 * @param reg First register address to read.
 * @param value Output buffer.
 * @param len Number of bytes to read.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR` or `HAL_TIMEOUT`.
 */
static HAL_StatusTypeDef pca9685_readRegs(uint8_t reg, uint8_t *value, uint16_t len);

/**
 * @brief Program the PCA9685 PWM base frequency.
 * @param freq_hz Requested PWM frequency in Hertz.
 * @return `HAL_OK` on success, otherwise `HAL_ERROR`.
 */
static HAL_StatusTypeDef pca9685_setPwmFrequencyHz(uint16_t freq_hz);

/**
 * @brief Check whether an I2C address responds on the shared bus.
 * @param addr_8bit STM32 HAL-style 8-bit I2C address.
 * @return `HAL_OK` if the device acknowledges, otherwise `HAL_ERROR`.
 */
static HAL_StatusTypeDef pca9685_isAddressReady(uint16_t addr_8bit);

/**
 * @brief Probe an I2C address and verify it behaves like a PCA9685.
 * @param addr_8bit STM32 HAL-style 8-bit I2C address.
 * @return `HAL_OK` when the address is reachable and readable.
 */
static HAL_StatusTypeDef pca9685_probeAddress(uint16_t addr_8bit);

/**
 * @brief Discover the active PCA9685 address on the bus.
 * @return `HAL_OK` when a usable device address is found.
 */
static HAL_StatusTypeDef pca9685_findAddress(void);

/** @brief Currently selected STM32 HAL-style 8-bit I2C address for the PCA9685. */
static uint16_t pca9685_i2c_addr_8bit = PCA9685_I2C_DEFAULT_ADDR_8BIT;

static HAL_StatusTypeDef pca9685_writeReg(uint8_t reg, uint8_t value)
{
    return i2c_writeOS(&PCA9685_I2C_HANDLE, pca9685_i2c_addr_8bit, reg, I2C_MEMADD_SIZE_8BIT, &value, 1U);
}

static HAL_StatusTypeDef pca9685_readReg(uint8_t reg, uint8_t *value)
{
    if (value == NULL)
    {
        return HAL_ERROR;
    }

    return i2c_readOS(&PCA9685_I2C_HANDLE, pca9685_i2c_addr_8bit, reg, I2C_MEMADD_SIZE_8BIT, value, 1U);
}

static HAL_StatusTypeDef pca9685_readRegs(uint8_t reg, uint8_t *value, uint16_t len)
{
    if ((value == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    return i2c_readOS(&PCA9685_I2C_HANDLE, pca9685_i2c_addr_8bit, reg, I2C_MEMADD_SIZE_8BIT, value, len);
}

static HAL_StatusTypeDef pca9685_isAddressReady(uint16_t addr_8bit)
{
    return i2c_isDeviceReadyOS(&PCA9685_I2C_HANDLE, addr_8bit, 3U, 5U);
}

static HAL_StatusTypeDef pca9685_probeAddress(uint16_t addr_8bit)
{
    uint8_t mode1;

    if (pca9685_isAddressReady(addr_8bit) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (i2c_readOS(&PCA9685_I2C_HANDLE, addr_8bit, PCA9685_REG_MODE1, I2C_MEMADD_SIZE_8BIT, &mode1, 1U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef pca9685_findAddress(void)
{
    /* Hardware address straps, not the All Call alias or a responding sensor. */
    pca9685_i2c_addr_8bit = PCA9685_I2C_DEFAULT_ADDR_8BIT;
    return pca9685_probeAddress(pca9685_i2c_addr_8bit);
}

static HAL_StatusTypeDef pca9685_setPwmFrequencyHz(uint16_t freq_hz)
{
    uint32_t prescale_calc;
    uint8_t prescale;
    uint8_t prescale_readback;
    uint8_t oldmode;
    uint8_t sleep_mode;
    uint8_t restart_mode;
    uint8_t mode1_readback;

    if (freq_hz == 0U)
    {
        return HAL_ERROR;
    }

    prescale_calc = (PCA9685_OSC_HZ + ((uint32_t)freq_hz * (PCA9685_PWM_STEPS / 2UL)))
                    / ((uint32_t)freq_hz * PCA9685_PWM_STEPS);
    if (prescale_calc == 0U)
    {
        return HAL_ERROR;
    }
    prescale_calc -= 1UL;
    if (prescale_calc > 255UL)
    {
        return HAL_ERROR;
    }
    prescale = (uint8_t)prescale_calc;

    if (pca9685_readReg(PCA9685_REG_MODE1, &oldmode) != HAL_OK)
    {
        return HAL_ERROR;
    }

    sleep_mode = (oldmode & (uint8_t)(~PCA9685_MODE1_RESTART)) | PCA9685_MODE1_SLEEP;
    if (pca9685_writeReg(PCA9685_REG_MODE1, sleep_mode) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_writeReg(PCA9685_REG_PRESCALE, prescale) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_readReg(PCA9685_REG_PRESCALE, &prescale_readback) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (prescale_readback != prescale)
    {
        return HAL_ERROR;
    }

    if (pca9685_writeReg(PCA9685_REG_MODE1, oldmode) != HAL_OK)
    {
        return HAL_ERROR;
    }

    osDelay(1U);

    restart_mode = oldmode | PCA9685_MODE1_AI | PCA9685_MODE1_RESTART;
    if (pca9685_writeReg(PCA9685_REG_MODE1, restart_mode) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_readReg(PCA9685_REG_MODE1, &mode1_readback) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (((mode1_readback & PCA9685_MODE1_AI) == 0U) ||
        ((mode1_readback & PCA9685_MODE1_SLEEP) != 0U))
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef pca9685_Init(void)
{
    /* Q6 inverts PA9. Init/retry cannot bypass the BLE-controlled PWM gate. */
    BoardIO_SetPwmReady(0U);
#if DISABLE_I2C_PCA9685
    return HAL_ERROR; /* No probing, register access or PWM output. */
#endif
    uint8_t channel;
    uint8_t mode1_readback;
    uint8_t mode2_readback;
    uint8_t led0_regs[4];

    /* Find the active PCA9685 I2C address before configuring the chip. */
    if (pca9685_findAddress() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_writeReg(PCA9685_REG_MODE2, PCA9685_MODE2_OUTDRV) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_writeReg(PCA9685_REG_MODE1, PCA9685_MODE1_ALLCALL | PCA9685_MODE1_AI) != HAL_OK)
    {
        return HAL_ERROR;
    }

    osDelay(1U);

    if (pca9685_setPwmFrequencyHz(PCA9685_SERVO_FREQ_HZ) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_readReg(PCA9685_REG_MODE2, &mode2_readback) != HAL_OK)
    {
        return HAL_ERROR;
    }
    /* OCH=0 latches all PWM bytes together at the transaction STOP. */
    if ((mode2_readback & (PCA9685_MODE2_OUTDRV | PCA9685_MODE2_OCH)) != PCA9685_MODE2_OUTDRV)
    {
        return HAL_ERROR;
    }

    if (pca9685_readReg(PCA9685_REG_MODE1, &mode1_readback) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if ((mode1_readback & (PCA9685_MODE1_ALLCALL | PCA9685_MODE1_AI)) !=
        (PCA9685_MODE1_ALLCALL | PCA9685_MODE1_AI))
    {
        return HAL_ERROR;
    }

    /* Functional check: write PWM channel registers and verify readback. */
    if (pca9685_SetPwm(0U, 0U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (pca9685_readRegs(PCA9685_REG_LED0_ON_L, led0_regs, (uint16_t)sizeof(led0_regs)) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if ((led0_regs[0] != 0U) || (led0_regs[1] != 0U) || (led0_regs[2] != 0U) || (led0_regs[3] != 0U))
    {
        return HAL_ERROR;
    }

    for (channel = 0U; channel < PCA9685_CHANNEL_COUNT; channel++)
    {
        if (pca9685_SetPwm(channel, 0U, 0U) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    /* Keep OE disabled through init/recovery. The servo owner enables it only
     * after the complete desired frame has been written successfully. */
    return HAL_OK;
}

HAL_StatusTypeDef pca9685_SetPwm(uint8_t channel, uint16_t on_tick, uint16_t off_tick)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR; /* No probing, register access or PWM output. */
#endif
    uint8_t payload[4];
    uint16_t reg;

    if ((channel >= PCA9685_CHANNEL_COUNT) || (on_tick > 4095U) || (off_tick > 4095U))
    {
        return HAL_ERROR;
    }

    reg = (uint16_t)(PCA9685_REG_LED0_ON_L + ((uint16_t)channel * 4U));

    payload[0] = LO_UINT16(on_tick);
    payload[1] = HI_UINT16(on_tick);
    payload[2] = LO_UINT16(off_tick);
    payload[3] = HI_UINT16(off_tick);

    return i2c_writeOS(&PCA9685_I2C_HANDLE, pca9685_i2c_addr_8bit, reg, I2C_MEMADD_SIZE_8BIT, payload, (uint16_t)sizeof(payload));
}

HAL_StatusTypeDef pca9685_SetDutyCycleX100(uint8_t channel, uint16_t duty_x100, uint16_t *off_tick)
{
    uint16_t off;

    if (duty_x100 > 10000U)
    {
        return HAL_ERROR;
    }

    off = (uint16_t)((((uint32_t)duty_x100 * 4095UL) + 5000UL) / 10000UL);

    if (pca9685_SetPwm(channel, 0U, off) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (off_tick != NULL)
    {
        *off_tick = off;
    }

    return HAL_OK;
}

HAL_StatusTypeDef pca9685_ReadPwm(uint8_t channel, uint16_t *on, uint16_t *off)
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    uint8_t data[4];
    if (channel >= PCA9685_CHANNEL_COUNT || !on || !off) return HAL_ERROR;
    if (pca9685_readRegs(PCA9685_REG_LED0_ON_L + 4U * channel, data, 4) != HAL_OK) return HAL_ERROR;
    *on = data[0] | ((uint16_t)data[1] << 8);
    *off = data[2] | ((uint16_t)data[3] << 8);
    return HAL_OK;
}

static uint16_t pca9685_PulseToTicks(uint16_t pulse_us)
{
    uint16_t pulse_limited;
    uint32_t off_calc;

    pulse_limited = pulse_us;
    if (pulse_limited < PCA9685_SERVO_MIN_PULSE_US)
    {
        pulse_limited = PCA9685_SERVO_MIN_PULSE_US;
    }
    else if (pulse_limited > PCA9685_SERVO_MAX_PULSE_US)
    {
        pulse_limited = PCA9685_SERVO_MAX_PULSE_US;
    }

    off_calc = (((uint32_t)pulse_limited * (uint32_t)PCA9685_SERVO_FREQ_HZ * PCA9685_PWM_STEPS) + 500000UL) / 1000000UL;
    if (off_calc > 4095UL)
    {
        off_calc = 4095UL;
    }
    return (uint16_t)off_calc;
}

HAL_StatusTypeDef pca9685_SetServoPulseFrame(const uint16_t pulse_us[PCA9685_CHANNEL_COUNT])
{
#if DISABLE_I2C_PCA9685
    return HAL_ERROR;
#endif
    uint8_t payload[PCA9685_CHANNEL_COUNT * 4U];
    if (pulse_us == NULL) return HAL_ERROR;
    for (uint8_t channel = 0U; channel < PCA9685_CHANNEL_COUNT; channel++) {
        uint16_t off = pca9685_PulseToTicks(pulse_us[channel]);
        payload[4U * channel] = 0U;
        payload[4U * channel + 1U] = 0U;
        payload[4U * channel + 2U] = LO_UINT16(off);
        payload[4U * channel + 3U] = HI_UINT16(off);
    }
    /* MODE1.AI=1, MODE2.OCH=0: one mutex acquisition and one STOP for all
     * channels; sensors cannot interleave between individual servo updates. */
    return i2c_writeOS(&PCA9685_I2C_HANDLE, pca9685_i2c_addr_8bit,
                      PCA9685_REG_LED0_ON_L, I2C_MEMADD_SIZE_8BIT,
                      payload, (uint16_t)sizeof(payload));
}

HAL_StatusTypeDef pca9685_SetServoPulseUs(uint8_t channel, uint16_t pulse_us, uint16_t *off_tick)
{
    if (channel >= PCA9685_CHANNEL_COUNT) return HAL_ERROR;
    uint16_t off = pca9685_PulseToTicks(pulse_us);

    if (pca9685_SetPwm(channel, 0U, off) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (off_tick != NULL)
    {
        *off_tick = off;
    }

    return HAL_OK;
}
