#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include "main.h"
#define DEVICE_ID_DIGITS 6U

/* Stable pseudorandom short identifier from the factory 96-bit UID.
 * No RNG startup, persistent Flash record or RAM cache is required.
 * This short display label is not a globally unique address or a secret. */
static inline void DeviceIdentity_Format(char id[DEVICE_ID_DIGITS + 1U])
{
    uint32_t hash = 2166136261U;
    hash = (hash ^ HAL_GetUIDw0()) * 16777619U;
    hash = (hash ^ HAL_GetUIDw1()) * 16777619U;
    hash = (hash ^ HAL_GetUIDw2()) * 16777619U;
    hash ^= hash >> 16;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13;
    uint32_t value = hash % 1000000U;
    id[DEVICE_ID_DIGITS] = '\0';
    for (unsigned i = DEVICE_ID_DIGITS; i != 0U; ) {
        id[--i] = '0' + value % 10U;
        value /= 10U;
    }
}
#endif
