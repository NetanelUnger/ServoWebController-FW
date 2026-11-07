/* Single owner of application notifications and indications. See BLE_TRANSPORT.md. */
#include "ble_tx.h"
#include "ble_core.h"
#include "svc_ctl.h"
#include "app_conf.h"
#include "app_freertos.h"
#include "log_module.h"
#include <string.h>
#include <stdio.h>

#define INVALID_HANDLE 0xffffU
#define WAKE 1U
#define TELEMETRY_MAX 64U
#define DEADLINE_MS 2000U
#define ATT_PROBE_MS 50U
typedef enum { IDLE, SUBMITTING, WAIT_POOL, WAIT_COMPLETE, DRAIN } TxState;
_Static_assert(CFG_BLE_NUM_LINK<=8,"telemetry pending bitmap supports up to 8 links");
typedef enum { ATT_NONE, ATT_RESERVED, ATT_READY, ATT_RETRY, ATT_SENDING, ATT_CLOSING } AttPhase;
typedef struct {
    uint32_t serial, deadline, next, wake;
    uint16_t attribute;
    uint8_t phase, write, status, error, length;
    uint8_t data[BLE_TX_PAYLOAD_MAX];
} AttResponse;
typedef struct {
    uint16_t handle, mtu;
    uint32_t session;
    uint32_t subscription[BLE_TX_STREAM_COUNT];
    uint8_t mode[BLE_TX_STREAM_COUNT];
    uint8_t closing;
    AttResponse control;
} Link;
typedef struct {
    uint16_t service, characteristic;
    uint8_t data[TELEMETRY_MAX], length, pending, next_link;
    uint32_t next;
} Stream;
static Link links[CFG_BLE_NUM_LINK];
static Stream streams[BLE_TX_STREAM_COUNT];
static uint8_t initialized, completed, indication, orphaned;
static uint8_t terminate_attempts, rx_reservations;
static uint32_t serial, deadline, pool_sequence, waiting_pool, submitted_at;
static uint32_t control_serial, control_wake;
extern osThreadId_t HciAsyncEvtTaskHandle;
static TxState state;
static BLE_TX_Item active;
static uint16_t offset, chunk;
static uint8_t round_robin, control_round_robin;
/* Monotonic counters are debugger-visible, and BLE_STATS reads them without halting. */
static struct { uint32_t accepted, completed, pool, timeout, errors, stale, coalesced,
    rx_rejected, tx_rejected, rx_high, tx_high, max_completion_ticks, control_retries, control_fatal; } stats;

