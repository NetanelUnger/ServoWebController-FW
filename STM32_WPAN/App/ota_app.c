/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    ota_app.c
  * @author  MCD Application Team
  * @brief   ota_app application definition.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Project OTA v2: callbacks copy bounded requests; defaultTask owns Flash work. */
#include "main.h"
#include "app_common.h"
#include "ota_app.h"
#include "config.h"
#include "app_freertos.h"
#include "flash_manager.h"
#include "display/image_store.h"
#include "ota_image.h"
#include <stdbool.h>
#include <string.h>

#define NO_OWNER 0xffffU
#define OP_BEGIN 0x10U
#define OP_VERIFY 0x11U
#define OP_INSTALL 0x12U
#define OP_ABORT 0x13U
#define OP_DATA 0x14U
/* States: idle, busy, ready, receiving, verified, installing, error, aborted. */
enum { IDLE, BUSY, READY, RECEIVING, VERIFIED, INSTALLING, OTA_ERROR, ABORTED };
enum { INVALID=0x80, OCCUPIED, IO_ERROR, BAD_STATE, OFFSET_ERROR, CRC_ERROR, IMAGE_ERROR, LOST_CONNECTION };
static uint8_t request[228], status_data[20];
static uint16_t request_size, owner=NO_OWNER;
static uint8_t operation, state;
static uint32_t image_size, image_crc, received, session;
static volatile bool pending, disconnected;
static uint32_t aligned_data[56];
static volatile bool flash_done;
static volatile FM_FlashOp_Status_t flash_status;
static uint32_t enter(void) {uint32_t k=__get_PRIMASK();__disable_irq();return k;}
static void leave(uint32_t k) {__set_PRIMASK(k);}
static void flash_callback(FM_FlashOp_Status_t s) {flash_status=s;flash_done=true;}
static FM_CallbackNode_t callback={.NodeList={NULL,NULL},.Callback=flash_callback};

/* FM_BUSY retains the callback: it must finish before the node/buffer is reused.
 * Never retry a failed program (ECC quadwords cannot safely be programmed twice). */
