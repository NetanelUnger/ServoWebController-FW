/* Controlled board qualification module; compile with build_guest.ps1.
 * Imports copy scalar values from one coherent snapshot captured before step.
 * No pointers, hardware registers, WASI, malloc or C runtime are exposed. */
#include "robot_api.h"

static __attribute__((noinline)) int32_t math_case(uint32_t n)
{
    float x = (float)n * 1.5f + 0.25f;
    double y = (double)n * 2.125 + 0.5;
    return (int32_t)(x * 100.0f) + (int32_t)(y * 100.0);
}

__attribute__((export_name("step")))
int32_t step(uint32_t mode)
{
    if (mode == 2) { for (;;) { __asm__ volatile ("" ::: "memory"); } }
    if (mode == 3) return *(volatile int32_t *)(uintptr_t)0xfffffff0U;
    if (mode == 4) {
        /* Exercise the real motor API without requesting a new target. */
        return servo_set_us(0, (uint32_t)servo_get_us(0));
    }
    if (mode == 5) return servo_set_us(16, 1500); /* Must reject. */
    if (mode == 6) return servo_set_us(0, 499);   /* Must reject, not clamp. */
    if (mode == 7) {
        int32_t sum = 0;
        for (uint32_t i = 0; i < 128; ++i) sum += math_case(10U + (i & 1U));
        return sum; /* 64 * (3700 + 4062) = 496768 */
    }
    if (mode == 1) {
        int32_t valid = 0;
        for (uint32_t i = 0; i < 64; ++i) if (tof_mm(i) >= 0) ++valid;
        /* Read the actual IMU sequence and timestamp as well. */
        return valid + (imu_word(1) != 0 ? 256 : 0) + (millis() != 0 ? 512 : 0);
    }
    /* Run-time input prevents constant folding of float and double tests. */
    return math_case(mode + 10);
}