static uint32_t ticks(uint32_t ms) { return (osKernelGetTickFreq()*ms+999U)/1000U; }
static void wake(void) { if (bleTxEventHandle) osEventFlagsSet(bleTxEventHandle, WAKE); }
static void init(void) {
    if (initialized) return;
    for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) links[i].handle=INVALID_HANDLE;
    initialized=1;
}
static Link *find(uint16_t handle) {
    if (handle==INVALID_HANDLE) return NULL;
    for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) if (links[i].handle==handle) return &links[i];
    return NULL;
}
static uint8_t current(const BLE_TX_Item *item) {
    if (!item || item->Stream>=BLE_TX_STREAM_COUNT) return 0;
    Link *l=find(item->Connection);
    return l && !l->closing && l->session==item->Session && item->Stream<BLE_TX_STREAM_COUNT &&
        l->subscription[item->Stream]==item->Subscription && l->mode[item->Stream];
}
uint8_t BLE_TX_Current(const BLE_TX_Item *item) {
    taskENTER_CRITICAL(); uint8_t ok=current(item); taskEXIT_CRITICAL(); return ok;
}
uint8_t BLE_TX_Capture(uint16_t handle, BLE_TX_Stream stream, BLE_TX_Item *item) {
    if (!item || (unsigned)stream>=BLE_TX_STREAM_COUNT) return 0;
    taskENTER_CRITICAL(); init(); Link *l=find(handle);
    if (l) { item->Connection=handle; item->Session=l->session;
        item->Subscription=l->subscription[stream]; item->Stream=stream; item->Reserved=0; }
    taskEXIT_CRITICAL(); return l!=NULL;
}
void BLE_TX_Register(BLE_TX_Stream stream,uint16_t service,uint16_t characteristic) {
    if ((unsigned)stream>=BLE_TX_STREAM_COUNT) return;
    taskENTER_CRITICAL(); init(); streams[stream].service=service;
    streams[stream].characteristic=characteristic; streams[stream].next=osKernelGetTickCount(); taskEXIT_CRITICAL();
}
void BLE_TX_Connected(uint16_t handle) {
    taskENTER_CRITICAL(); init();
    if (!find(handle)) for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) if (links[i].handle==INVALID_HANDLE) {
        memset(&links[i],0,sizeof(links[i])); links[i].handle=handle; links[i].session=++serial; links[i].mtu=23; break;
    }
    taskEXIT_CRITICAL(); wake();
}
void BLE_TX_Disconnected(uint16_t handle) {
    taskENTER_CRITICAL(); Link *l=find(handle);
    if (l) { unsigned index=(unsigned)(l-links); l->handle=INVALID_HANDLE; l->control.phase=ATT_NONE; l->closing=0;
        for (unsigned s=0;s<BLE_TX_STREAM_COUNT;s++) streams[s].pending &= ~(1U<<index); }
    if (active.Connection==handle && state==WAIT_POOL) { state=IDLE; active.Length=0; }
    if (active.Connection==handle && (state==SUBMITTING || state==WAIT_COMPLETE || state==DRAIN)) {
        orphaned=1; state=DRAIN;
    }
    taskEXIT_CRITICAL(); wake();
}
void BLE_TX_EventsDrained(void) {
    /* Called by the serialized BLE dispatcher, after disconnect and all already
     * queued events. A terminated ATT bearer cannot own further stack TX work. */
    taskENTER_CRITICAL();
    if (state==DRAIN && orphaned) { state=IDLE; active.Length=0; completed=0; orphaned=0; }
    taskEXIT_CRITICAL(); wake();
}
void BLE_TX_Event(void *event) {
    hci_event_pckt *p=(void *)((hci_uart_pckt *)event)->data;
    if (p->evt!=HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return;
    evt_blecore_aci *v=(void *)p->data;
    taskENTER_CRITICAL();
    if (v->ecode==ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE) {
        /* Connection_Handle is explicitly unused in ST's event contract. */
        ++pool_sequence; ++control_wake;
    } else if (v->ecode==ACI_GATT_NOTIFICATION_COMPLETE_VSEVT_CODE) {
        aci_gatt_notification_complete_event_rp0 *n=(void *)v->data;
        ++control_wake;
        if ((state==SUBMITTING || state==WAIT_COMPLETE || state==DRAIN) && !indication &&
            n->Attr_Handle==streams[active.Stream].characteristic+1U) completed=1;
        else ++stats.stale;
    } else if (v->ecode==ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE) {
        aci_gatt_server_confirmation_event_rp0 *n=(void *)v->data;
        ++control_wake;
        if ((state==SUBMITTING || state==WAIT_COMPLETE || state==DRAIN) && indication &&
            n->Connection_Handle==active.Connection) completed=1;
        else ++stats.stale;
    } else if (v->ecode==ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE) {
        aci_att_exchange_mtu_resp_event_rp0 *m=(void *)v->data;
        Link *l=find(m->Connection_Handle); if (l && m->Server_RX_MTU>=23) l->mtu=m->Server_RX_MTU;
    } else if (v->ecode==ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
        aci_gatt_attribute_modified_event_rp0 *a=(void *)v->data;
        Link *l=find(a->Connection_Handle);
        if (l && a->Attr_Data_Length>=2) for (unsigned s=0;s<BLE_TX_STREAM_COUNT;s++) {
            if (streams[s].characteristic && a->Attr_Handle==streams[s].characteristic+2U) {
                l->mode[s]=a->Attr_Data[0]&3U; ++l->subscription[s];
                streams[s].pending &= ~(1U<<(l-links));
            }
        }
    }
    taskEXIT_CRITICAL(); wake();
}
uint8_t BLE_TX_SessionCurrent(const BLE_TX_Item *item) {
    if (!item) return 0;
    taskENTER_CRITICAL(); Link *l=find(item->Connection);
    uint8_t ok=l && !l->closing && l->session==item->Session; taskEXIT_CRITICAL(); return ok;
}
static unsigned reservations(void) { return rx_reservations; }
uint8_t BLE_TX_ReserveRx(BLE_TX_Item *item) {
    if (!item) return 0;
    taskENTER_CRITICAL(); Link *l=find(item->Connection);
    uint8_t ok=l && !l->closing && l->session==item->Session && reservations()+osMessageQueueGetCount(commTxQueueHandle)<5U;
    if (ok) { ++rx_reservations; item->Reserved=1; }
    taskEXIT_CRITICAL(); return ok;
}
void BLE_TX_Release(const BLE_TX_Item *item) {
    if (!item || !item->Reserved) return;
    taskENTER_CRITICAL();
    if (rx_reservations) --rx_reservations;
    taskEXIT_CRITICAL();
}
static void finish(void) { BLE_TX_Release(&active); active.Reserved=0; active.Length=0; offset=0; }
uint8_t BLE_TX_ConfirmationIsOta(uint16_t connection) {
    return indication && active.Stream==BLE_TX_OTA && active.Connection==connection &&
        (state==SUBMITTING || state==WAIT_COMPLETE) && completed;
}
osStatus_t BLE_TX_Enqueue(const BLE_TX_Item *item,uint32_t wait) {
    (void)wait; /* Always nonblocking. RX admission has already reserved reply capacity. */
    if (!item || !item->Length || !BLE_TX_Current(item)) return osErrorResource;
    taskENTER_CRITICAL();
    osStatus_t result=osErrorResource;
    if (item->Reserved || reservations()+osMessageQueueGetCount(commTxQueueHandle)<5U) {
        BLE_TX_Item queued=*item; queued.Reserved=0;
        result=osMessageQueuePut(commTxQueueHandle,&queued,0,0);
        if (result==osOK && item->Reserved && rx_reservations) --rx_reservations;
    }
    if (result!=osOK) ++stats.tx_rejected;
    uint32_t depth=osMessageQueueGetCount(commTxQueueHandle);
    if (depth>stats.tx_high) stats.tx_high=depth;
    taskEXIT_CRITICAL(); wake(); return result;
}
tBleStatus BLE_TX_Ordered(BLE_TX_Stream stream,const uint8_t *data,uint16_t length) {
    if (!data || !length || length>BLE_TX_PAYLOAD_MAX || (unsigned)stream>=BLE_TX_STREAM_COUNT) return BLE_STATUS_INVALID_PARAMS;
    uint8_t failed=0;
    for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) {
        BLE_TX_Item item;
        taskENTER_CRITICAL(); uint16_t h=links[i].handle; uint8_t enabled=links[i].mode[stream]; taskEXIT_CRITICAL();
        if (h==INVALID_HANDLE || !enabled || !BLE_TX_Capture(h,stream,&item)) continue;
        item.Length=(uint8_t)length; memcpy(item.Payload,data,length);
        if (BLE_TX_Enqueue(&item,0)!=osOK) failed=1;
    }
    return failed ? BLE_STATUS_INSUFFICIENT_RESOURCES : BLE_STATUS_SUCCESS;
}
tBleStatus BLE_TX_Latest(BLE_TX_Stream stream,const uint8_t *data,uint16_t length) {
    if (!data || !length || length>TELEMETRY_MAX || stream<=BLE_TX_REPLY || stream>=BLE_TX_OTA)
        return BLE_STATUS_INVALID_PARAMS;
    taskENTER_CRITICAL(); Stream *s=&streams[stream];
    if (s->pending) ++stats.coalesced;
    memcpy(s->data,data,length); s->length=(uint8_t)length;
    s->pending=0;
    for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) if (links[i].handle!=INVALID_HANDLE && links[i].mode[stream]) s->pending|=1U<<i;
    taskEXIT_CRITICAL(); wake(); return BLE_STATUS_SUCCESS;
}
tBleStatus BLE_TX_Local(uint16_t service,uint16_t characteristic,const uint8_t *data,uint16_t length) {
    if ((!data && length) || length>243U) return BLE_STATUS_INVALID_PARAMS;
    tBleStatus result=aci_gatt_update_char_value_ext(0,service,characteristic,GATT_CHAR_UPDATE_LOCAL_ONLY,length,0,(uint8_t)length,data);
    BLE_TX_AttResultFor("local",characteristic+1U,result); return result;
}
static void control_fail(Link *l, uint32_t now) {
    if (!l->closing) { ++stats.control_fatal; ++stats.errors; }
    l->closing=1; l->control.phase=ATT_CLOSING; l->control.next=now;
}
uint8_t BLE_TX_AttBegin(uint16_t connection,uint16_t attribute,uint8_t write,
                       const uint8_t *data,uint16_t length,BLE_TX_AttTicket *ticket) {
    if (!ticket || write>1U || length>BLE_TX_PAYLOAD_MAX || (length && !data) || (!write && length)) return 0;
    taskENTER_CRITICAL(); Link *l=find(connection);
    if (!l || l->closing) { taskEXIT_CRITICAL(); return 0; }
    if (l->control.phase!=ATT_NONE && l->control.phase!=ATT_SENDING) {
        /* One outstanding request per unenhanced ATT bearer. Never overwrite a
         * saved decision or admit side effects on a colliding request. */
        control_fail(l,osKernelGetTickCount()); taskEXIT_CRITICAL();
        LOG_WARNING_APP("BLE ATT collision conn=0x%04X attr=0x%04X\n",connection,attribute);
        wake(); return 0;
    }
    /* A subsequent sequential ATT request can arrive while the prior ACI call
     * returns. ATT_SENDING bytes already live in the owner's stack copy; replacing
     * this slot cannot change them. The prior result is checked by request serial. */
    AttResponse *c=&l->control; memset(c,0,sizeof(*c));
    c->serial=++control_serial; c->phase=ATT_RESERVED; c->write=write;
    c->attribute=attribute; c->length=(uint8_t)length;
    c->deadline=osKernelGetTickCount()+ticks(DEADLINE_MS);
    if (length) memcpy(c->data,data,length);
    ticket->Connection=connection; ticket->Session=l->session; ticket->Serial=c->serial;
    taskEXIT_CRITICAL(); return 1;
}
void BLE_TX_AttCommit(const BLE_TX_AttTicket *ticket,uint8_t status,uint8_t error) {
    if (!ticket) return;
    taskENTER_CRITICAL(); Link *l=find(ticket->Connection);
    if (l && !l->closing && l->session==ticket->Session && l->control.serial==ticket->Serial &&
        l->control.phase==ATT_RESERVED) {
        l->control.status=status; l->control.error=error; l->control.phase=ATT_READY;
    }
    taskEXIT_CRITICAL(); wake();
}
/* Runs at most one controller command per owner step, outside the critical
 * section. The copied response outlives HCI callback storage and any disconnect.
 * A 0x64 permit has NOT been accepted. Unlike notification updates, ST does not
 * promise a pool-available event for permit commands, so these alone have a
 * bounded 50ms probe in addition to pool/completion wakes. */
