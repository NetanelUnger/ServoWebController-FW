#ifndef SAFE_POSE_FORMAT_H
#define SAFE_POSE_FORMAT_H
#include <stdint.h>
#include <stdbool.h>

#define SAFE_POSE_FILE_TYPE 4U
#define SAFE_POSE_BYTES 40U
#define SAFE_POSE_CHANNELS 16U
/* SFP1, version 1, channel count 16, then 16 LE uint16 pulse widths. */
static inline bool safe_pose_decode(const uint8_t *p, uint32_t n, uint16_t *values)
{
    if (n != SAFE_POSE_BYTES || p[0]!='S' || p[1]!='F' || p[2]!='P' || p[3]!='1' ||
        p[4]!=1 || p[5]!=0 || p[6]!=SAFE_POSE_CHANNELS || p[7]!=0) return false;
    for (unsigned i=0; i<SAFE_POSE_CHANNELS; ++i) {
        uint16_t value=(uint16_t)p[8+2*i] | ((uint16_t)p[9+2*i]<<8);
        if (value<500 || value>2500) return false;
        if (values) values[i]=value;
    }
    return true;
}
#endif
