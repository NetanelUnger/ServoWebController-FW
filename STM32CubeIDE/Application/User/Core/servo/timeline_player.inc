/* Included by servo_task.c. All execution is on the sole PCA writer task.
 * RTC ticks accumulate in uint64_t: dispatch/interpolation use one absolute clock,
 * not repeated sleeps or rounded FreeRTOS tick deltas. No allocation or storage
 * operations during playback. The flash lease prevents mutation and OTA. */
const char *servo_TimelinePlay(uint32_t id) {
#if DISABLE_I2C_PCA9685
    return "DISABLED";
#endif
    taskENTER_CRITICAL();
    if(timeline.active || timeline.loading || servos_controller.demo1_active) {
        taskEXIT_CRITICAL();return "BUSY";
    }
    if(!ble_connected || !servos_controller.pca9685_ready) {
        taskEXIT_CRITICAL();return "NOT_READY";
    }
    timeline.loading=1U;timeline.cancelled=0U;
    taskEXIT_CRITICAL();
    const uint8_t *p=NULL;uint32_t size=0;
    bool ok=ImageStore_AcquireTimeline(id,&p,&size);
    (void)size;
    taskENTER_CRITICAL();
    timeline.loading=0U;
    if(!ok || timeline.cancelled || !ble_connected || !servos_controller.pca9685_ready) {
        if(ok) ImageStore_ReleaseTimeline();
        taskEXIT_CRITICAL();return ok?"CANCELLED":"INVALID_FILE";
    }
    servo_StopLocked();
    timeline.leased=1U;timeline.events=p+32;timeline.id=id;
    timeline.count=timeline_u32(p+8);timeline.duration=timeline_u32(p+12);
    timeline.repeat=timeline_u32(p+16);timeline.next=0;
    timeline.elapsed=0;timeline.max_late=0;timeline.ticks=0;timeline.cycle_ms=0;
    timeline.last_tick=TIMER_IF_GetTimerValue();timeline.state="PLAYING";timeline.active=1U;
    taskEXIT_CRITICAL();servo_Wake();return NULL;
}
void servo_TimelineStatus(char *out,size_t capacity) {
    taskENTER_CRITICAL();
    const char *state=timeline.state;
    uint32_t id=timeline.id,elapsed=timeline.elapsed,next=timeline.next,count=timeline.count,late=timeline.max_late;
    taskEXIT_CRITICAL();
    (void)snprintf(out,capacity,"OK TIMELINE_STATUS %s %lu %lu %lu %lu %lu\n",state,
        (unsigned long)id,(unsigned long)elapsed,(unsigned long)next,(unsigned long)count,(unsigned long)late);
}
/* Advance each ramp to a logical plan time before applying the next event.
 * This preserves exact starting values even if a worker wake misses a deadline. */
static uint8_t timeline_InterpolateLocked(uint32_t at) {
    uint8_t changed=0;
    for(uint8_t ch=0;ch<SERVO_CHANNEL_COUNT;ch++) {
        volatile servo_state_t *s=&servos_controller.servo[ch];
        if(!s->move.active) continue;
        uint32_t elapsed=at-timeline.starts[ch];
        if(elapsed>s->move.duration_ms) elapsed=s->move.duration_ms;
        uint16_t value=servo_ComputeTimedMoveValue(s,elapsed-s->move.elapsed_ms);
        if(value!=s->current_value) {s->current_value=value;dirty_channels|=(uint16_t)(1U<<ch);changed=1;}
    }
    return changed;
}
static void timeline_FinishLocked(const char *state) {
    timeline_StopLocked(state);
    for(uint8_t ch=0;ch<SERVO_CHANNEL_COUNT;ch++) servo_StopTimedMove(&servos_controller.servo[ch]);
}
static uint8_t timeline_AdvanceLocked(void) {
    if(!timeline.active || timeline.finishing) return 0;
    uint32_t now=TIMER_IF_GetTimerValue();
    timeline.ticks+=(uint32_t)(now-timeline.last_tick);timeline.last_tick=now;
    uint64_t total=timeline.ticks*1000U/TIMER_IF_Convert_ms2Tick(1000U);
    uint64_t relative=total-timeline.cycle_ms;
    /* Never burst through arbitrarily many missed repeat cycles. Freeze safely. */
    if(timeline.repeat && relative>=(uint64_t)timeline.repeat*2U) {
        timeline_FinishLocked("FAULT");return 0;
    }
    uint8_t changed=0;unsigned budget=64;
    for(;;) {
        uint32_t boundary=timeline.repeat?timeline.repeat:timeline.duration;
        uint32_t at=relative>boundary?boundary:(uint32_t)relative;
        while(timeline.next<timeline.count) {
            const uint8_t *e=timeline.events+16U*timeline.next;
            uint32_t dispatch=timeline_u32(e);
            if(dispatch>at) break;
            /* Reserve the entire timestamp group before touching desired state.
             * Never emit a partial simultaneous update on budget exhaustion. */
            uint32_t group=1;
            while(timeline.next+group<timeline.count &&
                  timeline_u32(timeline.events+16U*(timeline.next+group))==dispatch) group++;
            if(group>budget) {timeline_FinishLocked("FAULT");return changed;}
            budget-=group;
            changed|=timeline_InterpolateLocked(dispatch);
            uint32_t late=relative>UINT32_MAX?UINT32_MAX:(uint32_t)relative-dispatch;
            if(late>timeline.max_late) timeline.max_late=late;
            while(group--) {
                e=timeline.events+16U*timeline.next;
                uint8_t ch=e[10];volatile servo_state_t *s=&servos_controller.servo[ch];
                if(e[11]==0) {
                    s->current_value=timeline_u16(e+8);servo_StopTimedMove(s);
                    dirty_channels|=(uint16_t)(1U<<ch);changed=1;
                } else {
                    s->move.active=1;s->move.start_value=s->current_value;s->move.target_value=timeline_u16(e+8);
                    s->move.duration_ms=timeline_u32(e+4);s->move.elapsed_ms=0;timeline.starts[ch]=dispatch;
                }
                timeline.next++;
            }
        }
        changed|=timeline_InterpolateLocked(at);timeline.elapsed=at;
        if(relative<boundary) break;
        if(!timeline.repeat) {
            /* Hold ownership until the final frame succeeds (or fails). */
            timeline.finishing=1U;
            for(uint8_t ch=0;ch<SERVO_CHANNEL_COUNT;ch++) servo_StopTimedMove(&servos_controller.servo[ch]);
            break;
        }
        /* Clip unfinished ramps, hold boundary values, then apply time-zero events.
         * Absolute cycle origin advances by the intended duration, never wake time. */
        for(uint8_t ch=0;ch<SERVO_CHANNEL_COUNT;ch++) servo_StopTimedMove(&servos_controller.servo[ch]);
        timeline.cycle_ms+=boundary;relative-=boundary;timeline.next=0;
    }
    return changed;
}
