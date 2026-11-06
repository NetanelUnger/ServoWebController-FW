/* Fixed flash cache sharing the OTA download slot. No framebuffer. */
#include "image_store.h"
#include "display.h"
#include "app_freertos.h"
#include "app_conf.h"
#include "flash_manager.h"
#include "config.h"
#include <string.h>
#define CATALOG_MAGIC 0x31474D49UL
#define SLOT_MAGIC 0x31544C53UL
#define SLOT_SIZE (96U*1024U)
#define PAYLOAD_OFFSET 32U
#define NO_CONNECTION 0xFFFFU
_Static_assert(FLASH_PAGE_SIZE == 8192U, "Flash layout changed");
_Static_assert(CFG_ACTIVE_SLOT_START_SECTOR_INDEX == 3 && CFG_USER_DATA_SECTORS_NB == 1, "OTA layout changed");
_Static_assert(IMAGE_STORE_BASE+8192U+5U*SLOT_SIZE == IMAGE_STORE_END, "Image layout");
/* One in-flight job. Critical sections only cover RAM state, never Flash/SPI. */
static uint8_t request[232], result[240];
static uint16_t request_size, result_size=16, owner=NO_CONNECTION;
static volatile bool pending, disconnected;
static bool initialized, catalog, empty, ota, uploading;
static uint8_t valid_mask, upload_id;
static uint32_t received, expected_crc;
static uint32_t aligned_data[60];
static volatile bool fm_done;
static volatile FM_FlashOp_Status_t fm_status;
static uint32_t enter(void) { uint32_t mask=__get_PRIMASK(); __disable_irq(); return mask; }
static void leave(uint32_t mask) { __set_PRIMASK(mask); }
static uint16_t get16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static uint32_t get32(const uint8_t *p) { return get16(p)|((uint32_t)get16(p+2)<<16); }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t slot(uint8_t id) { return IMAGE_STORE_BASE+8192U+(id-1U)*SLOT_SIZE; }
static uint32_t crc32(const uint8_t *p,uint32_t n) {
    uint32_t crc=0xFFFFFFFFU;
    while(n--) { crc^=*p++; for(unsigned b=0;b<8;b++) crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320U); }
    return ~crc;
}
/* Integer geometry exactly matches visibleRow in the Web (pixel-square intersection). */
uint16_t ImageStore_RowLeft(uint16_t y) {
    uint16_t dy=y<120U?119U-y:y-120U, x=0;
    while(x<120U && (uint32_t)(119U-x)*(119U-x)+(uint32_t)dy*dy>=14400U) ++x;
    return x;
}
void ImageStore_Init(void) {
    if(initialized) return;
    const uint32_t *head=(const uint32_t *)IMAGE_STORE_BASE;
    catalog=head[0]==CATALOG_MAGIC && head[1]==1 && head[2]==IMAGE_STORE_BYTES && head[3]==5 && head[4]==0xFFFFFFFFU && head[5]==0xFFFFFFFFU && head[6]==0xFFFFFFFFU && head[7]==0xFFFFFFFFU && head[0x160/4]==0xFFFFFFFFU;
    empty=true;
    if(!catalog) for(uint32_t a=IMAGE_STORE_BASE;a<IMAGE_STORE_END;a+=4) if(*(const uint32_t *)a!=0xFFFFFFFFU) {empty=false;break;}
    if(catalog) for(uint8_t id=1;id<=5;id++) {
        const uint32_t *h=(const uint32_t *)slot(id);
        if(h[0]==SLOT_MAGIC && h[1]==IMAGE_STORE_BYTES && h[3]==1 && h[2]==crc32((const uint8_t *)(slot(id)+PAYLOAD_OFFSET),IMAGE_STORE_BYTES)) valid_mask|=1U<<(id-1);
    }
    result[0]=1; result[1]=2; result[7]=valid_mask;
    initialized=true;
}
static void flash_callback(FM_FlashOp_Status_t status) { fm_status=status; fm_done=true; }
static FM_CallbackNode_t callback={.NodeList={NULL,NULL},.Callback=flash_callback};
/* FM_BUSY queues this callback: wait for it before retrying, never enqueue twice.
 * Do not abandon accepted operations on disconnect or reuse their source buffer. */
