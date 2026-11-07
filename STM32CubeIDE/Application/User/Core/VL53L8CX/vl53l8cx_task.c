/**
 * @file    vl53l8cx_task.c
 * @brief   VL53L8CX lazy-init ranging task and BLE notification glue.
 */

#include "vl53l8cx_task.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include "config.h"
#include "log_module.h"
#include "i2c_os/i2c_os.h"
#include "tof_app.h"
#include "display/display.h"

#define VL53L8CX_I2C_HANDLE hi2c3

volatile vl53l8cx_runtime_t g_vl53l8cx_runtime;

static VL53L8CX_Object_t s_vl53l8cxObject;
static VL53L8CX_Result_t s_vl53l8cxResult;
static uint32_t s_vl53l8cxNextInitTickMs;

_Static_assert(CONFIG_VL53L8CX_DISTANCE_LIMIT_MIN_MM < CONFIG_VL53L8CX_DISTANCE_LIMIT_MAX_MM,
               "Invalid VL53L8CX distance limits");
_Static_assert(CONFIG_VL53L8CX_DISTANCE_MIN_MM < CONFIG_VL53L8CX_DISTANCE_MAX_MM,
               "Invalid default VL53L8CX distance range");
_Static_assert(CONFIG_VL53L8CX_DISTANCE_MIN_MM >= CONFIG_VL53L8CX_DISTANCE_LIMIT_MIN_MM,
               "Default VL53L8CX minimum distance is below the allowed range");
_Static_assert(CONFIG_VL53L8CX_DISTANCE_MAX_MM <= CONFIG_VL53L8CX_DISTANCE_LIMIT_MAX_MM,
               "Default VL53L8CX maximum distance is above the allowed range");

static void vl53l8cx_ResetLatestFrame(void);
static void vl53l8cx_HandleSensorFault(void);
static void vl53l8cx_StopRanging(void);
static uint8_t vl53l8cx_IsDistanceValid(const VL53L8CX_ZoneResult_t *pZoneResult);
static uint8_t vl53l8cx_EncodeDistance(uint32_t distance_mm,
                                       uint16_t min_distance_mm,
                                       uint16_t max_distance_mm);
static int32_t vl53l8cx_ConfigureSensor(void);
static int32_t vl53l8cx_StartRanging(void);
static int32_t vl53l8cx_BusInit(void);
static int32_t vl53l8cx_BusDeInit(void);
static int32_t vl53l8cx_GetTick(void);
static int32_t vl53l8cx_WriteReg(uint16_t Address, uint16_t Register, uint8_t *pData, uint16_t Size);
static int32_t vl53l8cx_ReadReg(uint16_t Address, uint16_t Register, uint8_t *pData, uint16_t Size);

void vl53l8cx_TaskInit(void)
{
    (void)memset((void *)&g_vl53l8cx_runtime, 0, sizeof(g_vl53l8cx_runtime));
    (void)memset(&s_vl53l8cxObject, 0, sizeof(s_vl53l8cxObject));
    (void)memset(&s_vl53l8cxResult, 0, sizeof(s_vl53l8cxResult));
    g_vl53l8cx_runtime.ble_distance_min_mm = CONFIG_VL53L8CX_DISTANCE_MIN_MM;
    g_vl53l8cx_runtime.ble_distance_max_mm = CONFIG_VL53L8CX_DISTANCE_MAX_MM;
    vl53l8cx_ResetLatestFrame();
    s_vl53l8cxNextInitTickMs = HAL_GetTick() + CONFIG_VL53L8CX_STARTUP_DELAY_MS;
}

