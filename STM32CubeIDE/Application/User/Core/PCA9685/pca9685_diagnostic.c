/* Board bring-up: address-only scan, then fixed hardware address 0x40.
 * No arbitrary detected address is treated as a PCA9685. Normal servo/TOF tasks
 * remain disabled. All bus access holds the shared I2C mutex. */
#include "pca9685_diagnostic.h"
#include "app_freertos.h"
#include "config.h"
#include "../board_io.h"
#include <string.h>

/* SWD-readable report, all fields are 32-bit words; no debugger function calls.
 * stage: 1=scanning, 2=no ACK, 3=register failure, 4=channel 0 neutral pulse active.
 * ack[address] = 1 if ACK; error[] captures HAL errors for every probe. */
volatile struct {
    uint32_t stage, scl, sda, mode1, mode2, prescale, verified, cycles, last_error;
    uint32_t ack[128], error[128];
} g_pca_diagnostic;
static uint8_t active;
static uint16_t diagnostic_address = 0x40U << 1;
/* Identification only: WHO_AM_I at 0x0f and TOF 16-bit model/revision regs.
 * No IMU sampling or TOF ranging is started. */
volatile struct {
    uint32_t rounds, imu6a_id, imu6b_id, tof_id, tof_revision;
    uint32_t imu6a_ok, imu6b_ok, tof_ok, failures;
} g_sensor_identification;
static void identify_sensors(void)
{
    uint8_t v;
    for (unsigned n = 0; n < 20; ++n) {
        v = 0;
        HAL_StatusTypeDef s = HAL_I2C_Mem_Read(&hi2c3, 0x6a << 1, 0x0f, I2C_MEMADD_SIZE_8BIT, &v, 1, 20);
        g_sensor_identification.imu6a_id = s == HAL_OK ? v : 0xffffffff;
        if (s == HAL_OK && v == 0x73) ++g_sensor_identification.imu6a_ok;
        v = 0;
        s = HAL_I2C_Mem_Read(&hi2c3, 0x6b << 1, 0x0f, I2C_MEMADD_SIZE_8BIT, &v, 1, 20);
        g_sensor_identification.imu6b_id = s == HAL_OK ? v : 0xffffffff;
        if (s == HAL_OK && v == 0x73) ++g_sensor_identification.imu6b_ok;
#if DISABLE_I2C_VL53L8CX
        /* Never access the TOF register bank behind an enabled ULD driver. */
        uint8_t id = 0, rev = 0;
        s = HAL_I2C_Mem_Read(&hi2c3, 0x29 << 1, 0, I2C_MEMADD_SIZE_16BIT, &id, 1, 20);
        HAL_StatusTypeDef r = HAL_I2C_Mem_Read(&hi2c3, 0x29 << 1, 1, I2C_MEMADD_SIZE_16BIT, &rev, 1, 20);
        g_sensor_identification.tof_id = s == HAL_OK ? id : 0xffffffff;
        g_sensor_identification.tof_revision = r == HAL_OK ? rev : 0xffffffff;
        if (s == HAL_OK && r == HAL_OK && id == 0xf0 && rev == 0x0c) ++g_sensor_identification.tof_ok;
        else ++g_sensor_identification.failures;
#endif
        ++g_sensor_identification.rounds;
        osDelay(10);
    }
}

static HAL_StatusTypeDef write_reg(uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef s = HAL_I2C_Mem_Write(&hi2c3, diagnostic_address, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 20);
    if (s != HAL_OK) g_pca_diagnostic.last_error = HAL_I2C_GetError(&hi2c3);
    return s;
}
static HAL_StatusTypeDef read_reg(uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef s = HAL_I2C_Mem_Read(&hi2c3, diagnostic_address, reg, I2C_MEMADD_SIZE_8BIT, value, 1, 20);
    if (s != HAL_OK) g_pca_diagnostic.last_error = HAL_I2C_GetError(&hi2c3);
    return s;
}
static int verified_write(uint8_t reg, uint8_t value)
{
    uint8_t check = 0;
    if (write_reg(reg, value) != HAL_OK || read_reg(reg, &check) != HAL_OK) return 0;
    /* MODE1 RESTART is hardware-managed; it may remain set while sleeping.
     * Compare only writable configuration bits, not that status/command bit. */
    uint8_t mask = reg == 0 ? 0x7f : 0xff;
    if ((check & mask) != (value & mask)) return 0;
    ++g_pca_diagnostic.verified;
    return 1;
}
void PCA9685_DiagnosticInit(void)
{
    if (!ENABLE_PCA9685_DIAGNOSTIC) return;
    BoardIO_SetPwmReady(0U);
    if (osMutexAcquire(i2cMutexHandle, osWaitForever) != osOK) return;
    g_pca_diagnostic.stage = 1;
    g_pca_diagnostic.scl = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6);
    g_pca_diagnostic.sda = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7);
    identify_sensors();
    /* Use a real register read at the strapped address. Zero-byte readiness
     * probes disagreed with successful sensor reads on this board; do not
     * gate initialization on that scan or select a different device address. */
    uint8_t value;
    if (read_reg(0, &value) != HAL_OK) {
        g_pca_diagnostic.error[0x40] = g_pca_diagnostic.last_error;
        g_pca_diagnostic.stage = 2;
        goto done;
    }
    g_pca_diagnostic.ack[0x40] = 1;
    g_pca_diagnostic.stage = 3;
    g_pca_diagnostic.mode1 = value;
    if (read_reg(1, &value) != HAL_OK) goto done;
    g_pca_diagnostic.mode2 = value;
    /* Sleep, disable group addressing, 50 Hz (shared servo-safe frequency).
     * Clear global overrides and every channel before enabling active-low OE. */
    if (!verified_write(0, 0x10) || !verified_write(1, 0x04) || !verified_write(0xfe, 121)) goto done;
    for (unsigned reg = 0xfa; reg <= 0xfd; ++reg) if (!verified_write(reg, 0)) goto done;
    for (unsigned ch = 0; ch < 16; ++ch) {
        for (unsigned byte = 0; byte < 4; ++byte)
            if (!verified_write(6 + 4*ch + byte, byte == 3 ? 0x10 : 0)) goto done;
    }
    if (!verified_write(0, 0x20)) goto done;
    osDelay(2);
    if (read_reg(0xfe, &value) != HAL_OK) goto done;
    g_pca_diagnostic.prescale = value;
    /* Q6 inverts MCU PA9: high at MCU enables active-low OE at PCA.
     * PWM_CH1 / LED0 only: nominal 1500 us at 50 Hz, no automatic sweep. */
    if (!verified_write(8, 0x33) || !verified_write(9, 0x01)) goto done;
    BoardIO_SetPwmReady(1U);
    active = 1;
    g_pca_diagnostic.stage = 4;
done:
    osMutexRelease(i2cMutexHandle);
}
void PCA9685_DiagnosticStep(void)
{
    if (!active) return;
    if (osMutexAcquire(i2cMutexHandle, osWaitForever) != osOK) return;
    uint8_t low, high;
    if (read_reg(8, &low) != HAL_OK || read_reg(9, &high) != HAL_OK || low != 0x33 || high != 1) {
        active = 0;
        g_pca_diagnostic.stage = 3;
        BoardIO_SetPwmReady(0U);
    }
    ++g_pca_diagnostic.cycles;
    osMutexRelease(i2cMutexHandle);
}
