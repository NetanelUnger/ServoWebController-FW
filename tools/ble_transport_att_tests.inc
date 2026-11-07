/* ATT recovery scenarios: drives the real ble_tx.c public API and the exact
 * NUS permission helpers. Controller responses and servo side effects are
 * mocked; callback admission/dispatch/retry logic is production code. */
static BLE_TX_AttTicket att_begin(uint16_t connection,uint16_t attribute,uint8_t write,const uint8_t *data,uint16_t length) {
    BLE_TX_AttTicket ticket; CHECK(BLE_TX_AttBegin(connection,attribute,write,data,length,&ticket)); return ticket;
}
static void att_advance_to_attempt(unsigned target) {
    for(unsigned count=0;count<50 && att_calls<target;++count) {
        uint32_t wait=step();
        if(att_calls>=target) break;
        CHECK(wait!=osWaitForever && wait<=256U);
        test_now+=wait;
    }
    CHECK(att_calls==target);
}
static void att_read_retry_without_event(void) {
    start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    CHECK(att_calls==0); att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1);
    CHECK(!terminations && !calls && !att_successes);
    uint32_t wait=step(); CHECK(wait>0 && wait<=7); /* 50ms rounds up at128Hz. */
    att_result=BLE_STATUS_SUCCESS; test_now+=wait; att_advance_to_attempt(2);
    CHECK(att_successes==1 && !terminations && att_history[1].write==0 && att_history[1].status==0);
    for(unsigned i=0;i<5;++i) step(); CHECK(att_calls==2);
    BLE_TX_AttCommit(&ticket,0,0); step(); CHECK(att_calls==2); /* Obsolete commit cannot replay. */
}
static void att_write_copied_bytes(void) {
    start(); uint8_t data[255]; for(unsigned i=0;i<255;++i) data[i]=(uint8_t)i;
    BLE_TX_AttTicket ticket=att_begin(1,0x202,1,data,255); memset(data,0xee,sizeof(data)); BLE_TX_AttCommit(&ticket,0,0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1);
    CHECK(att_history[0].write && att_history[0].length==255);
    for(unsigned i=0;i<255;++i) CHECK(att_history[0].data[i]==(uint8_t)i);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2);
    CHECK(att_history[1].length==255 && !memcmp(att_history[0].data,att_history[1].data,255));
}
static void att_priority_over_notification(void) {
    start(); enqueue("reply"); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    step(); CHECK(att_calls==1 && calls==0); step(); CHECK(calls==1); payload(0,"reply"); complete(0); step();
}
static void att_priority_during_pool_wait(void) {
    start(); enqueue("reply"); next_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); CHECK(calls==1);
    BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; step(); CHECK(att_calls==1 && calls==1);
    uint16_t pool[2]={0xffff,1}; event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,4);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2); CHECK(calls==1);
    step(); CHECK(calls==2); payload(1,"reply"); complete(1); step();
}
static void att_completion_during_recovery(void) {
    start(); enqueue("first"); enqueue("second"); step(); CHECK(calls==1);
    BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_inject_completion=1; att_advance_to_attempt(1);
    for(unsigned i=0;i<4 && stats.completed!=1;++i) step();
    CHECK(calls==1 && stats.completed==1 && !terminations);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(att_calls+1); step(); CHECK(calls==2); payload(1,"second");
    complete(1); step(); CHECK(stats.accepted==2 && stats.completed==2);
}
static void att_nus_command_once(void) {
    start(); uint8_t command[]={'P','I','N','G'};
    NUS_PermitWrite(1,0x202,command,sizeof(command),0); memset(command,'x',sizeof(command));
    CHECK(rx_queue.count==1 && att_calls==0 && parser_calls==0);
    BLE_TX_Item received; CHECK(osMessageQueueGet(commRxQueueHandle,&received,NULL,0)==osOK); CHECK(!memcmp(received.Payload,"PING",4));
    NusRxTask(&received); CHECK(parser_calls==1 && tx_queue.count==1);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1); att_advance_to_attempt(2);
    CHECK(parser_calls==1 && calls==0 && rx_queue.count==0 && !memcmp(att_history[1].data,"PING",4));
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(3); step(); CHECK(calls==1 && parser_calls==1); payload(0,"RR");
}
static void att_servo_write_once(void) {
    start(); uint8_t data[32]; memset(data,0x42,sizeof(data)); NUS_PermitWrite(1,0x203,data,32,1);
    CHECK(servo_write_calls==1 && servo_mutations==1 && att_calls==0); memset(data,0xee,sizeof(data));
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1); att_advance_to_attempt(2);
    CHECK(servo_write_calls==1 && servo_mutations==1 && att_history[1].data[0]==0x42);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(3); CHECK(servo_mutations==1 && att_successes==1);
    NUS_PermitWrite(1,0x203,data,31,1); att_advance_to_attempt(4);
    CHECK(servo_mutations==1 && att_history[3].status==1 && att_history[3].error==NUS_ALL_CHANNEL_DC_ATT_ERROR);
}
static void att_servo_read_once(void) {
    start(); NUS_PermitRead(1,0x203,1); CHECK(servo_refreshes==1 && att_calls==0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1); att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2);
    CHECK(servo_refreshes==1 && att_history[1].status==0);
    servo_refresh_result=BLE_STATUS_FAILED; NUS_PermitRead(1,0x203,1); att_advance_to_attempt(3);
    CHECK(servo_refreshes==2 && att_history[2].status==1 && att_history[2].error==NUS_ALL_CHANNEL_DC_ATT_ERROR);
}
static void att_stale_ticket_reconnect(void) {
    start(); BLE_TX_AttTicket old=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&old,0,0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1);
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1);
    uint8_t fresh[]={7}; BLE_TX_AttTicket current_ticket=att_begin(1,0x202,1,fresh,1);
    BLE_TX_AttCommit(&old,1,0x80); step(); CHECK(att_calls==1);
    BLE_TX_AttCommit(&current_ticket,0,0); att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2);
    CHECK(att_history[1].write==1 && att_history[1].data[0]==7 && att_history[1].status==0 && !terminations);
}
static void att_two_links(void) {
    start(); BLE_TX_Connected(2); BLE_TX_AttTicket first=att_begin(1,0x201,0,NULL,0), second=att_begin(2,0x202,0,NULL,0);
    BLE_TX_AttCommit(&first,0,0); BLE_TX_AttCommit(&second,1,0x80); att_result=BLE_STATUS_INSUFFICIENT_RESOURCES;
    att_advance_to_attempt(1); att_advance_to_attempt(2); CHECK(att_history[0].connection!=att_history[1].connection);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(3); att_advance_to_attempt(4);
    CHECK(att_successes==2 && att_history[2].connection!=att_history[3].connection && !terminations);
}
static void att_fatal_status(tBleStatus result) {
    start(); BLE_TX_Connected(2); cccd(2,BLE_TX_REPLY,1); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0);
    BLE_TX_AttCommit(&ticket,0,0); att_result=result; att_advance_to_attempt(1);
    for(unsigned i=0;i<5 && !terminations;++i) step(); CHECK(terminations==1 && terminated==1 && att_calls==1);
    BLE_TX_Item value=item(2,BLE_TX_REPLY,"other link"); CHECK(BLE_TX_Enqueue(&value,0)==osOK);
    for(unsigned i=0;i<5 && !calls;++i) step(); CHECK(calls==1 && sent[0].connection==2); payload(0,"other link");
    CHECK(!BLE_TX_AttBegin(1,0x201,0,NULL,0,&ticket));
}
static void att_fatal_invalid(void) { att_fatal_status(BLE_STATUS_INVALID_PARAMS); }
static void att_ambiguous_busy(void) { att_fatal_status(BLE_STATUS_BUSY); }
static void att_ambiguous_error(void) { att_fatal_status(BLE_STATUS_ERROR); }
static void att_ambiguous_security(void) { att_fatal_status(BLE_STATUS_SEC_PERMISSION_ERROR); }
static void att_deadline_rollover(void) {
    test_now=UINT32_MAX-20U; start(); uint32_t began=test_now; BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0);
    BLE_TX_AttCommit(&ticket,0,0); att_result=BLE_STATUS_INSUFFICIENT_RESOURCES;
    for(unsigned i=0;i<256;++i) { test_now=began+i; step(); CHECK(!terminations); }
    test_now=began+256; for(unsigned i=0;i<3 && !terminations;++i) step();
    CHECK(terminations==1 && terminated==1 && att_calls>1 && att_calls<=38);
    unsigned attempts=att_calls; BLE_TX_AttCommit(&ticket,0,0); step(); CHECK(att_calls==attempts);
}
static void att_static_bounds(void) {
    start(); BLE_TX_AttTicket ticket; uint8_t data[256]={0};
    CHECK(!BLE_TX_AttBegin(1,0x201,0,NULL,0,NULL)); CHECK(!BLE_TX_AttBegin(9,0x201,0,NULL,0,&ticket));
    CHECK(!BLE_TX_AttBegin(1,0x201,2,NULL,0,&ticket)); CHECK(!BLE_TX_AttBegin(1,0x201,0,data,1,&ticket));
    CHECK(!BLE_TX_AttBegin(1,0x202,1,NULL,1,&ticket)); CHECK(!BLE_TX_AttBegin(1,0x202,1,data,256,&ticket));
    BLE_TX_AttCommit(NULL,0,0);
    for(unsigned i=1;i<=CFG_BLE_NUM_LINK;++i) { BLE_TX_Connected((uint16_t)i); CHECK(BLE_TX_AttBegin((uint16_t)i,0x202,1,data,1,&ticket)); BLE_TX_AttCommit(&ticket,0,0); }
    BLE_TX_Connected((uint16_t)(CFG_BLE_NUM_LINK+1)); CHECK(!BLE_TX_AttBegin((uint16_t)(CFG_BLE_NUM_LINK+1),0x201,0,NULL,0,&ticket));
    for(unsigned i=0;i<CFG_BLE_NUM_LINK;++i) att_advance_to_attempt(i+1); CHECK(att_successes==CFG_BLE_NUM_LINK);
}
static void att_disconnect_during_call(void) {
    start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_inject_disconnect=1; att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1);
    BLE_TX_EventsDrained(); BLE_TX_Connected(1); cccd(1,BLE_TX_REPLY,1); ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2); CHECK(att_successes==1 && !terminations);
}