static bool flash_op(uint32_t address,uint32_t count,bool erase) {
    for(;;) {
        fm_done=false;
        FM_Cmd_Status_t s=erase?FM_Erase(address,count,&callback):FM_Write(aligned_data,(uint32_t *)address,count/4U,&callback);
        if(s!=FM_OK && s!=FM_BUSY) return false;
        while(!fm_done) osDelay(1);
        if(s==FM_OK) return fm_status==FM_OPERATION_COMPLETE;
    }
}
static bool write16(uint32_t address,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    aligned_data[0]=a;aligned_data[1]=b;aligned_data[2]=c;aligned_data[3]=d;
    return flash_op(address,16,false);
}
bool ImageStore_ClaimOTA(void) {
    ImageStore_Init();
    uint32_t key=enter();
    if(pending || uploading) {leave(key);return false;}
    ota=true; pending=true; owner=NO_CONNECTION; leave(key);
    bool ok=true;
    const uint32_t *marker=(const uint32_t *)(IMAGE_STORE_BASE+16);
    if(catalog && marker[0]==0xFFFFFFFFU && marker[1]==0xFFFFFFFFU && marker[2]==0xFFFFFFFFU && marker[3]==0xFFFFFFFFU) ok=write16(IMAGE_STORE_BASE+16,0,0,0,0); /* durable invalidation BEFORE OTA erases/writes */
    key=enter();if(ok) catalog=false;valid_mask=0;pending=false;leave(key);
    return ok;
}
void ImageStore_Disconnect(uint16_t connection) {
    uint32_t key=enter();
    if(owner==connection) { disconnected=true; if(!pending) {uploading=false;owner=NO_CONNECTION;} }
    leave(key);
}
uint8_t ImageStore_Submit(uint16_t connection,const uint8_t *p,uint16_t n,bool data) {
#if !ENABLE_DISPLAY
    return 0x83;
#endif
    if(!initialized || displayTaskHandle==NULL) return 0x81;
    if((!data && (n<4 || n>12)) || (data && (n<12 || n>228))) return 0x80;
    uint8_t op=data?8:p[0],id=data?upload_id:p[1];
    uint16_t seq=data?get16(p):get16(p+2);
    if(!seq || op<1 || op>8 || (!data && op==8) || (op!=6 && op!=7 && (id<1 || id>5))) return 0x80;
    if(!data && n!=(op==1?12:op==5?8:4)) return 0x80;
    uint32_t key=enter();
    if(ota || (!catalog && !empty)) {leave(key);return 0x84;}
    if(owner!=NO_CONNECTION && owner!=connection) {leave(key);return 0x81;}
    if(pending) {leave(key);return 0x81;}
    if(uploading && op!=8 && op!=2 && op!=7 && op!=6) {leave(key);return 0x81;}
    if((op==8 || op==2) && (!uploading || (op==2 && id!=upload_id))) {leave(key);return 0x85;}
    owner=connection;disconnected=false;
    if(data) {
        request[0]=8;request[1]=id;request[2]=p[0];request[3]=p[1];
        put32(request+4,(uint32_t)get16(p+2)*16U);memcpy(request+8,p+4,n-4);request_size=n+4;
    } else {memcpy(request,p,n);request_size=n;}
    memset(result,0,16);result[0]=1;result[1]=1;result[2]=op;result[3]=id;result[4]=(uint8_t)seq;result[5]=seq>>8;result[7]=valid_mask;result_size=16;
    pending=true;leave(key);
    osThreadFlagsSet(displayTaskHandle,1U);
    return 0;
}
uint16_t ImageStore_Status(uint8_t *out) {
    uint32_t key=enter();uint16_t n=result_size;memcpy(out,result,n);leave(key);return n;
}
void ImageStore_Process(void) {
    if(!pending || ota) return;
    uint8_t op=request[0], id=request[1], error=0;
    uint32_t offset=0;uint16_t bytes=0;
    uint8_t read_data[224];
    if(op==1) {
        if(get32(request+4)!=IMAGE_STORE_BYTES) error=0x80;
        else {
            if(!catalog) {
                empty=false;
                if(!write16(IMAGE_STORE_BASE,CATALOG_MAGIC,1,IMAGE_STORE_BYTES,5)) {error=0x82;ota=true;}
                else {catalog=true;empty=false;}
            }
            if(!error) {
                valid_mask&=~(1U<<(id-1));
                if(!flash_op((slot(id)-FLASH_BASE)/FLASH_PAGE_SIZE,12,true)) error=0x82;
                else {uploading=true;upload_id=id;received=0;expected_crc=get32(request+8);}
            }
        }
    } else if(op==8) {
        offset=get32(request+4);bytes=request_size-8;
        if(offset!=received || !bytes || offset+bytes>IMAGE_STORE_BYTES || (bytes%16U && offset+bytes!=IMAGE_STORE_BYTES)) error=0x85;
        else {
            memset(aligned_data,0xFF,sizeof(aligned_data));memcpy(aligned_data,request+8,bytes);
            if(!flash_op(slot(id)+PAYLOAD_OFFSET+offset,(bytes+15U)&~15U,false)) {error=0x82;uploading=false;}
            else received+=bytes;
        }
        bytes=0;
    } else if(op==2) {
        if(received!=IMAGE_STORE_BYTES || crc32((const uint8_t *)(slot(id)+PAYLOAD_OFFSET),IMAGE_STORE_BYTES)!=expected_crc) error=0x86;
        else if(!write16(slot(id),SLOT_MAGIC,IMAGE_STORE_BYTES,expected_crc,1)) error=0x82;
        else valid_mask|=1U<<(id-1);
        uploading=false;
    } else if(op==7) {uploading=false;
    } else if(op==6) { /* LIST: mask and owner-independent completion */
    } else if(!(valid_mask&(1U<<(id-1)))) error=0x87;
    else if(op==3) error=Display_ShowStored((const uint8_t *)(slot(id)+PAYLOAD_OFFSET));
    else if(op==4) {
        valid_mask&=~(1U<<(id-1));
        if(!flash_op((slot(id)-FLASH_BASE)/FLASH_PAGE_SIZE,12,true)) error=0x82;
    } else if(op==5) {
        offset=get32(request+4);
        if(offset>=IMAGE_STORE_BYTES) error=0x80;
        else {bytes=(uint16_t)((IMAGE_STORE_BYTES-offset)>224?224:IMAGE_STORE_BYTES-offset);memcpy(read_data,(const void *)(slot(id)+PAYLOAD_OFFSET+offset),bytes);}
    }
    uint32_t key=enter();
    if(disconnected) {uploading=false;error=0x88;}
    result[1]=error?3:2;result[6]=error;result[7]=valid_mask;
    put32(result+8,op==5?offset:received);
    put32(result+12,(id>=1 && id<=5 && (valid_mask&(1U<<(id-1))))?((const uint32_t *)slot(id))[2]:0);
    result_size=16;
    if(op==5 && !error) {memcpy(result+16,read_data,bytes);result_size+=bytes;}
    if(!uploading) owner=NO_CONNECTION;
    pending=false;leave(key);
}
