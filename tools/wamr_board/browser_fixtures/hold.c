#include "robot.h"
/* Re-submit the current target; never choose a different motor position. */
int32_t step(uint32_t now_ms) {
    (void)now_ms;
    int32_t target=servo_get_us(0);
    return target<0 ? -1 : servo_set_us(0,(uint32_t)target);
}