static uint8_t run_control(uint32_t now,uint32_t *wait,uint8_t *pending) {
    AttResponse saved={0}; uint16_t connection=INVALID_HANDLE; uint32_t session=0;
    uint8_t closing=0, expired=0;
    *wait=osWaitForever; *pending=0;
    taskENTER_CRITICAL();
    for (unsigned n=0;n<CFG_BLE_NUM_LINK;n++) {
        unsigned i=(control_round_robin+n)%CFG_BLE_NUM_LINK;
        Link *l=&links[i]; AttResponse *c=&l->control;
        if (l->handle==INVALID_HANDLE || c->phase==ATT_NONE) continue;
        if (c->phase!=ATT_CLOSING) *pending=1;
        if (c->phase!=ATT_CLOSING && (int32_t)(c->deadline-now)<=0) {
            control_fail(l,now); expired=1;
        }
        int32_t remaining=0;
        if (c->phase==ATT_RESERVED) remaining=(int32_t)(c->deadline-now);
        else if (c->phase==ATT_CLOSING) remaining=(int32_t)(c->next-now);
        else if (c->phase==ATT_RETRY && c->wake==control_wake) {
            remaining=(int32_t)(c->next-now);
            int32_t expiry=(int32_t)(c->deadline-now);
            if (expiry<remaining) remaining=expiry;
        }
        if (remaining>0) { if ((uint32_t)remaining<*wait) *wait=(uint32_t)remaining; continue; }
        saved=*c; connection=l->handle; session=l->session;
        control_round_robin=(uint8_t)((i+1U)%CFG_BLE_NUM_LINK);
        closing=c->phase==ATT_CLOSING;
        if (closing) c->next=now+ticks(1000);
        else { if (c->phase==ATT_RETRY) ++stats.control_retries; c->phase=ATT_SENDING; c->wake=control_wake; }
        break;
    }
    taskEXIT_CRITICAL();
    if (connection==INVALID_HANDLE) return 0;
    if (expired) LOG_WARNING_APP("BLE ATT timeout conn=0x%04X attr=0x%04X write=%u\n",connection,saved.attribute,saved.write);
    tBleStatus result=closing ? aci_gap_terminate(connection,0x13) : saved.write ?
        aci_gatt_permit_write(connection,saved.attribute,saved.status,saved.error,saved.length,saved.data) :
        aci_gatt_permit_read(connection,saved.status,saved.error,saved.status ? saved.attribute : 0U);
    taskENTER_CRITICAL(); Link *l=find(connection);
    if (l && l->session==session) {
        AttResponse *c=&l->control;
        if (c->serial!=saved.serial) {
            /* The peer's next request proves the previous ATT transaction ended.
             * A contradictory API failure is uncertain: never replay old bytes. */
            if (result!=BLE_STATUS_SUCCESS) control_fail(l,now);
        }
        else if (closing) { if (result!=BLE_STATUS_SUCCESS) ++stats.errors; }
        else if (c->phase==ATT_SENDING) {
            if (result==BLE_STATUS_SUCCESS) c->phase=ATT_NONE;
            else if (result==BLE_STATUS_INSUFFICIENT_RESOURCES) { c->phase=ATT_RETRY; c->next=now+ticks(ATT_PROBE_MS); }
            else control_fail(l,now); /* Unknown/ambiguous statuses must not replay. */
        }
    }
    taskEXIT_CRITICAL();
    if (!closing && result!=BLE_STATUS_SUCCESS && (saved.phase!=ATT_RETRY || result!=BLE_STATUS_INSUFFICIENT_RESOURCES))
        LOG_WARNING_APP("BLE ATT deferred conn=0x%04X attr=0x%04X write=%u status=0x%02X\n",connection,saved.attribute,saved.write,result);
    return 1;
}
static uint32_t select_work(uint32_t now) {
    if (active.Length && current(&active)) return 0;
    finish();
    while (osMessageQueueGet(commTxQueueHandle,&active,NULL,0)==osOK) {
        if (current(&active)) return 0;
        ++stats.stale; finish();
    }
    uint32_t wait=osWaitForever;
    for (unsigned n=0;n<BLE_TX_OTA-1U;n++) {
        unsigned id=1U+(round_robin+n)%(BLE_TX_OTA-1U); Stream *s=&streams[id];
        if (!s->pending) continue;
        int32_t remaining=(int32_t)(s->next-now);
        if (remaining>0) { if ((uint32_t)remaining<wait) wait=remaining; continue; }
        for (unsigned k=0;k<CFG_BLE_NUM_LINK;k++) {
            unsigned i=(s->next_link+k)%CFG_BLE_NUM_LINK;
            if (!(s->pending&(1U<<i))) continue;
            if (s->length>links[i].mtu-3U) continue;
            s->pending &= ~(1U<<i);
            if (links[i].handle==INVALID_HANDLE || links[i].closing || !links[i].mode[id]) continue;
            active.Connection=links[i].handle; active.Session=links[i].session;
            active.Subscription=links[i].subscription[id]; active.Stream=(uint8_t)id; active.Reserved=0;
            active.Length=s->length; memcpy(active.Payload,s->data,s->length);
            s->next_link=(uint8_t)((i+1U)%CFG_BLE_NUM_LINK);
            round_robin=(uint8_t)(id%(BLE_TX_OTA-1U)); return 0;
        }
    }
    return wait;
}
uint32_t BLE_TX_Run(void) {
    uint32_t now=osKernelGetTickCount(), wait;
    uint16_t terminate=INVALID_HANDLE;
    uint32_t control_wait; uint8_t control_pending;
    taskENTER_CRITICAL();
    if ((state==WAIT_COMPLETE || state==DRAIN) && completed) {
        uint32_t elapsed=now-submitted_at;
        if (elapsed>stats.max_completion_ticks) stats.max_completion_ticks=elapsed;
        uint8_t was_draining=state==DRAIN;
        ++stats.completed; completed=0; state=IDLE;
        if (!was_draining && current(&active)) { offset+=chunk; while (offset<active.Length && active.Payload[offset]=='\n') ++offset;
            if (offset>=active.Length) finish(); }
        else finish();
    }
    taskEXIT_CRITICAL();
    /* Consume completions even under continuous permission traffic. */
    if (run_control(now,&control_wait,&control_pending)) return 0;
    taskENTER_CRITICAL();
    if (state==DRAIN) {
        if (orphaned) { taskEXIT_CRITICAL(); return control_wait; }
        if ((int32_t)(deadline-now)>0) { wait=deadline-now; taskEXIT_CRITICAL(); return wait<control_wait ? wait : control_wait; }
        /* Terminate may itself be busy. Retry only the disconnect, never the TX.
         * Keep an observable one-second wait even if it continues to fail. */
        terminate=active.Connection; ++terminate_attempts; deadline=now+ticks(1000);
        taskEXIT_CRITICAL();
        tBleStatus result=aci_gap_terminate(terminate,0x13);
        if (result!=BLE_STATUS_SUCCESS) { taskENTER_CRITICAL(); ++stats.errors; taskEXIT_CRITICAL(); }
        return ticks(1000);
    }
    if (state==WAIT_POOL && pool_sequence!=waiting_pool) {
        state=IDLE;
        /* No packet was accepted. Re-select so a new reply outranks telemetry,
         * and a stale telemetry snapshot is replaced by the latest frame. */
        if (active.Stream>BLE_TX_REPLY && active.Stream<BLE_TX_OTA) {
            Link *l=find(active.Connection);
            if (l && current(&active)) streams[active.Stream].pending|=1U<<(l-links);
            finish();
        }
    }
    if (state==WAIT_POOL || state==WAIT_COMPLETE) {
        int32_t remaining=(int32_t)(deadline-now);
        if (remaining>0) { taskEXIT_CRITICAL(); return (uint32_t)remaining<control_wait ? (uint32_t)remaining : control_wait; }
        ++stats.timeout; terminate=active.Connection;
        uint8_t timed_out_state=(uint8_t)state;
        uint8_t timed_out_stream=active.Stream;
        uint16_t timed_out_attribute=streams[active.Stream].characteristic+1U;
        uint32_t timed_out_pool=pool_sequence, timed_out_wait=waiting_pool;
        state=DRAIN;
        deadline=now+ticks(1000); terminate_attempts=1;
        taskEXIT_CRITICAL();
        LOG_WARNING_APP("BLE TX timeout conn=0x%04X state=%u stream=%u attr=0x%04X pool=%lu wait=%lu\n",
            terminate,timed_out_state,timed_out_stream,timed_out_attribute,
            (unsigned long)timed_out_pool,(unsigned long)timed_out_wait);
        /* Never retry an accepted packet or send a HELP END after a lost line. */
        tBleStatus result=aci_gap_terminate(terminate,0x13);
        if (result!=BLE_STATUS_SUCCESS) { taskENTER_CRITICAL(); ++stats.errors; taskEXIT_CRITICAL(); }
        return ticks(1000);
    }
    if (control_pending) { taskEXIT_CRITICAL(); return control_wait; }
    wait=select_work(now);
    if (wait) { taskEXIT_CRITICAL(); return wait<control_wait ? wait : control_wait; }
    if (!current(&active)) { finish(); taskEXIT_CRITICAL(); return 0; }
    Link *l=find(active.Connection); Stream *s=&streams[active.Stream];
    uint8_t mode=(l->mode[active.Stream]&1U) ? 1U : 2U;
    indication=mode==2U; completed=orphaned=0;
    chunk=active.Length-offset;
    if (active.Stream==BLE_TX_REPLY) {
        uint8_t *newline=memchr(active.Payload+offset,'\n',chunk);
        if (newline) chunk=(uint16_t)(newline-(active.Payload+offset));
    }
    if (!chunk) { ++offset; if (offset>=active.Length) finish(); taskEXIT_CRITICAL(); return 0; }
    if (chunk>243U || chunk>l->mtu-3U) {
        ++stats.errors;
        if (active.Stream==BLE_TX_REPLY) {
            const char error[]="- MTU TOO SMALL";
            memcpy(active.Payload,error,sizeof(error)-1U); active.Length=sizeof(error)-1U; offset=0; chunk=active.Length;
        } else { finish(); taskEXIT_CRITICAL(); return 0; }
    }
    uint16_t service=s->service, characteristic=s->characteristic, connection=active.Connection;
    state=SUBMITTING; waiting_pool=pool_sequence; submitted_at=now; deadline=now+ticks(DEADLINE_MS);
    taskEXIT_CRITICAL();
    tBleStatus result=aci_gatt_update_char_value_ext(connection,service,characteristic,mode,chunk,0,(uint8_t)chunk,active.Payload+offset);
    taskENTER_CRITICAL();
    if (result==BLE_STATUS_SUCCESS) {
        ++stats.accepted;
        if (active.Stream>BLE_TX_REPLY && active.Stream<BLE_TX_OTA)
            streams[active.Stream].next=now+ticks(active.Stream==BLE_TX_ADC ? 200U : active.Stream==BLE_TX_TOF ? 100U : 50U);
        if (state==SUBMITTING) state=WAIT_COMPLETE;
    }
    else if (result==BLE_STATUS_INSUFFICIENT_RESOURCES) { ++stats.pool; state=current(&active) ? WAIT_POOL : IDLE; completed=0; }
    else {
        ++stats.errors; completed=0;
        /* A command accepted by ATT must not fail silently or continue with a
         * misleading multiline END marker. Fail this link, never replay data. */
        if (current(&active) && (active.Stream==BLE_TX_REPLY || active.Stream==BLE_TX_OTA)) {
            state=DRAIN; deadline=now; orphaned=0;
        } else { state=IDLE; finish(); }
    }
    taskEXIT_CRITICAL(); return 0;
}
void BLE_TX_Task(void *argument) {
    (void)argument;
    for (;;) {
        uint32_t wait=BLE_TX_Run();
        if (wait) (void)osEventFlagsWait(bleTxEventHandle,WAKE,osFlagsWaitAny,wait);
    }
}
void BLE_TX_AttResult(tBleStatus result) {
    if (result!=BLE_STATUS_SUCCESS) { taskENTER_CRITICAL(); ++stats.errors; taskEXIT_CRITICAL(); }
}
void BLE_TX_AttResultFor(const char *operation,uint16_t attribute,tBleStatus result) {
    BLE_TX_AttResult(result);
    if (result!=BLE_STATUS_SUCCESS)
        LOG_WARNING_APP("BLE ATT failure op=%s attr=0x%04X status=0x%02X\n",operation,attribute,result);
}
void BLE_TX_RxResult(uint8_t accepted) {
    taskENTER_CRITICAL(); if (!accepted) ++stats.rx_rejected;
    uint32_t depth=osMessageQueueGetCount(commRxQueueHandle); if (depth>stats.rx_high) stats.rx_high=depth;
    taskEXIT_CRITICAL();
}
void BLE_TX_Stats(char *out,unsigned size) {
    if (!out || !size) return;
    uint32_t values[17];
    taskENTER_CRITICAL();
    values[0]=state; values[1]=stats.rx_rejected; values[2]=stats.tx_rejected;
    values[3]=stats.accepted; values[4]=stats.completed; values[5]=stats.pool;
    values[6]=stats.timeout; values[7]=stats.errors; values[8]=stats.stale;
    values[9]=stats.coalesced; values[10]=stats.rx_high; values[11]=stats.tx_high;
    values[12]=reservations(); values[13]=stats.max_completion_ticks;
    values[14]=0; for (unsigned i=0;i<CFG_BLE_NUM_LINK;i++) if (links[i].control.phase!=ATT_NONE) ++values[14];
    values[15]=stats.control_retries; values[16]=stats.control_fatal;
    taskEXIT_CRITICAL();
    /* Keep the complete diagnostic reply within 254 bytes at counter extremes.
     * Internal counters remain uint32; 9999999 means at least that many. */
    for (unsigned i=1;i<=9;i++) if (values[i]>9999999U) values[i]=9999999U;
    for (unsigned i=15;i<=16;i++) if (values[i]>9999999U) values[i]=9999999U;
    uint64_t lag_ms=(uint64_t)values[13]*1000U/osKernelGetTickFreq();
    if (lag_ms>9999U) lag_ms=9999U; /* bounded diagnostic field, 9999 means >=9999 */
    snprintf(out,size,"OK BLE_STATS\ns=%lu rsv=%lu\nrx=%lu/%lu\ntx=%lu/%lu\nrxrej=%lu\ntxrej=%lu\nsent=%lu\ndone=%lu\npool=%lu\ntimeout=%lu\nerr=%lu\nstale=%lu\nmerge=%lu\nheap=%lu\nmin=%lu\nstack=%lu\nhci=%lu\nlag=%lu\ncp=%lu\ncr=%lu\ncf=%lu\nOK BLE_STATS END",
        (unsigned long)values[0],(unsigned long)values[12],
        (unsigned long)osMessageQueueGetCount(commRxQueueHandle),(unsigned long)values[10],
        (unsigned long)osMessageQueueGetCount(commTxQueueHandle),(unsigned long)values[11],
        (unsigned long)values[1],(unsigned long)values[2],(unsigned long)values[3],(unsigned long)values[4],
        (unsigned long)values[5],(unsigned long)values[6],(unsigned long)values[7],(unsigned long)values[8],(unsigned long)values[9],
        (unsigned long)xPortGetFreeHeapSize(),(unsigned long)xPortGetMinimumEverFreeHeapSize(),
        (unsigned long)(uxTaskGetStackHighWaterMark(bleTxTaskHandle)*sizeof(StackType_t)),
        (unsigned long)(uxTaskGetStackHighWaterMark(HciAsyncEvtTaskHandle)*sizeof(StackType_t)),
        (unsigned long)lag_ms,(unsigned long)values[14],(unsigned long)values[15],(unsigned long)values[16]);
}
