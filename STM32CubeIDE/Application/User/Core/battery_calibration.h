#ifndef BATTERY_CALIBRATION_H
#define BATTERY_CALIBRATION_H
#include <stdint.h>
/* One-point gain calibration supplied for this board: RAW 3112 = 7830 mV.
 * Assumes zero offset. Integer-only, nearest-mV rounding, no extra RAM. */
#define BATTERY_CAL_RAW 3112U
#define BATTERY_CAL_MV 7830U
static inline uint16_t Battery_Millivolts(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * BATTERY_CAL_MV + BATTERY_CAL_RAW / 2U)
                      / BATTERY_CAL_RAW);
}
#endif
