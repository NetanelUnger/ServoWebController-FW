#ifndef ROBOT_SENSOR_MATH_H
#define ROBOT_SENSOR_MATH_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { int32_t minimum, maximum, mean, min_zone, max_zone, count; } RobotTofStats;
static inline int32_t RobotTof_Value(const uint16_t *map,bool ready,uint32_t zone,uint16_t low,uint16_t high) {
    if(!ready || zone>=64 || map[zone]==UINT16_MAX || map[zone]<low || map[zone]>high)return -1;
    return map[zone];
}
/* Exactly 64 cells; no allocation, I2C or unbounded work in a native callback. */
static inline RobotTofStats RobotTof_Reduce(const uint16_t *map,bool ready,uint16_t low,uint16_t high) {
    RobotTofStats s={-1,-1,-1,-1,-1,0};uint32_t sum=0;
    for(uint32_t i=0;i<64;i++) {
        int32_t value=RobotTof_Value(map,ready,i,low,high);
        if(value<0)continue;
        if(s.minimum<0 || value<s.minimum) {s.minimum=value;s.min_zone=(int32_t)i;}
        if(value>s.maximum) {s.maximum=value;s.max_zone=(int32_t)i;}
        ++s.count;sum+=(uint32_t)value;
    }
    if(s.count)s.mean=(int32_t)(sum/(uint32_t)s.count);
    return s;
}
/* IMU_Runtime.motion is independent of the versioned BLE packet layout.
 * The producer fixes +/-4g (0.122 mg/LSB) and +/-2000 dps (70 mdps/LSB). */
static inline int32_t RobotImu_Axis(const uint8_t *motion,bool ready,uint32_t axis,bool gyro) {
    if(!ready || axis>=3)return INT32_MIN;
    unsigned at=(gyro?6U:0U)+axis*2U;
    int32_t raw=(int16_t)((uint16_t)motion[at] | (uint16_t)motion[at+1]<<8);
    return gyro?raw*70:raw*122/1000;
}
#endif
