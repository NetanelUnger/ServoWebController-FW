#include "robot.h"
static int32_t stage;
int32_t init(void) {tof_enable(1);return imu_enable(1);}
int32_t step(uint32_t now) {
    (void)now;
    if(!stage) {
        if(!tof_ready() || !imu_ready())return 0;
        if(tof_enable(0) || imu_enable(0))return -100;
        stage=1;
    }
    return !tof_ready() && !imu_ready() && tof_mm(0)==-1 &&
        imu_accel_mg(0)==ROBOT_INVALID && !tof_frame_id() && !imu_frame_id()?1:-101;
}