void vl53l8cx_Task(void)
{
#if DISABLE_I2C_VL53L8CX
    osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
    return;
#endif
    int32_t status;

    if (TOF_APP_GetMode() == 0U ||
        (TOF_APP_GetMode() != 3U && TOF_APP_IsNotificationEnabled() == 0U))
    {
        if (g_vl53l8cx_runtime.ranging_active != 0U)
        {
            vl53l8cx_StopRanging();
        }

        osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
        return;
    }

    if (g_vl53l8cx_runtime.initialized == 0U)
    {
        if ((int32_t)(HAL_GetTick() - s_vl53l8cxNextInitTickMs) < 0)
        {
            osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
            return;
        }

        ++g_vl53l8cx_runtime.init_attempts;
        status = vl53l8cx_ConfigureSensor();
        g_vl53l8cx_runtime.last_status = status;
        if (status != VL53L8CX_OK)
        {
            LOG_INFO_APP("VL53L8CX init failed (%ld)\r\n", (long)status);
            s_vl53l8cxNextInitTickMs = HAL_GetTick() + CONFIG_VL53L8CX_INIT_RETRY_MS;
            osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
            return;
        }
    }

    if (g_vl53l8cx_runtime.ranging_active == 0U)
    {
        status = vl53l8cx_StartRanging();
        g_vl53l8cx_runtime.last_status = status;
        if (status != VL53L8CX_OK)
        {
            LOG_INFO_APP("VL53L8CX start failed (%ld)\r\n", (long)status);
            vl53l8cx_HandleSensorFault();
            osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
            return;
        }
    }

    status = VL53L8CX_GetDistance(&s_vl53l8cxObject, &s_vl53l8cxResult);
    if (status == VL53L8CX_OK)
    {
        uint8_t zone;
        uint8_t ble_payload[VL53L8CX_BLE_FRAME_SIZE];
        uint16_t distance_mm[VL53L8CX_ZONE_COUNT];
        uint16_t ble_distance_min_mm;
        uint16_t ble_distance_max_mm;

        vl53l8cx_GetDistanceEncodingRange(&ble_distance_min_mm, &ble_distance_max_mm);

        for (zone = 0U; zone < VL53L8CX_ZONE_COUNT; zone++)
        {
            /* Normalize once at acquisition: (row,col) <- (7-row,col).
             * XOR flips only the three row bits; no extra buffer or pass.
             * Both BLE encoding and local display consume this orientation. */
            const uint8_t source_zone = zone ^ 56U;
            ble_payload[zone] = VL53L8CX_BLE_DISTANCE_INVALID;
            distance_mm[zone] = VL53L8CX_DISTANCE_INVALID_MM;

            if ((source_zone < s_vl53l8cxResult.NumberOfZones) &&
                (vl53l8cx_IsDistanceValid(&s_vl53l8cxResult.ZoneResult[source_zone]) != 0U))
            {
                uint32_t zone_distance = s_vl53l8cxResult.ZoneResult[source_zone].Distance[0];

                if (zone_distance > (uint32_t)UINT16_MAX)
                {
                    zone_distance = (uint32_t)UINT16_MAX;
                }

                distance_mm[zone] = (uint16_t)zone_distance;
                ble_payload[zone] = vl53l8cx_EncodeDistance(zone_distance,
                                                            ble_distance_min_mm,
                                                            ble_distance_max_mm);
            }
        }

        taskENTER_CRITICAL();
        (void)memcpy((void *)g_vl53l8cx_runtime.ble_payload, ble_payload, sizeof(ble_payload));
        (void)memcpy((void *)g_vl53l8cx_runtime.distance_mm, distance_mm, sizeof(distance_mm));
        g_vl53l8cx_runtime.last_frame_tick_ms = HAL_GetTick();
        ++g_vl53l8cx_runtime.frames;
        taskEXIT_CRITICAL();

        (void)TOF_APP_NotifyDistanceMap(ble_payload, (uint8_t)sizeof(ble_payload));
        if (TOF_APP_GetMode() == 3U) (void)Display_DrawDepthMap(distance_mm);
    }
    else if (status != VL53L8CX_TIMEOUT)
    {
        g_vl53l8cx_runtime.last_status = status;
        ++g_vl53l8cx_runtime.faults;
        LOG_INFO_APP("VL53L8CX read failed (%ld)\r\n", (long)status);
        vl53l8cx_HandleSensorFault();
    }

    osDelay(pdMS_TO_TICKS(CONFIG_VL53L8CX_TASK_PERIOD_MS));
}

