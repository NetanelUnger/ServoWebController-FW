/* Compiles the actual production state machine with deterministic mock I/O.
 * Each scenario runs in a fresh process. Only diagnostic counter extremes
 * are injected directly; lifecycle transitions use production public APIs.
 * These tests model stack event order, not real controller or RF behavior. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../STM32_WPAN/App/ble_tx.h"
#include "ble_core.h"
#include "app_freertos.h"

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expression); exit(1); } } while (0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { BLE_TX_Item items[5]; unsigned head, count; } Queue;
static Queue tx_queue, rx_queue;
osMessageQueueId_t commTxQueueHandle=&tx_queue, commRxQueueHandle=&rx_queue;
osEventFlagsId_t bleTxEventHandle=(void *)1, commEventHandle=(void *)2;
osThreadId_t bleTxTaskHandle=(void *)3, HciAsyncEvtTaskHandle=(void *)4;
static uint32_t test_now, frequency=128, wakes, queue_put_wait, queue_puts, critical;
static unsigned calls, terminations, mtu=247, inject;
static uint16_t terminated;
static tBleStatus next_result, terminate_result;
static unsigned parser_calls;
static uint8_t parser_reply_length=2;
typedef struct { uint16_t connection, characteristic; uint8_t mode, length, payload[255]; } Sent;
static Sent sent[100];
typedef struct { uint16_t connection, attribute; uint8_t write, status, error, length, data[255]; } AttCall;
static AttCall att_history[100];
static unsigned att_calls, att_successes, att_inject_completion, att_inject_disconnect;
static tBleStatus att_result;
static unsigned att_inject_new_request;
static void att_reentrant_request(void);

static void event(uint16_t code,const void *data,size_t length);
static void complete(unsigned call);

uint32_t osKernelGetTickFreq(void) { return frequency; }
uint32_t osKernelGetTickCount(void) { return test_now; }
void test_enter_critical(void) { ++critical; }
void test_exit_critical(void) { CHECK(critical); --critical; }
osStatus_t osMessageQueuePut(osMessageQueueId_t id,const void *item,uint8_t priority,uint32_t timeout) {
    Queue *q=id; (void)priority; CHECK(timeout==0); queue_put_wait=timeout; ++queue_puts;
    if(q->count==ARRAY_SIZE(q->items)) return osErrorResource;
    q->items[(q->head+q->count)%ARRAY_SIZE(q->items)]=*(const BLE_TX_Item *)item;
    ++q->count; return osOK;
}
osStatus_t osMessageQueueGet(osMessageQueueId_t id,void *item,uint8_t *priority,uint32_t timeout) {
    Queue *q=id; (void)priority; CHECK(timeout==0);
    if(!q->count) return osErrorResource;
    *(BLE_TX_Item *)item=q->items[q->head]; q->head=(q->head+1)%ARRAY_SIZE(q->items); --q->count; return osOK;
}
uint32_t osMessageQueueGetCount(osMessageQueueId_t id) { return ((Queue *)id)->count; }
uint32_t osMessageQueueGetSpace(osMessageQueueId_t id) { return (uint32_t)ARRAY_SIZE(((Queue *)id)->items)-((Queue *)id)->count; }
uint32_t osEventFlagsSet(osEventFlagsId_t id,uint32_t flags) { CHECK(id); ++wakes; return flags; }
uint32_t osEventFlagsWait(osEventFlagsId_t id,uint32_t flags,uint32_t options,uint32_t timeout) {
    (void)id; (void)flags; (void)options; (void)timeout; CHECK(!"Blocking wait is forbidden in a transport step"); return 0;
}
size_t xPortGetFreeHeapSize(void) { return 10000; }
size_t xPortGetMinimumEverFreeHeapSize(void) { return 9999; }
uint32_t uxTaskGetStackHighWaterMark(osThreadId_t task) { (void)task; return 100; }
tBleStatus aci_gap_terminate(uint16_t handle,uint8_t reason) { CHECK(!critical && reason==0x13); ++terminations; terminated=handle; return terminate_result; }
tBleStatus aci_gatt_update_char_value_ext(uint16_t connection,uint16_t service,uint16_t characteristic,uint8_t mode,uint16_t length,uint16_t offset,uint8_t value_length,const uint8_t *data) {
    Sent *s; tBleStatus result=next_result; next_result=BLE_STATUS_SUCCESS;
    CHECK(!critical && calls<ARRAY_SIZE(sent) && service && characteristic);
    CHECK(offset==0 && length==value_length && data);
    s=&sent[calls++]; s->connection=connection; s->characteristic=characteristic;
    s->mode=mode; s->length=value_length; memcpy(s->payload,data,value_length);
    if(inject==1) { inject=0; complete(calls-1); }
    else if(inject==2) { uint16_t pool[2]={0xffff,1}; inject=0; event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,sizeof(pool)); }
    else if(inject==3 || inject==4) { unsigned action=inject; inject=0; BLE_TX_Disconnected(connection); if(action==4) BLE_TX_EventsDrained(); }
    if(mode && length>mtu-3) return BLE_STATUS_INVALID_PARAMS;
    return result;
}
static tBleStatus att_call(uint16_t connection,uint16_t attribute,uint8_t write,uint8_t status,uint8_t error,uint8_t length,const uint8_t *data) {
    CHECK(!critical && att_calls<ARRAY_SIZE(att_history));
    AttCall *a=&att_history[att_calls++]; a->connection=connection; a->attribute=attribute;
    a->write=write; a->status=status; a->error=error; a->length=length;
    if(length) { CHECK(data); memcpy(a->data,data,length); }
    if(att_inject_completion) { unsigned index=att_inject_completion-1; att_inject_completion=0; complete(index); }
    if(att_inject_disconnect) { unsigned connection_to_drop=att_inject_disconnect; att_inject_disconnect=0; BLE_TX_Disconnected((uint16_t)connection_to_drop); }
    if(att_inject_new_request) { att_inject_new_request=0; att_reentrant_request(); }
    if(att_result==BLE_STATUS_SUCCESS) ++att_successes;
    return att_result;
}
tBleStatus aci_gatt_permit_read(uint16_t connection,uint8_t status,uint8_t error,uint16_t attribute) {
    return att_call(connection,attribute,0,status,error,0,NULL);
}
tBleStatus aci_gatt_write_resp(uint16_t connection,uint16_t attribute,uint8_t status,uint8_t error,uint8_t length,const uint8_t *data) {
    return att_call(connection,attribute,1,status,error,length,data);
}
#include "../STM32_WPAN/App/ble_tx.c"
typedef BLE_TX_Item NUS_APP_QueueItem_t;
#include "rx_under_test.inc"
typedef uint8_t packetSize_t;
#define COMM_TEXT_RESPONSE_MAX_LEN 255U
static packetSize_t parser_ProcessOne(const uint8_t *input,packetSize_t length,uint8_t *output) {
    CHECK(!critical && input && length); ++parser_calls; memset(output,'R',parser_reply_length); return parser_reply_length;
}
#include "rx_task_under_test.inc"
static unsigned servo_write_calls, servo_mutations, servo_refreshes, servo_syncs;
static tBleStatus servo_refresh_result;
#define NUS_ALL_CHANNEL_DC_ATT_ERROR 0x80U
static tBleStatus NUS_APP_WriteAllChannelDcPayload(const uint8_t *data,uint8_t length) {
    ++servo_write_calls;
    if(!data || length!=32) return BLE_STATUS_INVALID_PARAMS;
    ++servo_mutations; return BLE_STATUS_SUCCESS;
}
static tBleStatus NUS_APP_RefreshAllChannelDcRead(void) { ++servo_refreshes; return servo_refresh_result; }
static tBleStatus NUS_APP_SyncAllChannelDcCharacteristic(void) { ++servo_syncs; return BLE_STATUS_SUCCESS; }
#include "nus_permit_read_under_test.inc"
#include "nus_permit_write_under_test.inc"

static uint16_t characteristic(BLE_TX_Stream stream) { return (uint16_t)(0x100U+(unsigned)stream*0x10U); }
static void event(uint16_t code,const void *data,size_t length) {
    uint8_t packet[300]={0}; hci_uart_pckt *uart=(void *)packet;
    hci_event_pckt *hci=(void *)uart->data; evt_blecore_aci *vendor=(void *)hci->data;
    CHECK(length<=253); uart->type=4; hci->evt=HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE;
    hci->plen=(uint8_t)(length+2); vendor->ecode=code; memcpy(vendor->data,data,length);
    BLE_TX_Event(packet); CHECK(!critical);
}
static void cccd(uint16_t handle,BLE_TX_Stream stream,unsigned mode) {
    aci_gatt_attribute_modified_event_rp0 data={0}; data.Connection_Handle=handle;
    data.Attr_Handle=(uint16_t)(characteristic(stream)+2); data.Attr_Data_Length=2; data.Attr_Data[0]=(uint8_t)mode;
    event(ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE,&data,10);
}
static void complete(unsigned call) {
    uint16_t data; CHECK(call<calls);
    if(sent[call].mode==2) { data=sent[call].connection; event(ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE,&data,2); }
    else { data=(uint16_t)(sent[call].characteristic+1); event(ACI_GATT_NOTIFICATION_COMPLETE_VSEVT_CODE,&data,2); }
}
static uint32_t step(void) { uint32_t wait=BLE_TX_Run(); CHECK(!critical); return wait; }
static void start(void) {
    for(unsigned i=0;i<BLE_TX_STREAM_COUNT;++i) BLE_TX_Register((BLE_TX_Stream)i,1,characteristic((BLE_TX_Stream)i));
    BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1);
}
static BLE_TX_Item item(uint16_t handle,BLE_TX_Stream stream,const char *text) {
    BLE_TX_Item value={0}; CHECK(BLE_TX_Capture(handle,stream,&value));
    CHECK(strlen(text)<=BLE_TX_PAYLOAD_MAX); value.Length=(uint8_t)strlen(text); memcpy(value.Payload,text,value.Length); return value;
}
static void enqueue(const char *text) { BLE_TX_Item value=item(1,BLE_TX_REPLY,text); CHECK(BLE_TX_Enqueue(&value,0)==osOK); }
static void payload(unsigned n,const char *text) { CHECK(n<calls && sent[n].length==strlen(text) && !memcmp(sent[n].payload,text,strlen(text))); }
static void success_completion(void) {
    start(); enqueue("first"); enqueue("second"); CHECK(step()==0 && calls==1); payload(0,"first");
    for(unsigned i=0;i<10;++i) { ++test_now; CHECK(step()>0 && calls==1); }
    uint16_t wrong=0xbeef; event(ACI_GATT_NOTIFICATION_COMPLETE_VSEVT_CODE,&wrong,2); CHECK(step()>0 && calls==1);
    complete(0); step(); CHECK(calls==2); payload(1,"second"); complete(1); CHECK(step()==osWaitForever);
    complete(1); CHECK(step()==osWaitForever && calls==2);
}
static void pool_wakeup(void) {
    start(); enqueue("retained"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); CHECK(calls==1);
    for(unsigned i=0;i<20;++i) { ++test_now; CHECK(step()>0 && calls==1); }
    uint16_t pool[2]={0xbeef,1}; event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,sizeof(pool)); step();
    CHECK(calls==2); payload(1,"retained"); complete(1); CHECK(step()==osWaitForever);
}
static void reentrant_events(void) {
    start(); enqueue("sync"); inject=1; step(); CHECK(calls==1); CHECK(step()==osWaitForever && calls==1);
    enqueue("pool"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; inject=2; step(); CHECK(calls==2);
    step(); CHECK(calls==3); complete(2); CHECK(step()==osWaitForever);
}
static void delayed_timeout(void) {
    start(); enqueue("lost\nHELP END"); step(); CHECK(calls==1); payload(0,"lost");
    test_now=255; CHECK(step()==1 && !terminations); test_now=256; step(); CHECK(terminations==1 && terminated==1 && calls==1);
    CHECK(step()>0 && calls==1); BLE_TX_Disconnected(1); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1);
    enqueue("new"); CHECK(step()==osWaitForever && calls==1); complete(0); BLE_TX_EventsDrained();
    step(); CHECK(calls==2); payload(1,"new"); complete(1); CHECK(step()==osWaitForever);
}
static void disconnect_barrier(void) {
    start(); BLE_TX_Item old=item(1,BLE_TX_REPLY,"old"); CHECK(BLE_TX_Enqueue(&old,0)==osOK); step();
    BLE_TX_Disconnected(1); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1);
    CHECK(!BLE_TX_Current(&old) && BLE_TX_Enqueue(&old,0)!=osOK); enqueue("fresh");
    CHECK(step()==osWaitForever && calls==1); BLE_TX_EventsDrained(); step(); CHECK(calls==2); payload(1,"fresh");
    complete(1); CHECK(step()==osWaitForever);
}
static void disconnect_pool(void) {
    start(); enqueue("old"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); CHECK(calls==1);
    BLE_TX_Disconnected(1); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("fresh"); BLE_TX_EventsDrained();
    step(); CHECK(calls==2 && !terminations); payload(1,"fresh");
    complete(1); test_now=1000; CHECK(step()==osWaitForever && !terminations);
}
static void subscription_modes(void) {
    start();
    for(unsigned mode=0;mode<4;++mode) {
        cccd(1,BLE_TX_REPLY,mode); BLE_TX_Item value=item(1,BLE_TX_REPLY,"mode");
        if(!mode) { CHECK(!BLE_TX_Current(&value) && BLE_TX_Enqueue(&value,0)!=osOK); CHECK(step()==osWaitForever); continue; }
        CHECK(BLE_TX_Enqueue(&value,0)==osOK); unsigned before=calls; step(); CHECK(calls==before+1 && sent[before].mode==(mode&1?1:2));
        uint16_t wrong=mode==2 ? 2 : (uint16_t)(characteristic(BLE_TX_REPLY)+1);
        event(ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE,&wrong,2);
        CHECK(step()>0 && calls==before+1); complete(before); CHECK(step()==osWaitForever);
    }
    BLE_TX_Item stale=item(1,BLE_TX_REPLY,"queued"); CHECK(BLE_TX_Enqueue(&stale,0)==osOK);
    cccd(1,BLE_TX_REPLY,0); cccd(1,BLE_TX_REPLY,1); CHECK(!BLE_TX_Current(&stale)); unsigned before=calls;
    CHECK(step()==osWaitForever && calls==before);
}
static void priority_and_coalescing(void) {
    start(); cccd(1,BLE_TX_TOF,1); const uint8_t first[]={1}, latest[]={2};
    CHECK(BLE_TX_Latest(BLE_TX_TOF,first,1)==0); CHECK(BLE_TX_Latest(BLE_TX_TOF,latest,1)==0); enqueue("priority");
    step(); CHECK(calls==1); payload(0,"priority"); complete(0); step(); CHECK(calls==2 && sent[1].characteristic==characteristic(BLE_TX_TOF) && sent[1].payload[0]==2);
    complete(1); step(); BLE_TX_Latest(BLE_TX_TOF,first,1); uint32_t wait=step(); CHECK(wait>0 && wait!=osWaitForever && calls==2);
    enqueue("reply"); step(); CHECK(calls==3); payload(2,"reply"); complete(2); step(); test_now+=wait; step(); CHECK(calls==4 && sent[3].payload[0]==1);
}
static void multilink(void) {
    start(); BLE_TX_Connected(2); cccd(2,BLE_TX_REPLY,2); cccd(1,BLE_TX_TOF,1); cccd(2,BLE_TX_TOF,1);
    CHECK(BLE_TX_Ordered(BLE_TX_REPLY,(const uint8_t *)"both",4)==0); step(); CHECK(calls==1 && sent[0].connection==1);
    complete(0); step(); CHECK(calls==2 && sent[1].connection==2 && sent[1].mode==2);
    uint16_t wrong=1; event(ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE,&wrong,2); CHECK(step()>0 && calls==2);
    complete(1); step(); const uint8_t t[]={7}; BLE_TX_Latest(BLE_TX_TOF,t,1); step(); CHECK(calls==3);
    BLE_TX_Disconnected(2); complete(2); step(); test_now+=100; step(); CHECK(calls==3);
}
static void length_and_mtu(void) {
    start(); uint8_t data[256]={0}; CHECK(BLE_TX_Ordered(BLE_TX_REPLY,NULL,1)==BLE_STATUS_INVALID_PARAMS);
    CHECK(BLE_TX_Ordered(BLE_TX_REPLY,data,0)==BLE_STATUS_INVALID_PARAMS); CHECK(BLE_TX_Ordered(BLE_TX_REPLY,data,256)==BLE_STATUS_INVALID_PARAMS);
    CHECK(BLE_TX_Latest(BLE_TX_TOF,NULL,1)==BLE_STATUS_INVALID_PARAMS); CHECK(BLE_TX_Latest(BLE_TX_TOF,data,0)==BLE_STATUS_INVALID_PARAMS);
    CHECK(BLE_TX_Latest(BLE_TX_TOF,data,65)==BLE_STATUS_INVALID_PARAMS); CHECK(BLE_TX_Latest(BLE_TX_REPLY,data,1)==BLE_STATUS_INVALID_PARAMS);
    CHECK(BLE_TX_Local(1,characteristic(BLE_TX_REPLY),data,244)==BLE_STATUS_INVALID_PARAMS);
    mtu=23; enqueue("123456789012345678901\nHELP END"); step(); CHECK(calls==1); payload(0,"- MTU TOO SMALL"); complete(0); CHECK(step()==osWaitForever && calls==1);
    enqueue("12345678901234567890"); step(); CHECK(calls==2 && sent[1].length==20); complete(1); CHECK(step()==osWaitForever);
    mtu=247; uint16_t exchange[2]={1,247}; event(ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE,exchange,4); BLE_TX_Item long_item=item(1,BLE_TX_REPLY,"x"); long_item.Length=243; memset(long_item.Payload,'x',243);
    CHECK(BLE_TX_Enqueue(&long_item,0)==osOK); step(); CHECK(calls==3 && sent[2].length==243); complete(2); step();
    long_item.Length=244; CHECK(BLE_TX_Enqueue(&long_item,0)==osOK); step(); CHECK(calls==4); payload(3,"- MTU TOO SMALL"); complete(3); CHECK(step()==osWaitForever);
}
static void queue_full_and_copy(void) {
    start(); BLE_TX_Item value=item(1,BLE_TX_REPLY,"copy");
    for(unsigned i=0;i<5;++i) CHECK(BLE_TX_Enqueue(&value,0)==osOK);
    CHECK(BLE_TX_Enqueue(&value,0)==osErrorResource && !queue_put_wait); memset(value.Payload,'x',value.Length);
    for(unsigned i=0;i<5;++i) { step(); CHECK(calls==i+1); payload(i,"copy"); complete(i); }
    CHECK(step()==osWaitForever);
}
static void tick_rollover(void) {
    test_now=UINT32_MAX-10; start(); enqueue("wrap"); step(); test_now+=255; CHECK(step()==1); ++test_now; step(); CHECK(terminations==1);
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); CHECK(step()==osWaitForever);
}
static void pool_timeout(void) {
    start(); enqueue("pool\nHELP END"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); test_now=256; step();
    CHECK(terminations==1 && calls==1); BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1);
    enqueue("recovered"); step(); CHECK(calls==2); payload(1,"recovered");
}

static void rx_admission(void) {
    start(); uint8_t command[256]={1}; unsigned before=queue_puts;
    CHECK(NUS_APP_EnqueueRX(1,NULL,1)!=osOK); CHECK(NUS_APP_EnqueueRX(1,command,0)!=osOK);
    CHECK(NUS_APP_EnqueueRX(1,command,256)!=osOK); CHECK(NUS_APP_EnqueueRX(2,command,1)!=osOK);
    CHECK(queue_puts==before && rx_queue.count==0);
    for(unsigned i=0;i<5;++i) CHECK(NUS_APP_EnqueueRX(1,command,255)==osOK && queue_put_wait==0);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK && rx_queue.count==5);
    memset(command,0xee,sizeof(command)); BLE_TX_Item received;
    CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK);
    CHECK(received.Length==255 && received.Payload[0]==1 && received.Payload[254]==0 && received.Reserved);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK); /* Executing command still owns reply credit. */
    BLE_TX_Release(&received); CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    /* A full RX ring releases any newly acquired reply reservation on failure. */
    while(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK) BLE_TX_Release(&received);
    memset(&received,0,sizeof(received)); for(unsigned i=0;i<5;++i) CHECK(osMessageQueuePut(commRxQueueHandle,&received,0,0)==osOK);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK); rx_queue.head=rx_queue.count=0;
    for(unsigned i=0;i<5;++i) CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
}
static void reservation_capacity(void) {
    start(); const uint8_t command[]={1}; BLE_TX_Item received;
    CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK);
    received.Length=2; memcpy(received.Payload,"ok",2); CHECK(BLE_TX_Enqueue(&received,99)==osOK && !queue_put_wait);
    /* Queued reply uses its reservation, not a second unit of the capacity. */
    for(unsigned i=0;i<4;++i) CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK);
    step(); complete(0); step(); CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
}
static void stale_reservations(void) {
    start(); uint8_t command[]={1}; BLE_TX_Item old;
    CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK); CHECK(osMessageQueueGet(commRxQueueHandle,&old,NULL,0)==osOK);
    old.Length=3; memcpy(old.Payload,"old",3); CHECK(BLE_TX_Enqueue(&old,0)==osOK);
    cccd(1,BLE_TX_REPLY,0); cccd(1,BLE_TX_REPLY,1); CHECK(step()==osWaitForever && calls==0);
    for(unsigned i=0;i<5;++i) CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    BLE_TX_Disconnected(1); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); BLE_TX_EventsDrained();
    while(osMessageQueueGet(commRxQueueHandle,&old,NULL,0)==osOK) NusRxTask(&old);
    CHECK(parser_calls==0); /* Stale requests never reach the side-effecting parser. */
    for(unsigned i=0;i<5;++i) CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK);

}
static void disconnect_during_submit(void) {
    start(); enqueue("old"); inject=3; next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step();
    BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); BLE_TX_EventsDrained(); enqueue("new"); step();
    CHECK(calls==2 && !terminations); payload(1,"new"); complete(1); CHECK(step()==osWaitForever);
}
static void termination_retry(void) {
    start(); enqueue("lost\nHELP END"); step(); terminate_result=BLE_STATUS_FAILED; test_now=256;
    CHECK(step()==128 && terminations==1 && calls==1); CHECK(step()==128 && calls==1);
    test_now+=128; CHECK(step()==128 && terminations==2 && calls==1);
    terminate_result=BLE_STATUS_SUCCESS; test_now+=128; CHECK(step()==128 && terminations==3 && calls==1);
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("new"); step(); CHECK(calls==2); payload(1,"new");
}
static void late_completion_after_timeout(void) {
    start(); enqueue("lost\nHELP END"); step(); test_now=256; step(); CHECK(terminations==1 && calls==1);
    complete(0); step(); CHECK(calls==1); /* Never resume a response after requesting link termination. */
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("new"); step(); CHECK(calls==2); payload(1,"new");
}
static void mtu_telemetry(void) {
    start(); cccd(1,BLE_TX_TOF,1); uint8_t map[64]; memset(map,42,sizeof(map));
    CHECK(BLE_TX_Latest(BLE_TX_TOF,map,64)==0); CHECK(step()==osWaitForever && calls==0);
    uint16_t exchange[2]={2,247}; event(ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE,exchange,4); CHECK(step()==osWaitForever && calls==0);
    exchange[0]=1; exchange[1]=22; event(ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE,exchange,4); CHECK(step()==osWaitForever && calls==0);
    exchange[1]=247; event(ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE,exchange,4); step(); CHECK(calls==1 && sent[0].length==64 && !memcmp(sent[0].payload,map,64));
}
static void malformed_cccd(void) {
    start(); cccd(1,BLE_TX_REPLY,0); aci_gatt_attribute_modified_event_rp0 value={0};
    value.Connection_Handle=1; value.Attr_Handle=(uint16_t)(characteristic(BLE_TX_REPLY)+2); value.Attr_Data_Length=1; value.Attr_Data[0]=1;
    event(ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE,&value,9); BLE_TX_Item result=item(1,BLE_TX_REPLY,"no"); CHECK(!BLE_TX_Current(&result));
    value.Connection_Handle=2; value.Attr_Data_Length=2; event(ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE,&value,10); CHECK(!BLE_TX_Current(&result));
}
static void invalid_inputs(void) {
    start(); BLE_TX_Item value={0}; uint8_t bytes[1]={0};
    CHECK(!BLE_TX_Capture(1,BLE_TX_REPLY,NULL)); CHECK(!BLE_TX_Capture(1,BLE_TX_STREAM_COUNT,&value));
    CHECK(!BLE_TX_Capture(1,(BLE_TX_Stream)-1,&value)); CHECK(!BLE_TX_Capture(0xffff,BLE_TX_REPLY,&value));
    CHECK(!BLE_TX_Current(NULL) && !BLE_TX_SessionCurrent(NULL) && !BLE_TX_ReserveRx(NULL)); BLE_TX_Release(NULL);
    CHECK(BLE_TX_Enqueue(NULL,0)!=osOK); CHECK(BLE_TX_Ordered(BLE_TX_STREAM_COUNT,bytes,1)==BLE_STATUS_INVALID_PARAMS);
    CHECK(BLE_TX_Ordered((BLE_TX_Stream)-1,bytes,1)==BLE_STATUS_INVALID_PARAMS); CHECK(BLE_TX_Local(1,characteristic(BLE_TX_REPLY),NULL,1)==BLE_STATUS_INVALID_PARAMS);
    BLE_TX_Register((BLE_TX_Stream)-1,1,1); BLE_TX_Register(BLE_TX_STREAM_COUNT,1,1);
    value=item(1,BLE_TX_REPLY,""); CHECK(BLE_TX_Enqueue(&value,0)!=osOK); CHECK(step()==osWaitForever);
}
static void bounded_stats(void) {
    start(); char out[COMM_TEXT_RESPONSE_MAX_LEN];
    /* Fault-inject long-running counter extremes, without touching lifecycle state. */
    memset(&stats,0xff,sizeof(stats)); stats.rx_high=5; stats.tx_high=5; BLE_TX_Stats(out,sizeof(out));
    if(!strstr(out,"OK BLE_STATS END")) fprintf(stderr,"Truncated stats (%zu bytes): %s\n",strlen(out),out);
    CHECK(strstr(out,"OK BLE_STATS END")!=NULL && strlen(out)<COMM_TEXT_RESPONSE_MAX_LEN);
    CHECK(strstr(out,"cp=0") && strstr(out,"cr=9999999") && strstr(out,"cf=9999999"));
    char tiny[4]={1,1,1,1}; BLE_TX_Stats(tiny,sizeof(tiny)); CHECK(tiny[3]==0); BLE_TX_Stats(NULL,0);
}


