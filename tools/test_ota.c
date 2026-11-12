#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include "ota_image.h"

typedef int FM_FlashOp_Status_t;
typedef int FM_Cmd_Status_t;
typedef struct {struct {void *next,*prev;} NodeList;void (*Callback)(int);} FM_CallbackNode_t;
typedef struct {int EvtOpcode;uint16_t ConnectionHandle;} OTA_APP_ConnHandleNotEvt_t;
typedef struct {int unused;} OTA_NotificationEvt_t;
#define OTA_DISCON_HANDLE_EVT 1
#define FM_OK 0
#define FM_BUSY 1
#define FM_OPERATION_COMPLETE 2
#define WATCHDOG_FLAG_DEFAULT 1
static int defaultTaskHandle, erase_calls, write_calls, init_calls, reset_calls;
static int claim_ok=1, driver_fail, fm_busy, cut_at=-1, checkpoints, jumped;
static jmp_buf power_cut;
static void checkpoint(void) {if(checkpoints++==cut_at) longjmp(power_cut,1);}
static uint32_t __get_PRIMASK(void) {return 0;}
static void __disable_irq(void) {}
static void __set_PRIMASK(uint32_t k) {(void)k;}
static void WatchdogFlagClear(uint32_t f) {(void)f;}
static void osDelay(unsigned ms) {(void)ms;}
static void osThreadFlagsSet(int task,unsigned flags) {(void)task;(void)flags;}
static void NVIC_SystemReset(void) {reset_calls++;}
static void OTA_Init(void) {init_calls++;}
static bool ImageStore_ClaimOTA(void) {return claim_ok!=0;}
static void erase_page(uint32_t page) {
    assert(page>=3 && page<=124);
    uint8_t *p=(uint8_t *)(uintptr_t)(0x08000000U+page*8192U);
    memset(p,255,4096);checkpoint();memset(p+4096,255,4096);checkpoint();
}
static void program(uint32_t address,const uint8_t *data,uint32_t n) {
    assert(address>=OTA_ACTIVE_ADDRESS && address+n<=OTA_COMMIT_ADDRESS+16U && n%16==0);
    uint8_t *p=(uint8_t *)(uintptr_t)address;
    for(uint32_t i=0;i<n;i+=8) {
        for(unsigned b=0;b<8;b++) {assert(p[i+b]==255);p[i+b]=data[i+b];}
        checkpoint();
    }
}
static FM_Cmd_Status_t FM_Erase(uint32_t page,uint32_t n,FM_CallbackNode_t *cb) {
    if(driver_fail) return 9;
    if(fm_busy) {fm_busy--;cb->Callback(0);return FM_BUSY;}
    erase_calls++;for(uint32_t i=0;i<n;i++) erase_page(page+i);cb->Callback(FM_OPERATION_COMPLETE);return FM_OK;
}
static FM_Cmd_Status_t FM_Write(uint32_t *src,uint32_t *dst,uint32_t words,FM_CallbackNode_t *cb) {
    if(driver_fail) return 9;
    write_calls++;program((uint32_t)(uintptr_t)dst,(uint8_t *)src,words*4);cb->Callback(FM_OPERATION_COMPLETE);return FM_OK;
}
#include "receiver.inc"

#define CFG_ACTIVE_SLOT_START_SECTOR_INDEX 3U
#define DOWNLOAD_SLOT_START_SECTOR_INDEX 64U
#define APP_SLOT_PAGE_SIZE 61U
#define FLASH_PAGE_NB 128U
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_TYPEPROGRAM_QUADWORD 2
#define FLASH_FLAG_ALL_ERRORS 0
#define __HAL_FLASH_CLEAR_FLAG(x) ((void)(x))
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
typedef struct {uint32_t TypeErase,NbPages,Page;} FLASH_EraseInitTypeDef;
static int HAL_FLASH_Unlock(void) {return 0;}
static void HAL_FLASH_Lock(void) {}
static int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *error) {
    *error=0;if(driver_fail) return 1;assert(e->NbPages==1);erase_page(e->Page);return 0;
}
static int HAL_FLASH_Program(int type,uint32_t to,uint32_t from) {
    (void)type;if(driver_fail) return 1;program(to,(const uint8_t *)(uintptr_t)from,16);return 0;
}
static uint8_t CheckFwAppValidity(uint8_t p);
static uint8_t DeleteSlot(uint8_t p);
static uint8_t MoveToActiveSlot(uint8_t p);
static void JumpFwApp(void) {assert(CheckFwAppValidity(3));jumped++;}
#include "installer.inc"

