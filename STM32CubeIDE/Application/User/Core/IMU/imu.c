#include "imu.h"
#include "lsm6dsv320x_reg.h"
#include "config.h"
#include "main.h"
#include "i2c_os/i2c_os.h"
#include <string.h>

volatile IMU_Runtime g_imu;
static uint32_t retry_at;
static uint32_t samples_after;
static uint8_t discard_first;
static int32_t imu_bus_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    (void)handle;
    return i2c_readOS(&hi2c3, 0x6bU << 1, reg, I2C_MEMADD_SIZE_8BIT, data, len) == HAL_OK ? 0 : -1;
}
static int32_t imu_bus_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    (void)handle;
    return i2c_writeOS(&hi2c3, 0x6bU << 1, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, len) == HAL_OK ? 0 : -1;
}
static void delay_ms(uint32_t ms) { osDelay(pdMS_TO_TICKS(ms)); }
static stmdev_ctx_t ctx = {.read_reg=imu_bus_read, .write_reg=imu_bus_write, .mdelay=delay_ms};
static void put16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t *p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }

/* Application axes: X = sensor X, Y = -sensor Y, Z = -sensor Z.
 * Apply once at the BLE encoding boundary; ST's driver/fusion stays native.
 * The two sign changes form a proper right-handed rotation, C = diag(1,-1,-1).
 */
static uint16_t negate_count(int16_t value)
{
    /* +32768 cannot fit in the wire's signed 16-bit field. Saturate, never wrap. */
    return value == INT16_MIN ? (uint16_t)INT16_MAX : (uint16_t)(-(int32_t)value);
}
static void put_board_vector(uint8_t *p, const int16_t v[3])
{
    put16(p, (uint16_t)v[0]);
    put16(p+2, negate_count(v[1]));
    put16(p+4, negate_count(v[2]));
}
static void put_board_quaternion(uint8_t *p, const uint16_t q[4])
{
    /* Change both rotation bases: R_board = C R_sensor C^-1.
     * Scalar-first q_board = (w,x,-y,-z); identity stays identity.
     * ST outputs IEEE binary16, so XOR only its sign bit (no integer negation).
     */
    put16(p, q[0]);
    put16(p+2, q[1]);
    put16(p+4, q[2] ^ 0x8000U);
    put16(p+6, q[3] ^ 0x8000U);
}

static uint32_t handled_request = 0xffffffffU;
static int configure(uint8_t mode)
{
    int32_t r = lsm6dsv320x_sflp_game_rotation_set(&ctx,0);
    r |= lsm6dsv320x_xl_setup(&ctx, mode ? LSM6DSV320X_ODR_AT_120Hz : LSM6DSV320X_ODR_OFF,
                             LSM6DSV320X_XL_HIGH_PERFORMANCE_MD);
    r |= lsm6dsv320x_gy_setup(&ctx, mode ? LSM6DSV320X_ODR_AT_120Hz : LSM6DSV320X_ODR_OFF,
                             LSM6DSV320X_GY_HIGH_PERFORMANCE_MD);
    r |= lsm6dsv320x_hg_xl_data_rate_set(&ctx, mode==1 ? LSM6DSV320X_HG_XL_ODR_AT_480Hz : LSM6DSV320X_HG_XL_ODR_OFF, mode==1);
    if (mode==2) {
        r |= lsm6dsv320x_sflp_data_rate_set(&ctx, LSM6DSV320X_SFLP_120Hz);
        r |= lsm6dsv320x_sflp_game_rotation_set(&ctx,1);
        r |= lsm6dsv320x_sflp_game_rotation_reset(&ctx,1);
    }
    return r;
}
static void publish(uint8_t *p)
{
    memcpy((void *)g_imu.packet,p,IMU_RAW_LENGTH);
    if (IMU_BLE_Publish(p)) ++g_imu.tx_drops;
}
static void status_event(uint8_t status, uint8_t requested, uint32_t id)
{
    uint8_t p[IMU_RAW_LENGTH]={0};
    p[0]=3; p[1]=status; p[2]=requested; p[3]=g_imu.running;
    put32(p+4,id); put32(p+8,HAL_GetTick());
    put32(p+40,g_imu.errors); put32(p+44,g_imu.tx_drops);
    IMU_BLE_Applied(g_imu.running);
    publish(p);
}