static void rx_reply_lifecycle(void) {
    start(); uint8_t command[]={1}; BLE_TX_Item received;
    CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK); CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK);
    NusRxTask(&received); CHECK(parser_calls==1 && tx_queue.count==1); step(); payload(0,"RR"); complete(0); step();
    parser_reply_length=0; CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK); CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK);
    NusRxTask(&received); CHECK(parser_calls==2 && tx_queue.count==0);
    cccd(1,BLE_TX_REPLY,0); parser_reply_length=2;
    CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK); CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK);
    NusRxTask(&received); CHECK(parser_calls==3 && tx_queue.count==0); cccd(1,BLE_TX_REPLY,1);
    for(unsigned i=0;i<5;++i) CHECK(NUS_APP_EnqueueRX(1,command,1)==osOK);
    CHECK(NUS_APP_EnqueueRX(1,command,1)!=osOK);
}
static void submit_disconnect_drained(void) {
    start(); enqueue("old"); inject=4; step(); CHECK(calls==1);
    BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("new"); step(); CHECK(calls==2); payload(1,"new"); complete(1); CHECK(step()==osWaitForever);
}
static void subscription_switch_inflight(void) {
    start(); enqueue("first\nold tail"); step(); cccd(1,BLE_TX_REPLY,0); cccd(1,BLE_TX_REPLY,2);
    enqueue("new"); BLE_TX_EventsDrained(); CHECK(step()>0 && calls==1); complete(0); step();
    CHECK(calls==2 && sent[1].mode==2); payload(1,"new"); complete(1); CHECK(step()==osWaitForever);
}
static void telemetry_link_fairness(void) {
    start(); BLE_TX_Connected(2); cccd(1,BLE_TX_ADC,1); cccd(2,BLE_TX_ADC,1); uint8_t data[]={1};
    CHECK(BLE_TX_Latest(BLE_TX_ADC,data,1)==0); step(); CHECK(calls==1 && sent[0].connection==1); complete(0); step();
    data[0]=2; CHECK(BLE_TX_Latest(BLE_TX_ADC,data,1)==0); test_now=26; step(); CHECK(calls==2 && sent[1].connection==2 && sent[1].payload[0]==2);
    complete(1); step(); data[0]=3; BLE_TX_Latest(BLE_TX_ADC,data,1); test_now+=26; step(); CHECK(calls==3 && sent[2].connection==1);
}
static void ota_confirmation_ownership(void) {
    start(); cccd(1,BLE_TX_OTA,2); const uint8_t data[]={1}; BLE_TX_Ordered(BLE_TX_OTA,data,1); step(); CHECK(calls==1);
    CHECK(!BLE_TX_ConfirmationIsOta(1)); uint16_t wrong=2; event(ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE,&wrong,2);
    CHECK(!BLE_TX_ConfirmationIsOta(1)); complete(0); CHECK(BLE_TX_ConfirmationIsOta(1) && !BLE_TX_ConfirmationIsOta(2));
    step(); CHECK(!BLE_TX_ConfirmationIsOta(1));
}
static void stack_rejection(void) {
    start(); enqueue("first\nHELP END"); enqueue("second"); next_result=BLE_STATUS_INVALID_PARAMS; step(); CHECK(calls==1);
    step(); CHECK(calls==1 && terminations==1); /* A lost reliable response is an observable disconnect, not silent continuation. */
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("fresh");
    step(); CHECK(calls==2); payload(1,"fresh"); complete(1); CHECK(step()==osWaitForever);
}