void vl53l8cx_GetDistanceEncodingRange(uint16_t *pMinMm, uint16_t *pMaxMm)
{
    uint16_t min_mm;
    uint16_t max_mm;

    taskENTER_CRITICAL();
    min_mm = g_vl53l8cx_runtime.ble_distance_min_mm;
    max_mm = g_vl53l8cx_runtime.ble_distance_max_mm;
    taskEXIT_CRITICAL();

    if (pMinMm != NULL)
    {
        *pMinMm = min_mm;
    }

    if (pMaxMm != NULL)
    {
        *pMaxMm = max_mm;
    }
}

uint8_t vl53l8cx_SetDistanceEncodingRange(uint16_t min_mm, uint16_t max_mm)
{
    if ((min_mm < CONFIG_VL53L8CX_DISTANCE_LIMIT_MIN_MM) ||
        (max_mm < CONFIG_VL53L8CX_DISTANCE_LIMIT_MIN_MM) ||
        (min_mm > CONFIG_VL53L8CX_DISTANCE_LIMIT_MAX_MM) ||
        (max_mm > CONFIG_VL53L8CX_DISTANCE_LIMIT_MAX_MM) ||
        (min_mm >= max_mm))
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    g_vl53l8cx_runtime.ble_distance_min_mm = min_mm;
    g_vl53l8cx_runtime.ble_distance_max_mm = max_mm;
    taskEXIT_CRITICAL();

    return 1U;
}

static void vl53l8cx_ResetLatestFrame(void)
{
    uint8_t zone;

    for (zone = 0U; zone < VL53L8CX_ZONE_COUNT; zone++)
    {
        g_vl53l8cx_runtime.ble_payload[zone] = VL53L8CX_BLE_DISTANCE_INVALID;
        g_vl53l8cx_runtime.distance_mm[zone] = VL53L8CX_DISTANCE_INVALID_MM;
    }

    g_vl53l8cx_runtime.last_frame_tick_ms = 0U;
}

static void vl53l8cx_HandleSensorFault(void)
{
    vl53l8cx_StopRanging();
    g_vl53l8cx_runtime.initialized = 0U;
    s_vl53l8cxNextInitTickMs = HAL_GetTick() + CONFIG_VL53L8CX_INIT_RETRY_MS;
}

static void vl53l8cx_StopRanging(void)
{
    if (g_vl53l8cx_runtime.ranging_active != 0U)
    {
        (void)VL53L8CX_Stop(&s_vl53l8cxObject);
        g_vl53l8cx_runtime.ranging_active = 0U;
    }
}