static void att_retry_due_zero(void) {
    test_now=UINT32_MAX-6U; start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; att_advance_to_attempt(1);
    uint32_t wait=step(); CHECK(wait==7 && att_calls==1); /* next tick legitimately wraps to zero. */
    test_now=0; att_result=BLE_STATUS_SUCCESS; att_advance_to_attempt(2); CHECK(att_successes==1);
}
static void att_reserved_deadline_zero(void) {
    test_now=UINT32_MAX-255U; start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0);
    CHECK(step()==256 && att_calls==0 && !terminations); /* Never publish an uncommitted decision. */
    BLE_TX_AttCommit(&ticket,1,0x80); att_advance_to_attempt(1); CHECK(att_history[0].status==1);
}
static void att_reentrant_request(void) {
    const uint8_t values[32]={7}; NUS_PermitWrite(1,0x203,values,sizeof(values),1);
}
static void att_reentrant_next_request(void) {
    start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    /* Controller has accepted read permission, dispatches the peer's next valid
     * ATT request before the owner's previous controller call returns. */
    att_inject_new_request=1; att_advance_to_attempt(1);
    CHECK(servo_mutations==1 && !terminations); att_advance_to_attempt(2);
    CHECK(att_successes==2 && att_history[1].write && att_history[1].data[0]==7);
}
static void att_fairness_under_pool_wakes(void) {
    start(); BLE_TX_Connected(2); BLE_TX_AttTicket first=att_begin(1,0x201,0,NULL,0), second=att_begin(2,0x202,0,NULL,0);
    BLE_TX_AttCommit(&first,0,0); BLE_TX_AttCommit(&second,0,0); att_result=BLE_STATUS_INSUFFICIENT_RESOURCES;
    att_advance_to_attempt(1); uint16_t pool[2]={0xffff,1}; unsigned saw_second=0;
    for(unsigned i=0;i<2*CFG_BLE_NUM_LINK && !saw_second;++i) {
        event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,4); step();
        for(unsigned n=0;n<att_calls;++n) if(att_history[n].connection==2) saw_second=1;
    }
    CHECK(saw_second); /* Repeated wake of index zero cannot starve another ready bearer. */
}

