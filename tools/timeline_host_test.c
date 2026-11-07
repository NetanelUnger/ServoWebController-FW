/* Actual servo task/player under fake clock and PCA. No real device calls. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "servo/servo_task.c"
#include "timeline_id_parser.inc"
static uint32_t clock_ms,fail_frame,frames;
static uint16_t written[16];
static uint8_t blob[32+16*100+2];
static uint32_t blob_size;
static bool lease,cancel_acquire;
void *servoTaskHandle=(void *)1;
TickType_t xTaskGetTickCount(void) {return clock_ms;}
uint32_t TIMER_IF_GetTimerValue(void) {return clock_ms;}
uint32_t TIMER_IF_Convert_ms2Tick(uint32_t ms) {return ms;}
uint32_t osThreadFlagsSet(void *t,uint32_t f) {(void)t;return f;}
uint32_t osThreadFlagsWait(uint32_t f,uint32_t o,uint32_t ms) {(void)o;(void)ms;return f;}
void osDelay(uint32_t ms) {(void)ms;}
void BoardIO_SetPwmReady(uint8_t ready) {(void)ready;}
HAL_StatusTypeDef pca9685_Init(void) {return HAL_OK;}
HAL_StatusTypeDef pca9685_SetServoPulseFrame(const uint16_t *frame) {
    frames++;if(fail_frame)return HAL_ERROR;memcpy(written,frame,sizeof(written));return HAL_OK;
}
bool ImageStore_AcquireTimeline(uint32_t id,const uint8_t **p,uint32_t *size) {
    if(id!=7 || lease || !timeline_validate(blob,blob_size))return false;
    lease=true;*p=blob;*size=blob_size;
    if(cancel_acquire)servo_SetBleConnected(0);
    return true;
}
void ImageStore_ReleaseTimeline(void) {lease=false;}
static void u32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void plan(uint32_t count,uint32_t duration,uint32_t repeat) {
    memset(blob,0,sizeof(blob));u32(blob,0x314c4d54);blob[4]=1;blob[6]=32;
    u32(blob+8,count);u32(blob+12,duration);u32(blob+16,repeat);u32(blob+20,2);
    blob_size=32+16*count+2;blob[blob_size-2]='{';blob[blob_size-1]='}';
}
static void event(unsigned i,uint32_t at,uint32_t ms,uint16_t pulse,uint8_t ch,uint8_t kind) {
    uint8_t *p=blob+32+16*i;u32(p,at);u32(p+4,ms);p[8]=(uint8_t)pulse;p[9]=(uint8_t)(pulse>>8);p[10]=ch;p[11]=kind;
}
static void reset(void) {
    servo_Stop();clock_ms=0;fail_frame=0;frames=0;cancel_acquire=false;
    servo_Init();servo_SetBleConnected(1);servo_Task();assert(servos_controller.pca9685_ready);
}
static void play(void) {assert(timeline_validate(blob,blob_size));assert(servo_TimelinePlay(7)==NULL);assert(lease);}
static void tick(uint32_t ms) {clock_ms=ms;servo_Task();}
int main(void) {
    uint32_t id;
    assert(comm_ParseTimelineId("4294967295",&id)&&id==UINT32_MAX);
    const char *bad[]={"0","-1","+1","0x10","4294967296","999999999999999999999","7 x","7,8",""};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!comm_ParseTimelineId(bad[i],&id));
    reset();plan(2,100,0);event(0,0,0,500,0,0);event(1,0,100,2500,0,1);play();
    assert(servo_SetServoImmediateValue(0,1200)==HAL_BUSY);
    assert(servo_StartTimedMove(0,1200,10)==HAL_BUSY);
    assert(servo_StartDemo1()==HAL_BUSY);
    tick(0);assert(written[0]==500);tick(25);assert(written[0]==1000);
    tick(100);assert(written[0]==2500 && !lease && !strcmp(timeline.state,"COMPLETE"));
    reset();plan(1,10,0);event(0,10,0,2200,1,0);play();fail_frame=1;tick(10);
    assert(!strcmp(timeline.state,"FAULT")&&!lease);
    reset();plan(1,100,50);event(0,0,100,2500,0,1);play();tick(0);tick(50);
    assert(written[0]==2000);tick(75);assert(written[0]==2125);servo_Stop();assert(!lease);
    reset();plan(0,0,1000);play();tick(1000);assert(timeline.active&&timeline.elapsed==0);
    servo_SetBleConnected(0);assert(!lease&&!timeline.active);
    reset();plan(0,0,0);cancel_acquire=true;assert(servo_TimelinePlay(7)!=NULL&&!lease&&!timeline.active);
    reset();plan(1,20,0);event(0,20,0,2300,0,0);clock_ms=UINT32_MAX-9U;play();tick(10);
    assert(written[0]==2300&&!lease); /* RTC wrap */
    reset();plan(1,UINT32_MAX,0);event(0,0,UINT32_MAX,2500,0,1);play();tick(0);tick(0x7fffffffU);
    assert(written[0]==2000);tick(UINT32_MAX);assert(written[0]==2500&&!lease);
    reset();plan(76,10,0);unsigned i=0;
    for(unsigned at=0;at<4;at++)for(unsigned ch=0;ch<15;ch++)event(i++,at,0,(uint16_t)(1000+at),(uint8_t)ch,0);
    for(unsigned ch=0;ch<16;ch++)event(i++,4,0,2000,(uint8_t)ch,0);
    play();tick(10);assert(!strcmp(timeline.state,"FAULT")&&timeline.next==60);
    for(unsigned ch=0;ch<15;ch++)assert(written[ch]==1003);
    assert(written[15]==1500); /* simultaneous group not partially applied */
    reset();plan(1,100,0);event(0,0,100,2000,0,1);assert(timeline_validate(blob,blob_size));
    blob[43]=2;assert(!timeline_validate(blob,blob_size));
    puts("PASS: parser, absolute ramp, repeat, wrap, cancellation, ownership, final failure, atomic budget");
    return 0;
}
