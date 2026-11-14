#include "robot.h"
static int32_t map[TOF_ZONES];
int32_t init(void) {return tof_enable(1);}
int32_t step(uint32_t now) {
    (void)now;
    if(imu_ready())return -100;
    if(!tof_ready())return 0;
    if(tof_set_range(0,65534))return -101;
    int32_t count=tof_read(map),n=0,lo=-1,hi=-1,sum=0,li=-1,hi_i=-1;
    for(int32_t i=0;i<64;i++) {
        int32_t v=map[i];
        if(v!=tof_at(i%8,i/8))return -102;
        if(v<0)continue;
        if(lo<0 || v<lo) {lo=v;li=i;}
        if(v>hi) {hi=v;hi_i=i;}
        sum+=v;++n;
    }
    if(count!=n || tof_count()!=n || tof_min_mm()!=lo || tof_max_mm()!=hi ||
       tof_min_zone()!=li || tof_max_zone()!=hi_i || tof_mean_mm()!=(n?sum/n:-1))return -103;
    if(tof_at(8,0)!=-1 || tof_mm(64)!=-1 || tof_age_ms()<0 || !tof_frame_id())return -104;
    if(lo>=0) {
        if(tof_set_range(lo,lo) || tof_min_mm()!=lo || tof_max_mm()!=lo || tof_count()<1)return -105;
        if(tof_set_range(200,100)!=-1 || tof_min_mm()!=lo)return -106;
        int32_t accepted=tof_read(map),seen=0;
        for(int32_t i=0;i<64;i++)if(map[i]>=0) {if(map[i]!=lo)return -107;++seen;}
        if(accepted!=seen || accepted!=tof_count())return -108;
    }
    return 1;
}