static void att_reentrant_contradiction(tBleStatus result) {
    start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    att_result=result; att_inject_new_request=1; att_advance_to_attempt(1);
    CHECK(servo_mutations==1); /* New request was admitted before contradictory old result arrived. */
    for(unsigned i=0;i<4 && !terminations;++i) step();
    CHECK(terminations==1 && terminated==1 && att_calls==1 && !att_successes);
    BLE_TX_AttCommit(&ticket,0,0); step(); CHECK(att_calls==1 && servo_mutations==1);
}
static void att_reentrant_old_resource_failure(void) { att_reentrant_contradiction(BLE_STATUS_INSUFFICIENT_RESOURCES); }
static void att_reentrant_old_pending(void) { att_reentrant_contradiction(BLE_STATUS_PENDING); }
static void att_collision_blocks_side_effects(void) {
    start(); uint8_t values[32]={1}; NUS_PermitWrite(1,0x203,values,sizeof(values),1);
    CHECK(servo_mutations==1 && !att_calls); NUS_PermitWrite(1,0x203,values,sizeof(values),1);
    CHECK(servo_mutations==1 && servo_write_calls==1);
    for(unsigned i=0;i<3 && !terminations;++i) step(); CHECK(terminations==1 && !att_calls);
}
static void att_all_links_wake_fairness(void) {
    start(); BLE_TX_AttTicket ticket;
    for(unsigned i=1;i<=CFG_BLE_NUM_LINK;++i) {
        BLE_TX_Connected((uint16_t)i); ticket=att_begin((uint16_t)i,(uint16_t)(0x200+i),0,NULL,0); BLE_TX_AttCommit(&ticket,0,0);
    }
    att_result=BLE_STATUS_INSUFFICIENT_RESOURCES; uint16_t pool[2]={0xffff,1}; uint32_t seen=0;
    for(unsigned i=0;i<CFG_BLE_NUM_LINK;++i) {
        event(ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE,pool,4); step(); CHECK(att_calls==i+1);
        seen|=1U<<(att_history[i].connection-1U);
    }
    CHECK(seen==((1U<<CFG_BLE_NUM_LINK)-1U));
}
static void att_uncommitted_absolute_timeout(void) {
    start(); BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); CHECK(step()==256 && att_calls==0);
    test_now=256; for(unsigned i=0;i<3 && !terminations;++i) step(); CHECK(terminations==1 && att_calls==0);
    BLE_TX_AttCommit(&ticket,0,0); step(); CHECK(att_calls==0);
}