void IMU_Step(void)
{
#if DISABLE_I2C_IMU
    return;
#endif
    const uint8_t mode = IMU_BLE_Mode();
    const uint32_t request = IMU_BLE_RequestId();
    if (!mode && !g_imu.ready) {
        if (request != handled_request) { status_event(0,0,request); handled_request=request; }
        return;
    }
    if (!g_imu.ready) {
        if (!mode || (int32_t)(HAL_GetTick()-retry_at)<0) return;
        retry_at = HAL_GetTick()+1000U;
        uint8_t id=0;
        g_imu.address=0x6b;
        if (lsm6dsv320x_device_id_get(&ctx,&id) || id!=LSM6DSV320X_ID) goto failed;
        g_imu.whoami=id;
        if (lsm6dsv320x_sw_reset(&ctx) || lsm6dsv320x_block_data_update_set(&ctx,1) ||
            lsm6dsv320x_auto_increment_set(&ctx,1) ||
            lsm6dsv320x_xl_full_scale_set(&ctx,LSM6DSV320X_4g) ||
            lsm6dsv320x_gy_full_scale_set(&ctx,LSM6DSV320X_2000dps) ||
            lsm6dsv320x_hg_xl_full_scale_set(&ctx,LSM6DSV320X_320g) ||
            lsm6dsv320x_timestamp_set(&ctx,1)) goto failed;
        g_imu.ready=1;
    }
    if (mode != g_imu.running) {
        if (configure(mode)) goto failed;
        g_imu.running=mode;
        samples_after=HAL_GetTick()+(mode==2 ? 1000U : 250U);
        discard_first=1;
    }
    if (request != handled_request) {
        status_event(0,mode,request); handled_request=request;
    }
    if (!mode) return;
    if ((int32_t)(HAL_GetTick()-samples_after)<0) return;
    lsm6dsv320x_data_ready_t ready;
    if (lsm6dsv320x_flag_data_ready_get(&ctx,&ready)) goto failed;
    if (!ready.drdy_xl || !ready.drdy_gy || (mode==1 && !ready.drdy_hgxl)) return;
    int16_t acc[3],gyro[3],hg[3]={0},temp=0;
    uint32_t timestamp;
    if (lsm6dsv320x_acceleration_raw_get(&ctx,acc) ||
        lsm6dsv320x_angular_rate_raw_get(&ctx,gyro) ||
        (mode==1 && lsm6dsv320x_hg_acceleration_raw_get(&ctx,hg)) ||
        lsm6dsv320x_temperature_raw_get(&ctx,&temp) ||
        lsm6dsv320x_timestamp_raw_get(&ctx,&timestamp)) goto failed;
    /* BDU may have held the first turn-on sample while we waited. Drain it. */
    if (discard_first) { discard_first=0; return; }
    uint8_t p[IMU_RAW_LENGTH]={0};
    p[0]=1; p[1]=0x17 | (ready.drdy_temp ? 8 : 0); /* Fresh ACC/GY/HG, timestamp; optional fresh temperature. */
    p[2]=g_imu.whoami; p[3]=g_imu.address;
    put32(p+4,++g_imu.frames); put32(p+8,HAL_GetTick()); put32(p+12,timestamp);
    put_board_vector(p+16, acc);
    put_board_vector(p+22, gyro);
    put_board_vector(p+28, hg);
    put16(p+34,temp);
    p[36]=1; p[37]=4; p[38]=4; p[39]=6; /* ST FS enum: 4g, 2000dps, 320g; UI ODR 120Hz. */
    put32(p+40,g_imu.errors); put32(p+44,g_imu.tx_drops);
    if (mode==2) {
        uint16_t q[4]; int16_t gravity[3],bias[3];
        if (lsm6dsv320x_sflp_quaternion_raw_get(&ctx,q) ||
            lsm6dsv320x_sflp_gravity_raw_get(&ctx,gravity) ||
            lsm6dsv320x_sflp_gbias_raw_get(&ctx,bias)) goto failed;
        p[0]=2; p[1]=7; /* Quaternion, gravity, bias valid after settling. */
        put_board_quaternion(p+16, q);
        put_board_vector(p+24, gravity);
        put_board_vector(p+30, bias);
        p[36]=3; p[37]=p[38]=p[39]=0; /* SFLP ODR enum: 120Hz. */
    }
    publish(p);
    return;
failed:
    ++g_imu.errors;
    (void)configure(0);
    /* Retry reset/configuration; never label failed reads as fresh data. */
    g_imu.ready=0; g_imu.running=0;
    status_event(2,mode,request);
    retry_at=HAL_GetTick()+1000U;
}
