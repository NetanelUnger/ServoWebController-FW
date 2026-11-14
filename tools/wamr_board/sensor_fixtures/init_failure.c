#include "robot.h"
int32_t init(void) {imu_enable(1);return -7;}
int32_t step(uint32_t now) {(void)now;return -100;}