static void att_fatal_keeps_accepted_flight(void) {
    start(); BLE_TX_Connected(2); cccd(2,BLE_TX_REPLY,1); enqueue("first"); enqueue("old queued"); step();
    CHECK(calls==1); BLE_TX_Item other=item(2,BLE_TX_REPLY,"other"); CHECK(BLE_TX_Enqueue(&other,0)==osOK);
    BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0); att_result=BLE_STATUS_ERROR;
    att_advance_to_attempt(1); for(unsigned i=0;i<3 && !terminations;++i) step();
    CHECK(terminations==1 && calls==1 && stats.completed==0);
    complete(0); step(); CHECK(stats.completed==1 && calls==2 && sent[1].connection==2); payload(1,"other");
    BLE_TX_Disconnected(1); BLE_TX_EventsDrained(); CHECK(step()>0 && calls==2); complete(1); step(); CHECK(stats.completed==2);
}
static void att_fatal_disconnect_without_completion(void) {
    start(); BLE_TX_Connected(2); cccd(2,BLE_TX_REPLY,1); enqueue("first"); step(); CHECK(calls==1);
    BLE_TX_Item other=item(2,BLE_TX_REPLY,"other"); CHECK(BLE_TX_Enqueue(&other,0)==osOK);
    BLE_TX_AttTicket ticket=att_begin(1,0x201,0,NULL,0); BLE_TX_AttCommit(&ticket,0,0); att_result=BLE_STATUS_ERROR;
    att_advance_to_attempt(1); for(unsigned i=0;i<3 && !terminations;++i) step(); CHECK(terminations==1 && calls==1);
    BLE_TX_Disconnected(1); CHECK(step()==osWaitForever && calls==1); BLE_TX_EventsDrained(); step();
    CHECK(calls==2 && sent[1].connection==2); payload(1,"other");
}
