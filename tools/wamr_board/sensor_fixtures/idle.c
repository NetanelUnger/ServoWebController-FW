#include "robot.h"
int32_t step(uint32_t now) {
    (void)now;
    return !tof_ready() && !imu_ready() && tof_min_mm()==-1 && tof_count()==0?1:-100;
}