static uint8_t image[1024],old_image[1024];
static uint8_t *flash=(uint8_t *)0x08000000;
static void make_image(uint8_t *p,uint8_t fill) {
    memset(p,fill,1024);OTA_Put32(p,0x20020000);OTA_Put32(p+4,OTA_ACTIVE_ADDRESS+0x181);
    OTA_Put32(p+0x160,OTA_ACTIVE_ADDRESS+1020);OTA_Put32(p+1020,OTA_TAG_MAGIC);
}
static void setup(void) {
    memset(flash,255,0x100000);memcpy((void *)OTA_ACTIVE_ADDRESS,old_image,sizeof(old_image));
    claim_ok=1;driver_fail=fm_busy=0;cut_at=-1;checkpoints=jumped=0;
    erase_calls=write_calls=init_calls=reset_calls=0;OTA_APP_Init();
}
static void begin(uint32_t crc) {
    uint8_t p[20]={0x10,2,1,0};OTA_Put32(p+4,sizeof(image));OTA_Put32(p+8,crc);
    OTA_Put32(p+12,42);OTA_Put32(p+16,OTA_TARGET_ID);
    assert(OTA_APP_Submit(7,p,20,false)==0);OTA_APP_Process();
}
static void control(uint8_t op) {
    uint8_t p[8]={op,2};OTA_Put32(p+4,42);assert(OTA_APP_Submit(7,p,8,false)==0);OTA_APP_Process();
}
static void send_image(void) {
    for(uint32_t i=0;i<sizeof(image);i+=16) {
        uint8_t p[20];OTA_Put32(p,i);memcpy(p+4,image+i,16);
        assert(OTA_APP_Submit(7,p,20,true)==0);
        assert(OTA_APP_Submit(7,p,20,true)==OCCUPIED);OTA_APP_Process();
        assert(OTA_APP_Submit(7,p,20,true)==OFFSET_ERROR);
        assert(received==i+16);
    }
}
static void committed_candidate(void) {
    memcpy((void *)OTA_DOWNLOAD_ADDRESS,image,sizeof(image));
    uint32_t *r=(uint32_t *)OTA_COMMIT_ADDRESS;r[0]=OTA_COMMIT_MAGIC;r[1]=sizeof(image);
    r[2]=OTA_Crc32(image,sizeof(image));r[3]=~r[2];
}
int main(void) {
    assert(VirtualAlloc(flash,0x100000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)==flash);
    make_image(image,0x55);make_image(old_image,0xaa);
    setup();assert(init_calls==1 && erase_calls==0);assert(OTA_ImageValid(image,sizeof(image)));
    for(unsigned pidx=0;pidx<256;pidx++) if(pidx!=3 && pidx!=64) assert(!CheckFwAppValidity((uint8_t)pidx));
    uint8_t p[20]={0x10,2,1,0};assert(OTA_APP_Submit(7,p,20,false)==INVALID);assert(!erase_calls);
    claim_ok=0;begin(OTA_Crc32(image,sizeof(image)));assert(state==OTA_ERROR && !erase_calls);
    setup();fm_busy=2;begin(OTA_Crc32(image,sizeof(image)));assert(state==READY && erase_calls==61);
    uint8_t other[20]={0};memcpy(other+4,image,16);
    assert(OTA_APP_Submit(8,other,20,true)==OCCUPIED);
    assert(OTA_APP_Submit(7,other,19,true)==INVALID);
    uint8_t early[8]={0x12,2};OTA_Put32(early+4,42);
    assert(OTA_APP_Submit(7,early,8,false)==BAD_STATE);
    early[0]=0x13;OTA_Put32(early+4,43);assert(OTA_APP_Submit(7,early,8,false)==BAD_STATE);
    assert(!CheckFwAppValidity(64));send_image();assert(!CheckFwAppValidity(64));
    control(0x11);assert(state==VERIFIED && !CheckFwAppValidity(64));
    control(0x12);assert(reset_calls==1 && CheckFwAppValidity(64));
    BootModeCheck();assert(jumped==1 && !memcmp((void *)OTA_ACTIVE_ADDRESS,image,sizeof(image)));
    assert(!CheckFwAppValidity(64));puts("PASS receiver: durable offsets, CRC verify, commit and boot installation");
    setup();begin(123);send_image();control(0x11);assert(state==OTA_ERROR && status_data[3]==CRC_ERROR);
    assert(!CheckFwAppValidity(64) && !memcmp((void *)OTA_ACTIVE_ADDRESS,old_image,sizeof(image)));
    setup();begin(OTA_Crc32(image,sizeof(image)));control(0x13);assert(state==ABORTED && !CheckFwAppValidity(64));
    setup();begin(OTA_Crc32(image,sizeof(image)));OTA_APP_ConnHandleNotEvt_t d={OTA_DISCON_HANDLE_EVT,7};
    OTA_APP_EvtRx(&d);OTA_APP_Process();assert(state==ABORTED && owner==NO_OWNER);
    setup();driver_fail=1;begin(0);assert(state==OTA_ERROR && status_data[3]==IO_ERROR);
    puts("PASS receiver: malformed begin, busy storage, CRC mismatch, abort, disconnect, Flash error");
    setup();committed_candidate();((uint8_t *)OTA_DOWNLOAD_ADDRESS)[700]^=1;
    BootModeCheck();assert(jumped==1 && !memcmp((void *)OTA_ACTIVE_ADDRESS,old_image,sizeof(image)));
    setup();committed_candidate();((uint32_t *)OTA_COMMIT_ADDRESS)[3]=0xffffffff;
    BootModeCheck();assert(jumped==1 && !memcmp((void *)OTA_ACTIVE_ADDRESS,old_image,sizeof(image)));
    puts("PASS boot: corrupt image and torn commit never erase the active firmware");
    setup();memset((void *)OTA_ACTIVE_ADDRESS,255,sizeof(image));BootModeCheck();assert(!jumped);
    setup();committed_candidate();driver_fail=1;BootModeCheck();assert(!jumped && CheckFwAppValidity(64));
    driver_fail=0;BootModeCheck();assert(jumped==1);
    /* Cut after each half-page erase and half-quadword program. Restart with all
     * SRAM upload state discarded; only the Flash commit survives. */
    for(int cut=0;cut<372;cut++) {
        setup();committed_candidate();cut_at=cut;
        if(!setjmp(power_cut)) BootModeCheck();
        cut_at=-1;jumped=0;OTA_APP_Init();BootModeCheck();
        assert(jumped==1);assert(!memcmp((void *)OTA_ACTIVE_ADDRESS,image,sizeof(image)));
        assert(!CheckFwAppValidity(64));
    }
    puts("PASS boot: all 372 power-cut boundaries recover the complete new firmware");
    VirtualFree(flash,0,MEM_RELEASE);return 0;
}
