#include "robot.h"
static int32_t calls;
int32_t init(void) {++calls;return 0;}
int32_t step(uint32_t now) {(void)now;return calls==1 && !tof_ready() && !imu_ready()?1:-100;}