static bool flash_op(uint32_t address,uint32_t count,bool erase) {
    for(unsigned attempts=0;attempts<8;attempts++) {
        flash_done=false;
        FM_Cmd_Status_t r=erase?FM_Erase(address,count,&callback):
            FM_Write(aligned_data,(uint32_t *)address,count/4U,&callback);
        if(r!=FM_OK && r!=FM_BUSY) return false;
        while(!flash_done) {WatchdogFlagClear(WATCHDOG_FLAG_DEFAULT);osDelay(1);}
        if(r==FM_OK) return flash_status==FM_OPERATION_COMPLETE;
    }
    return false;
}
static void publish(uint8_t next,uint8_t error) {
    uint32_t k=enter();
    state=next;status_data[0]=OTA_PROTOCOL_VERSION;status_data[1]=next;
    status_data[2]=operation;status_data[3]=error;
    OTA_Put32(status_data+4,session);OTA_Put32(status_data+8,received);
    OTA_Put32(status_data+12,image_size);OTA_Put32(status_data+16,image_crc);
    pending=false;leave(k);
}
uint16_t OTA_APP_Status(uint8_t *out) {
    uint32_t k=enter();memcpy(out,status_data,20);leave(k);return 20;
}
uint8_t OTA_APP_Submit(uint16_t connection,const uint8_t *p,uint16_t n,bool data) {
    if(!p || (data?(n<20 || n>228 || ((n-4U)&15U)):(n!=20 && n!=8))) return INVALID;
    uint8_t op=data?OP_DATA:p[0];
    if(!data && (p[1]!=OTA_PROTOCOL_VERSION || (op==OP_BEGIN?n!=20:n!=8) ||
       (op!=OP_BEGIN && op!=OP_VERIFY && op!=OP_INSTALL && op!=OP_ABORT))) return INVALID;
    if(op==OP_BEGIN && (p[2]!=1 || p[3]!=0 || OTA_Get32(p+16)!=OTA_TARGET_ID ||
       !OTA_Get32(p+12) || OTA_Get32(p+4)<OTA_TAG_OFFSET+4 ||
       OTA_Get32(p+4)>OTA_MAX_IMAGE_SIZE || (OTA_Get32(p+4)&15U))) return INVALID;
    if(!data && op!=OP_BEGIN && (p[2] || p[3])) return INVALID;
    uint32_t k=enter();
    if(pending || (owner!=NO_OWNER && owner!=connection)) {leave(k);return OCCUPIED;}
    if(op==OP_BEGIN) {
        if(state==READY || state==RECEIVING || state==VERIFIED || state==INSTALLING) {leave(k);return BAD_STATE;}
    } else if(owner!=connection || (!data && OTA_Get32(p+4)!=session)) {leave(k);return BAD_STATE;}
    if(data && (state!=READY && state!=RECEIVING)) {leave(k);return BAD_STATE;}
    if(data && (OTA_Get32(p)!=received || n-4U>image_size-received)) {leave(k);return OFFSET_ERROR;}
    if(op==OP_VERIFY && ((state!=READY && state!=RECEIVING) || received!=image_size)) {leave(k);return BAD_STATE;}
    if(op==OP_INSTALL && state!=VERIFIED) {leave(k);return BAD_STATE;}
    if(op==OP_ABORT && state==INSTALLING) {leave(k);return BAD_STATE;}
    if(op==OP_BEGIN) {
        image_size=OTA_Get32(p+4);image_crc=OTA_Get32(p+8);session=OTA_Get32(p+12);received=0;
    }
    memcpy(request,p,n);request_size=n;operation=op;owner=connection;disconnected=false;
    status_data[1]=BUSY;status_data[2]=op;status_data[3]=0;
    OTA_Put32(status_data+4,session);OTA_Put32(status_data+8,received);
    OTA_Put32(status_data+12,image_size);OTA_Put32(status_data+16,image_crc);
    pending=true;leave(k);
    osThreadFlagsSet(defaultTaskHandle,1U);return 0;
}
void OTA_APP_Process(void) {
    if(!pending) {
        if(disconnected && owner!=NO_OWNER) {owner=NO_OWNER;publish(ABORTED,LOST_CONNECTION);}
        return;
    }
    uint8_t error=0,next=state;
    if(disconnected && operation!=OP_INSTALL) {owner=NO_OWNER;publish(ABORTED,LOST_CONNECTION);return;}
    if(operation==OP_BEGIN) {
        /* One claim invalidates BOTH image catalogs before any sector erase. */
        if(!ImageStore_ClaimOTA()) error=OCCUPIED;
        else {
            /* Invalidate an old commit before touching candidate bytes. */
            if(!flash_op(124U,1,true)) error=IO_ERROR;
            for(uint32_t page=64U;!error && page<124U;page++) {
                if(disconnected) {error=LOST_CONNECTION;break;}
                if(!flash_op(page,1,true)) error=IO_ERROR;
            }
        }
        next=READY;
    } else if(operation==OP_DATA) {
        uint32_t n=request_size-4U;
        memcpy(aligned_data,request+4,n);
        if(!flash_op(OTA_DOWNLOAD_ADDRESS+received,n,false) ||
           memcmp((const void *)(OTA_DOWNLOAD_ADDRESS+received),request+4,n)) error=IO_ERROR;
        else received+=n;
        next=RECEIVING;
    } else if(operation==OP_VERIFY) {
        if(!OTA_ImageValid((const uint8_t *)OTA_DOWNLOAD_ADDRESS,image_size)) error=IMAGE_ERROR;
        else if(OTA_Crc32((const uint8_t *)OTA_DOWNLOAD_ADDRESS,image_size)!=image_crc) error=CRC_ERROR;
        next=VERIFIED;
    } else if(operation==OP_INSTALL) {
        /* Durable commit is the ONLY bootloader installation trigger. */
        aligned_data[0]=OTA_COMMIT_MAGIC;aligned_data[1]=image_size;
        aligned_data[2]=image_crc;aligned_data[3]=~image_crc;
        if(!flash_op(OTA_COMMIT_ADDRESS,16,false) ||
           !OTA_CommitValid((const uint8_t *)OTA_DOWNLOAD_ADDRESS,(const uint8_t *)OTA_COMMIT_ADDRESS)) error=IO_ERROR;
        else {
            publish(INSTALLING,0);
            osDelay(500); /* allow the client to observe the durable acknowledgement */
            NVIC_SystemReset();return;
        }
    } else if(operation==OP_ABORT) {next=ABORTED;owner=NO_OWNER;}
    if(disconnected) {error=LOST_CONNECTION;owner=NO_OWNER;}
    publish(error?OTA_ERROR:next,error);
}
void OTA_APP_EvtRx(OTA_APP_ConnHandleNotEvt_t *event) {
    if(event->EvtOpcode==OTA_DISCON_HANDLE_EVT && event->ConnectionHandle==owner) {
        disconnected=true;osThreadFlagsSet(defaultTaskHandle,1U);
    }
}
void OTA_APP_Init(void) {
    owner=NO_OWNER;pending=false;disconnected=false;operation=0;
    session=received=image_size=image_crc=0;publish(IDLE,0);OTA_Init();
}
/* Retained ABI for the central TX dispatcher; v2 uses durable status reads. */
uint8_t OTA_APP_GetConfStatus(void) {return 2;}
void OTA_Notification(OTA_NotificationEvt_t *event) {(void)event;}
