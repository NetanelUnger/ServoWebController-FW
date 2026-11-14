#include "robot.h"
/* Intentional instruction-budget trap; does not access motors. */
int32_t step(uint32_t now_ms) {
    volatile uint32_t n=now_ms;
    while(n!=0) n++;
    return (int32_t)n;
}
