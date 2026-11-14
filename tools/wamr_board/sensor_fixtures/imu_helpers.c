#include "robot.h"
int32_t init(void) {return imu_enable(1);}
int32_t step(uint32_t now) {
    (void)now;
    if(tof_ready())return -100;
    if(!imu_ready())return 0;
    int32_t ax=(int16_t)(imu_word(4)&65535),gx=(int16_t)(imu_word(5)>>16);
    if(imu_accel_mg(IMU_X)!=ax*122/1000 || imu_gyro_mdps(IMU_X)!=gx*70)return -101;
    if(imu_accel_mg(3)!=ROBOT_INVALID || imu_gyro_mdps(3)!=ROBOT_INVALID)return -102;
    if(!imu_frame_id() || imu_age_ms()<0)return -103;
    return 1;
}
