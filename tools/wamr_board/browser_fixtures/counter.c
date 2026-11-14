#include "robot.h"
static int32_t n;
int32_t step(uint32_t now_ms) { (void)now_ms; return ++n; }
