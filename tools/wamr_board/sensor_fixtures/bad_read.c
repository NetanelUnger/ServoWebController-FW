#include "robot.h"
int32_t step(uint32_t now) {(void)now;return tof_read((int32_t*)65532);}
