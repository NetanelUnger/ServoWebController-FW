#include "robot.h"
int32_t init(void) {volatile uint32_t n=1;while(n)n++;return 0;}
int32_t step(uint32_t now) {(void)now;return -100;}