static void pool_timeout_keeps_barrier(void) {
    start(); enqueue("stalled\nHELP END"); enqueue("queued old"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES;
    step(); terminate_result=BLE_STATUS_FAILED; test_now=256; step(); CHECK(calls==1 && terminations==1);
    test_now+=128; step(); CHECK(calls==1 && terminations==2);
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); enqueue("fresh");
    step(); CHECK(calls==2); payload(1,"fresh");
}


static void reply_preempts_unaccepted_telemetry(void) {
    start(); cccd(1,BLE_TX_TOF,1); uint8_t data[]={1}; BLE_TX_Latest(BLE_TX_TOF,data,1);
    next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); CHECK(calls==1);
    enqueue("urgent reply"); data[0]=2; BLE_TX_Latest(BLE_TX_TOF,data,1);
    uint16_t pool[2]={0xffff,1}; event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,4); step();
    CHECK(calls==2); payload(1,"urgent reply"); complete(1);
    uint32_t wait=step(); if(calls==2) { CHECK(wait!=osWaitForever); test_now+=wait; step(); }
    CHECK(calls==3 && sent[2].characteristic==characteristic(BLE_TX_TOF) && sent[2].payload[0]==2);
}

#include "ble_transport_att_tests.inc"

static const struct { const char *name; void (*run)(void); } scenarios[]={
    {"att_fatal_keeps_accepted_flight",att_fatal_keeps_accepted_flight},
    {"att_fatal_disconnect_without_completion",att_fatal_disconnect_without_completion},
    {"att_reentrant_old_resource_failure",att_reentrant_old_resource_failure},
    {"att_reentrant_old_pending",att_reentrant_old_pending},
    {"att_collision_blocks_side_effects",att_collision_blocks_side_effects},
    {"att_all_links_wake_fairness",att_all_links_wake_fairness},
    {"att_uncommitted_absolute_timeout",att_uncommitted_absolute_timeout},
    {"att_retry_due_zero",att_retry_due_zero}, {"att_reserved_deadline_zero",att_reserved_deadline_zero},
    {"att_reentrant_next_request",att_reentrant_next_request}, {"att_fairness_under_pool_wakes",att_fairness_under_pool_wakes},
    {"att_read_retry_without_event",att_read_retry_without_event},
    {"att_write_copied_bytes",att_write_copied_bytes},
    {"att_priority_over_notification",att_priority_over_notification},
    {"att_priority_during_pool_wait",att_priority_during_pool_wait},
    {"att_completion_during_recovery",att_completion_during_recovery},
    {"att_nus_command_once",att_nus_command_once},
    {"att_servo_write_once",att_servo_write_once},
    {"att_servo_read_once",att_servo_read_once},
    {"att_stale_ticket_reconnect",att_stale_ticket_reconnect},
    {"att_two_links",att_two_links},
    {"att_fatal_invalid",att_fatal_invalid}, {"att_ambiguous_busy",att_ambiguous_busy},
    {"att_ambiguous_error",att_ambiguous_error}, {"att_ambiguous_security",att_ambiguous_security},
    {"att_deadline_rollover",att_deadline_rollover},
    {"att_static_bounds",att_static_bounds},
    {"att_disconnect_during_call",att_disconnect_during_call},
    {"reply_preempts_unaccepted_telemetry",reply_preempts_unaccepted_telemetry},
    {"pool_timeout_keeps_barrier",pool_timeout_keeps_barrier},
    {"rx_reply_lifecycle",rx_reply_lifecycle}, {"submit_disconnect_drained",submit_disconnect_drained},
    {"subscription_switch_inflight",subscription_switch_inflight}, {"telemetry_link_fairness",telemetry_link_fairness},
    {"ota_confirmation_ownership",ota_confirmation_ownership}, {"stack_rejection",stack_rejection},
    {"rx_admission",rx_admission}, {"reservation_capacity",reservation_capacity}, {"stale_reservations",stale_reservations},
    {"disconnect_during_submit",disconnect_during_submit}, {"termination_retry",termination_retry}, {"late_completion_after_timeout",late_completion_after_timeout},
    {"mtu_telemetry",mtu_telemetry}, {"malformed_cccd",malformed_cccd}, {"invalid_inputs",invalid_inputs}, {"bounded_stats",bounded_stats},
    {"success_completion",success_completion}, {"pool_wakeup",pool_wakeup}, {"reentrant_events",reentrant_events},
    {"delayed_timeout",delayed_timeout}, {"disconnect_barrier",disconnect_barrier}, {"disconnect_pool",disconnect_pool},
    {"subscription_modes",subscription_modes}, {"priority_and_coalescing",priority_and_coalescing}, {"multilink",multilink},
    {"length_and_mtu",length_and_mtu}, {"queue_full_and_copy",queue_full_and_copy}, {"tick_rollover",tick_rollover}, {"pool_timeout",pool_timeout}
};
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    for(unsigned i=0;i<ARRAY_SIZE(scenarios);++i) {
        if(!strcmp(argv[1],"--list")) puts(scenarios[i].name);
        else if(!strcmp(argv[1],scenarios[i].name)) { scenarios[i].run(); CHECK(!critical); printf("PASS %s\n",scenarios[i].name); return 0; }
    }
    return strcmp(argv[1],"--list")?2:0;
}