static uint8_t vl53l8cx_IsDistanceValid(const VL53L8CX_ZoneResult_t *pZoneResult)
{
    if ((pZoneResult == NULL) ||
        (pZoneResult->NumberOfTargets == 0U) ||
        (pZoneResult->Status[0] != 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t vl53l8cx_EncodeDistance(uint32_t distance_mm,
                                       uint16_t min_distance_mm,
                                       uint16_t max_distance_mm)
{
    uint32_t span_mm;
    uint32_t scaled;

    if (max_distance_mm <= min_distance_mm)
    {
        return 0U;
    }

    if (distance_mm <= (uint32_t)min_distance_mm)
    {
        return 0U;
    }

    if (distance_mm >= (uint32_t)max_distance_mm)
    {
        return VL53L8CX_BLE_DISTANCE_MAX_VALID;
    }

    span_mm = (uint32_t)max_distance_mm - (uint32_t)min_distance_mm;
    scaled = ((distance_mm - (uint32_t)min_distance_mm) * (uint32_t)VL53L8CX_BLE_DISTANCE_MAX_VALID
              + (span_mm / 2U)) / span_mm;
    if (scaled > (uint32_t)VL53L8CX_BLE_DISTANCE_MAX_VALID)
    {
        scaled = (uint32_t)VL53L8CX_BLE_DISTANCE_MAX_VALID;
    }

    return (uint8_t)scaled;
}

static int32_t vl53l8cx_ConfigureSensor(void)
{
    int32_t status;
    uint32_t sensor_id;
    VL53L8CX_IO_t io_ctx;
    VL53L8CX_ProfileConfig_t profile;

    (void)memset(&s_vl53l8cxObject, 0, sizeof(s_vl53l8cxObject));

    io_ctx.Init = vl53l8cx_BusInit;
    io_ctx.DeInit = vl53l8cx_BusDeInit;
    io_ctx.Address = VL53L8CX_DEFAULT_I2C_ADDRESS;
    io_ctx.WriteReg = vl53l8cx_WriteReg;
    io_ctx.ReadReg = vl53l8cx_ReadReg;
    io_ctx.GetTick = vl53l8cx_GetTick;

    status = VL53L8CX_RegisterBusIO(&s_vl53l8cxObject, &io_ctx);
    if (status != VL53L8CX_OK)
    {
        return status;
    }

    status = VL53L8CX_Init(&s_vl53l8cxObject);
    if (status != VL53L8CX_OK)
    {
        return status;
    }

    sensor_id = 0U;
    status = VL53L8CX_ReadID(&s_vl53l8cxObject, &sensor_id);
    if ((status != VL53L8CX_OK) || (sensor_id != VL53L8CX_ID))
    {
        return VL53L8CX_ERROR;
    }

    profile.RangingProfile = VL53L8CX_PROFILE_8x8_AUTONOMOUS;
    profile.TimingBudget = CONFIG_VL53L8CX_TIMING_BUDGET_MS;
    profile.Frequency = CONFIG_VL53L8CX_RANGING_FREQUENCY_HZ;
    profile.EnableAmbient = 1U;
    profile.EnableSignal = 1U;

    status = VL53L8CX_ConfigProfile(&s_vl53l8cxObject, &profile);
    if (status != VL53L8CX_OK)
    {
        return status;
    }

    g_vl53l8cx_runtime.initialized = 1U;
    LOG_INFO_APP("VL53L8CX ready id=0x%04lX, 8x8 at %lu Hz\r\n",
                 (unsigned long)sensor_id,
                 (unsigned long)profile.Frequency);

    return VL53L8CX_OK;
}

static int32_t vl53l8cx_StartRanging(void)
{
    int32_t status;

    status = VL53L8CX_Start(&s_vl53l8cxObject, VL53L8CX_MODE_ASYNC_CONTINUOUS);
    if (status == VL53L8CX_OK)
    {
        g_vl53l8cx_runtime.ranging_active = 1U;
    }

    return status;
}

static int32_t vl53l8cx_BusInit(void)
{
    /*
     * The STM32 HAL I2C peripheral is already initialized in main.c.
     * This callback is only meant to provide the BSP with a usable bus,
     * not to verify that the sensor is already answering on the wire.
     */
    return VL53L8CX_OK;
}

static int32_t vl53l8cx_BusDeInit(void)
{
    return VL53L8CX_OK;
}

static int32_t vl53l8cx_GetTick(void)
{
    return (int32_t)HAL_GetTick();
}

static int32_t vl53l8cx_WriteReg(uint16_t Address, uint16_t Register, uint8_t *pData, uint16_t Size)
{
#if DISABLE_I2C_VL53L8CX
    return -1; /* Keep the transport blocked even for a direct driver call. */
#endif
    HAL_StatusTypeDef status;

    status = i2c_writeOS(&VL53L8CX_I2C_HANDLE, Address, Register, I2C_MEMADD_SIZE_16BIT, pData, Size);
    if (status == HAL_OK)
    {
        return VL53L8CX_STATUS_OK;
    }

    return (status == HAL_TIMEOUT) ? VL53L8CX_STATUS_TIMEOUT_ERROR : VL53L8CX_STATUS_ERROR;
}

static int32_t vl53l8cx_ReadReg(uint16_t Address, uint16_t Register, uint8_t *pData, uint16_t Size)
{
#if DISABLE_I2C_VL53L8CX
    return -1; /* Keep the transport blocked even for a direct driver call. */
#endif
    HAL_StatusTypeDef status;

    status = i2c_readOS(&VL53L8CX_I2C_HANDLE, Address, Register, I2C_MEMADD_SIZE_16BIT, pData, Size);
    if (status == HAL_OK)
    {
        return VL53L8CX_STATUS_OK;
    }

    return (status == HAL_TIMEOUT) ? VL53L8CX_STATUS_TIMEOUT_ERROR : VL53L8CX_STATUS_ERROR;
}
