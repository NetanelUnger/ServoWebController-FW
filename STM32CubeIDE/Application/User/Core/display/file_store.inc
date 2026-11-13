#include "../servo/timeline_format.h"
#include "../servo/safe_pose_format.h"
/* Sequential file store v2; included by image_store.c after the v1 adapter.
 * All mutations run in displayTask. See FILE_STORE.md for persistence/wire rules. */
#define FS_MAGIC 0x32534653UL
#define FS_COMMIT 0x324D4F43UL
#define FS_PAGE 8192U
#define FS_TABLE_B (IMAGE_STORE_BASE+FS_PAGE)
#define FS_SCRATCH (FS_TABLE_B+FS_PAGE)
#define FS_DATA (FS_SCRATCH+FS_PAGE)
#define FS_CAPACITY (IMAGE_STORE_END-FS_DATA)
#define FS_MAX_FILES 64U
#define FS_FORMAT_KEY 0x46534F4BUL
#define FS_LIVE 1U
#define FS_WRITING 2U
#define FS_DELETED 3U
#define FS_RESERVED 4U
#define FS_CORRUPT 5U
enum { FS_BEGIN=0x20, FS_END, FS_SHOW, FS_DELETE, FS_READ, FS_LIST,
       FS_INFO, FS_ABORT, FS_DATA_OP, FS_PLAN, FS_FORMAT, FS_NAME, FS_RESERVE };
enum { FS_INVALID=0x80, FS_BUSY, FS_IO, FS_DISABLED, FS_LOCKED,
       FS_OFFSET, FS_CRC, FS_MISSING, FS_DISCONNECTED, FS_VERSION,
       FS_NO_SPACE, FS_REBUILD, FS_TABLE_FULL };
typedef struct {
    uint32_t id, offset, length, capacity, crc, revision;
    uint16_t width, height;
    uint8_t type, layout, state, version;
    char name[64];
} FS_Record;
typedef struct {
    uint32_t magic, version, generation, count;
    uint32_t ota_marker[4];
    uint32_t next_id, tail, journal_page, journal_crc;
    uint32_t journal_lo, journal_hi, formatting, reserved;
    uint8_t padding[448]; /* boot tag at +0x160 must stay erased */
    FS_Record files[FS_MAX_FILES];
    uint32_t commit[4];
} FS_Catalog;
_Static_assert(sizeof(FS_Record)==96, "Disk record size");
_Static_assert(sizeof(FS_Catalog)==6672, "Disk catalog size");
_Static_assert(FS_CAPACITY==464U*1024U, "Storage bounds");
static FS_Catalog fs_cat;
static uint32_t fs_current, fs_received;
static uint16_t fs_owner=NO_CONNECTION, fs_request_size, fs_result_size=32;
static uint8_t fs_request[232], fs_result[240], fs_mode, fs_upload=255, fs_name_size;
static volatile bool fs_playback;
static bool fs_initialized, fs_recover, fs_fault, fs_ota, fs_legacy_status;
static volatile bool fs_pending, fs_disconnected;
static uint16_t fs_safe_pose[SAFE_POSE_CHANNELS];
static bool fs_safe_pose_valid;
/* Select newest committed ID. Uploads always allocate a new file, so an
 * interrupted replacement leaves the previous safe pose usable. */
static void fs_refresh_safe_pose(void) {
    uint16_t values[SAFE_POSE_CHANNELS];
    uint16_t candidate[SAFE_POSE_CHANNELS];
    uint32_t newest=0;
    if(fs_mode==1 && !fs_fault && !fs_cat.formatting && !fs_cat.journal_page) {
        for(unsigned i=0;i<fs_cat.count;i++) {
            const FS_Record *r=&fs_cat.files[i];
            if(r->state==FS_LIVE && r->type==SAFE_POSE_FILE_TYPE && r->version==1 &&
               !r->width && !r->height && !r->layout && r->id>newest &&
               r->length==SAFE_POSE_BYTES && crc32((const uint8_t *)r->offset,r->length)==r->crc &&
               safe_pose_decode((const uint8_t *)r->offset,r->length,candidate)) {
                newest=r->id;memcpy(values,candidate,sizeof(values));
            }
        }
    }
    uint32_t key=enter();
    fs_safe_pose_valid=newest!=0;
    if(newest) memcpy(fs_safe_pose,values,sizeof(values));
    leave(key);
}
bool ImageStore_GetSafePose(uint16_t values[16]) {
    uint32_t key=enter();bool valid=fs_safe_pose_valid;
    if(valid) memcpy(values,fs_safe_pose,sizeof(fs_safe_pose));
    leave(key);return valid;
}
static uint32_t fs_align(uint32_t n) { return (n+15U)&~15U; }
static const FS_Catalog *fs_disk(uint32_t a) { return (const FS_Catalog *)a; }
static bool fs_catalog_valid(uint32_t a) {
    const FS_Catalog *c=fs_disk(a);
    if(c->magic!=FS_MAGIC || c->version!=2 || c->count>FS_MAX_FILES || !c->next_id ||
       c->tail<FS_DATA || c->tail>IMAGE_STORE_END || (c->tail&15U) ||
       c->commit[0]!=FS_COMMIT || c->commit[1]!=c->generation ||
       c->commit[2]!=~c->commit[3] || c->commit[2]!=crc32((const uint8_t *)c,sizeof(*c)-16U)) return false;
    for(unsigned i=0;i<4;i++) if(c->ota_marker[i]!=0xFFFFFFFFU) return false;
    if(*(const uint32_t *)(a+0x160U)!=0xFFFFFFFFU || c->formatting>1) return false;
    if(c->journal_page && (c->journal_page<FS_DATA || c->journal_page>=IMAGE_STORE_END ||
       (c->journal_page&(FS_PAGE-1U)) || c->journal_lo>=c->journal_hi ||
       c->journal_hi>FS_PAGE || (c->journal_lo&15U) || (c->journal_hi&15U))) return false;
    uint32_t end=FS_DATA;
    for(unsigned i=0;i<c->count;i++) {
        const FS_Record *r=&c->files[i];
        if(!r->id || r->id>=c->next_id || r->offset!=end || !r->capacity || (r->capacity&15U) ||
           r->capacity>IMAGE_STORE_END-end || r->length>r->capacity ||
           r->state<FS_LIVE || r->state>FS_CORRUPT || r->name[63]!=0) return false;
        for(unsigned j=0;j<i;j++) if(c->files[j].id==r->id) return false;
        end+=r->capacity;
    }
    return end==c->tail;
}
/* FF writes can consume ECC units too. Skip them in metadata and page copies. */
static bool fs_write(uint32_t a,const uint8_t *p,uint32_t n) {
    for(uint32_t at=0;at<n;at+=16) {
        bool erased=true;
        for(unsigned j=0;j<16;j++) if(p[at+j]!=255) {erased=false;break;}
        if(erased) continue;
        memcpy(aligned_data,p+at,16);
        if(!flash_op(a+at,16,false)) return false;
    }
    return true;
}
static bool fs_erase(uint32_t a,uint32_t pages) { return flash_op((a-FLASH_BASE)/FS_PAGE,pages,true); }
static bool fs_save(void) {
    uint32_t target=fs_current==IMAGE_STORE_BASE?FS_TABLE_B:IMAGE_STORE_BASE;
    if(!++fs_cat.generation) {fs_fault=true;return false;}
    fs_cat.commit[0]=FS_COMMIT;fs_cat.commit[1]=fs_cat.generation;
    fs_cat.commit[2]=crc32((const uint8_t *)&fs_cat,sizeof(fs_cat)-16);fs_cat.commit[3]=~fs_cat.commit[2];
    if(!fs_erase(target,1) || !fs_write(target,(const uint8_t *)&fs_cat,sizeof(fs_cat)-16) ||
       !fs_write(target+sizeof(fs_cat)-16,(const uint8_t *)fs_cat.commit,16) || !fs_catalog_valid(target)) {
        fs_fault=true;return false;
    }
    fs_current=target;return true;
}
/* Intent + scratch are durable before erasing a shared page. Replay is idempotent. */
static bool fs_replay(void) {
    if(!fs_cat.journal_page) return true;
    const uint8_t *backup=(const uint8_t *)FS_SCRATCH;
    if(crc32(backup,FS_PAGE)!=fs_cat.journal_crc || !fs_erase(fs_cat.journal_page,1)) return false;
    for(uint32_t at=0;at<FS_PAGE;at+=16) {
        if(at>=fs_cat.journal_lo && at<fs_cat.journal_hi) continue;
        if(!fs_write(fs_cat.journal_page+at,backup+at,16)) return false;
    }
    for(uint32_t at=0;at<FS_PAGE;at++) {
        uint8_t expected=at>=fs_cat.journal_lo && at<fs_cat.journal_hi?255:backup[at];
        if(*(const uint8_t *)(fs_cat.journal_page+at)!=expected) return false;
    }
    fs_cat.journal_page=fs_cat.journal_crc=fs_cat.journal_lo=fs_cat.journal_hi=0;
    return fs_save();
}
static bool fs_clear_extent(uint32_t start,uint32_t length) {
    uint32_t end=start+length;
    for(uint32_t page=start&~(FS_PAGE-1U);page<end;page+=FS_PAGE) {
        uint32_t lo=start>page?start-page:0,hi=end<page+FS_PAGE?end-page:FS_PAGE;
        if(lo==0 && hi==FS_PAGE) {if(!fs_erase(page,1)) return false;continue;}
        if(!fs_erase(FS_SCRATCH,1) || !fs_write(FS_SCRATCH,(const uint8_t *)page,FS_PAGE)) return false;
        fs_cat.journal_page=page;fs_cat.journal_crc=crc32((const uint8_t *)FS_SCRATCH,FS_PAGE);
        fs_cat.journal_lo=lo;fs_cat.journal_hi=hi;
        if(!fs_save() || !fs_replay()) return false;
    }
    return true;
}
static void fs_trim(void) {
    while(fs_cat.count && fs_cat.files[fs_cat.count-1].state==FS_DELETED) {
        fs_cat.tail=fs_cat.files[--fs_cat.count].offset;
        memset(&fs_cat.files[fs_cat.count],0,sizeof(FS_Record));
    }
}
static bool fs_finish_format(void) {
    if(!fs_erase(FS_SCRATCH,59)) return false;
    fs_cat.formatting=0;return fs_save();
}
static bool fs_recover_catalog(void) {
    if(!fs_recover) return !fs_fault;
    if(!fs_replay() || (fs_cat.formatting && !fs_finish_format())) {fs_fault=true;return false;}
    bool changed=false;
    for(unsigned i=0;i<fs_cat.count;i++) {
        FS_Record *r=&fs_cat.files[i];
        if(r->state==FS_WRITING) {r->state=FS_DELETED;r->length=0;changed=true;}
        else if(r->state==FS_LIVE && crc32((const uint8_t *)r->offset,r->length)!=r->crc) {r->state=FS_CORRUPT;changed=true;}
    }
    fs_trim();
    bool ok=!changed || fs_save();
    fs_recover=false;fs_refresh_safe_pose();return ok;
}
static uint32_t fs_holes(void) {
    uint32_t n=0;
    if(fs_mode==1) for(unsigned i=0;i<fs_cat.count;i++) if(fs_cat.files[i].state==FS_DELETED) n+=fs_cat.files[i].capacity;
    return n;
}
static void fs_status_base(uint8_t op,uint16_t seq,uint32_t id,uint8_t error) {
    memset(fs_result,0,32);fs_result[0]=2;fs_result[1]=error?3:2;fs_result[2]=op;fs_result[3]=error;
    fs_result[4]=(uint8_t)seq;fs_result[5]=seq>>8;fs_result[6]=(fs_fault||fs_ota)?3:fs_mode;
    fs_result[7]=fs_mode==1?fs_cat.count:0;put32(fs_result+8,id);put32(fs_result+16,fs_mode==1?fs_cat.generation:0);
    put32(fs_result+24,fs_mode==1?IMAGE_STORE_END-fs_cat.tail:fs_mode==0?FS_CAPACITY:0);
    put32(fs_result+28,fs_holes());fs_result_size=32;
}
void ImageStore_Init(void) {
    if(fs_initialized) return;
    LegacyStore_Init();bool a=fs_catalog_valid(IMAGE_STORE_BASE),b=fs_catalog_valid(FS_TABLE_B);
    if(a || b) {
        fs_current=(!a || (b && fs_disk(FS_TABLE_B)->generation>fs_disk(IMAGE_STORE_BASE)->generation))?FS_TABLE_B:IMAGE_STORE_BASE;
        memcpy(&fs_cat,fs_disk(fs_current),sizeof(fs_cat));fs_mode=1;fs_recover=true;
    } else fs_mode=catalog?2:empty?0:3;
    fs_refresh_safe_pose();
    fs_status_base(FS_INFO,0,0,0);fs_initialized=true;
}
static int fs_find(uint32_t id) {
    for(unsigned i=0;i<fs_cat.count;i++) if(fs_cat.files[i].id==id) return (int)i;
    return -1;
}
static bool fs_valid_name(const char *name,uint8_t length) {
    const uint8_t *p=(const uint8_t *)name;unsigned at=0;
    while(at<length) {
        uint32_t cp=p[at++],minimum=0;unsigned extra=0;
        if(cp<0x80U) {if(cp<32U || cp==127U) return false;continue;}
        if(cp>=0xC2U && cp<=0xDFU) {cp&=31U;extra=1;minimum=0x80U;}
        else if(cp>=0xE0U && cp<=0xEFU) {cp&=15U;extra=2;minimum=0x800U;}
        else if(cp>=0xF0U && cp<=0xF4U) {cp&=7U;extra=3;minimum=0x10000U;}
        else return false;
        if(at+extra>length) return false;
        while(extra--) {if((p[at]&0xC0U)!=0x80U) return false;cp=(cp<<6)|(p[at++]&63U);}
        if(cp<minimum || cp>0x10FFFFU || (cp>=0xD800U && cp<=0xDFFFU)) return false;
    }
    return true;
}
static uint8_t fs_image_args(const uint8_t *p,FS_Record *r) {
    memset(r,0,sizeof(*r));r->type=p[1];r->length=get32(p+8);r->crc=get32(p+12);
    r->width=p[16];r->height=p[17];r->layout=p[18];r->version=p[19];
    if((r->type!=1 && r->type!=2 && r->type!=3 && r->type!=SAFE_POSE_FILE_TYPE) || r->version!=1) return FS_VERSION;
    if(r->type==SAFE_POSE_FILE_TYPE) {
        if(r->width || r->height || r->layout || r->length!=SAFE_POSE_BYTES) return FS_INVALID;
        r->capacity=fs_align(r->length);return 0;
    }
    if(r->type==3) {
        if(r->width || r->height || r->layout || r->length<34 || r->length>FS_CAPACITY) return FS_INVALID;
        r->capacity=fs_align(r->length);return 0;
    }
    if(!r->width || !r->height || r->width>240 || r->height>240 || r->layout>1 ||
       (r->layout==1 && (r->width!=240 || r->height!=240))) return FS_INVALID;
    uint32_t pixels=r->layout==1?IMAGE_STORE_BYTES/2U:(uint32_t)r->width*r->height;
    if(r->length!=pixels*(r->type==1?2U:1U)) return FS_INVALID;
    r->capacity=fs_align(r->length);return 0;
}
static uint8_t fs_plan(uint32_t target,FS_Record *r,int *index) {
    int found=target?fs_find(target):-1;
    if(target && found<0) return FS_MISSING;
    if(found>=0) {
        FS_Record *old=&fs_cat.files[found];
        if(old->state==FS_LIVE || old->state==FS_CORRUPT) {
            if((unsigned)found!=fs_cat.count-1U) return FS_REBUILD;
        } else if(old->state!=FS_DELETED && old->state!=FS_RESERVED) return FS_BUSY;
        if((unsigned)found!=fs_cat.count-1U && r->capacity>old->capacity) return FS_REBUILD;
        if(r->capacity>IMAGE_STORE_END-old->offset) return FS_NO_SPACE;
        r->offset=old->offset;
        if((unsigned)found!=fs_cat.count-1U) r->capacity=old->capacity;
        if(old->state==FS_LIVE || old->state==FS_CORRUPT) {r->id=old->id;r->revision=old->revision+1U;}
    } else {
        for(unsigned i=0;i<fs_cat.count;i++) if(fs_cat.files[i].state==FS_DELETED && fs_cat.files[i].capacity>=r->capacity &&
           (found<0 || fs_cat.files[i].capacity<fs_cat.files[found].capacity)) found=(int)i;
        if(found>=0) {r->offset=fs_cat.files[found].offset;r->capacity=fs_cat.files[found].capacity;}
        else {
            if(fs_cat.count==FS_MAX_FILES) return FS_TABLE_FULL;
            if(r->capacity>IMAGE_STORE_END-fs_cat.tail) return FS_NO_SPACE;
            found=(int)fs_cat.count;r->offset=fs_cat.tail;
        }
    }
    if(!r->id) {if(fs_cat.next_id==UINT32_MAX) return FS_TABLE_FULL;r->id=fs_cat.next_id;r->revision=1;}
    *index=found;return 0;
}
uint8_t ImageStore_Submit(uint16_t conn,const uint8_t *p,uint16_t n,bool data) {
    ImageStore_Init();
    if(fs_playback) return FS_BUSY;
    if(!data && n && p[0]<FS_BEGIN) {
        if(fs_ota) return FS_LOCKED;
        if(fs_pending || fs_upload!=255) return FS_BUSY;
        if(fs_mode==1 || fs_fault) return FS_VERSION;
        uint8_t e=LegacyStore_Submit(conn,p,n,false);if(!e) fs_legacy_status=true;return e;
    }
    if(data && fs_upload==255 && uploading) return LegacyStore_Submit(conn,p,n,true);
#if !ENABLE_DISPLAY
    return FS_DISABLED;
#endif
    if(displayTaskHandle==NULL) return FS_BUSY;
    if((!data && (n<8 || n>20)) || (data && (n<5 || n>228))) return FS_INVALID;
    uint8_t op=data?FS_DATA_OP:p[0];
    if(op<FS_BEGIN || op>FS_RESERVE || (!data && op==FS_DATA_OP) || !get16(p+(data?0:2))) return FS_INVALID;
    if(!data) {
        unsigned expected=(op==FS_BEGIN || op==FS_PLAN)?20:(op==FS_READ || op==FS_LIST || op==FS_FORMAT || op==FS_RESERVE)?12:8;
        if(op==FS_NAME ? (n<13 || n>20) : n!=expected) return FS_INVALID;
    }
    uint32_t key=enter();
    if(fs_playback || fs_pending || pending || uploading || (fs_owner!=NO_CONNECTION && fs_owner!=conn)) {leave(key);return FS_BUSY;}
    if(fs_ota) {leave(key);return FS_LOCKED;}
    if(fs_upload!=255 && op!=FS_DATA_OP && op!=FS_END && op!=FS_ABORT && op!=FS_NAME && op!=FS_INFO) {leave(key);return FS_BUSY;}
    if((op==FS_DATA_OP || op==FS_END || op==FS_NAME) && fs_upload==255) {leave(key);return FS_OFFSET;}
    memcpy(fs_request,p,n);fs_request_size=n;fs_request[231]=data;
    fs_owner=conn;fs_disconnected=false;fs_legacy_status=false;
    fs_status_base(op,get16(p+(data?0:2)),data?fs_cat.files[fs_upload].id:get32(p+4),0);
    fs_result[1]=1;fs_pending=true;leave(key);osThreadFlagsSet(displayTaskHandle,1U);return 0;
}
uint16_t ImageStore_Status(uint8_t *out) {
    if(fs_legacy_status) return LegacyStore_Status(out);
    uint32_t key=enter();uint16_t n=fs_result_size;memcpy(out,fs_result,n);leave(key);return n;
}
static bool fs_abort_upload(void) {
    if(fs_upload==255) return true;
    fs_cat.files[fs_upload].state=FS_DELETED;fs_cat.files[fs_upload].length=0;fs_upload=255;fs_trim();return fs_save();
}
void ImageStore_Disconnect(uint16_t conn) {
    LegacyStore_Disconnect(conn);uint32_t key=enter();if(fs_owner==conn) fs_disconnected=true;leave(key);
    if(displayTaskHandle) osThreadFlagsSet(displayTaskHandle,1U);
}
void ImageStore_Process(void) {
    if(pending) {LegacyStore_Process();fs_mode=catalog?2:empty?0:3;return;}
    if(fs_ota || fs_playback) return;
    if(fs_mode==1 && fs_recover) (void)fs_recover_catalog();
    if(!fs_pending) {
        uint32_t key=enter();
        bool cleanup=!fs_pending && fs_disconnected && fs_upload!=255 && !fs_fault;
        if(cleanup) fs_pending=true;
        if(!fs_pending && fs_disconnected) fs_owner=NO_CONNECTION;
        leave(key);
        if(cleanup) {
            (void)fs_abort_upload();
            key=enter();fs_pending=false;fs_owner=NO_CONNECTION;leave(key);
        }
        return;
    }
    bool data=fs_request[231]!=0;uint8_t op=data?FS_DATA_OP:fs_request[0],error=0;
    uint16_t seq=get16(fs_request+(data?0:2));
    uint32_t id=data && fs_upload!=255?fs_cat.files[fs_upload].id:get32(fs_request+4),value=0,checksum=0;
    uint16_t payload=0;uint8_t response[208];
    if(fs_fault || fs_mode==3) error=FS_LOCKED;
    if(op==FS_INFO) {
        error=0;put32(response,FS_CAPACITY);put32(response+4,FS_MAX_FILES);put32(response+8,30);put32(response+12,3);payload=16;
    } else if(op==FS_FORMAT && !error) {
        if(get32(fs_request+8)!=FS_FORMAT_KEY) error=FS_INVALID;
        else {
            uint32_t gen=fs_mode==1?fs_cat.generation:0,next=fs_mode==1?fs_cat.next_id:6;
            if(fs_mode!=1) fs_current=IMAGE_STORE_BASE;
            memset(&fs_cat,0,sizeof(fs_cat));memset(fs_cat.padding,255,sizeof(fs_cat.padding));memset(fs_cat.ota_marker,255,sizeof(fs_cat.ota_marker));
            fs_cat.magic=FS_MAGIC;fs_cat.version=2;fs_cat.generation=gen;fs_cat.next_id=next;fs_cat.tail=FS_DATA;fs_cat.formatting=1;
            if(!fs_save()) error=FS_IO;
            else {fs_mode=1;if(!fs_finish_format()) {fs_fault=true;error=FS_IO;}}
        }
    } else if(!error && fs_mode!=1) error=FS_VERSION;
    else if(!error && (op==FS_BEGIN || op==FS_PLAN || op==FS_RESERVE)) {
        FS_Record r;int index=-1;
        if(op==FS_RESERVE) {
            memset(&r,0,sizeof(r));r.capacity=get32(fs_request+8);
            if(!r.capacity || r.capacity>FS_CAPACITY) error=FS_INVALID;else r.capacity=fs_align(r.capacity);
        } else error=fs_image_args(fs_request,&r);
        if(!error) error=fs_plan(id,&r,&index);
        if(!error) {
            id=r.id;r.state=op==FS_RESERVE?FS_RESERVED:FS_WRITING;
            if(op!=FS_PLAN) {
                if(r.id==fs_cat.next_id) fs_cat.next_id++;
                fs_cat.files[index]=r;if((unsigned)index==fs_cat.count) fs_cat.count++;
                if((unsigned)index==fs_cat.count-1U) fs_cat.tail=r.offset+r.capacity;
                if(!fs_save()) error=FS_IO;
                else if(op==FS_BEGIN) {
                    fs_upload=(uint8_t)index;fs_received=0;fs_name_size=0;
                    if(!fs_clear_extent(r.offset,r.capacity)) {fs_fault=true;error=FS_IO;}
                }
            }
            if(!error) {memcpy(response,&r,sizeof(r));payload=sizeof(r);}
        }
    } else if(!error && op==FS_DATA_OP) {
        FS_Record *r=&fs_cat.files[fs_upload];uint32_t offset=(uint32_t)get16(fs_request+2)*16U,n=fs_request_size-4;
        if(offset!=fs_received || offset>r->length || n>r->length-offset || ((n&15U) && offset+n!=r->length)) error=FS_OFFSET;
        else {
            memset(aligned_data,255,sizeof(aligned_data));memcpy(aligned_data,fs_request+4,n);
            if(!flash_op(r->offset+offset,fs_align(n),false)) {fs_fault=true;error=FS_IO;}else fs_received+=n;
        }
        value=fs_received;
    } else if(!error && (op==FS_END || op==FS_NAME)) {
        FS_Record *r=&fs_cat.files[fs_upload];
        if(id!=r->id) error=FS_OFFSET;
        else if(op==FS_NAME) {
            uint32_t at=get32(fs_request+8),n=fs_request_size-12;
            if(at!=fs_name_size || at+n>63) error=FS_INVALID;
            else {memcpy(r->name+at,fs_request+12,n);fs_name_size+=n;r->name[fs_name_size]=0;}
        } else if(!fs_valid_name(r->name,fs_name_size)) error=FS_INVALID;
        else if(fs_received!=r->length || crc32((const uint8_t *)r->offset,r->length)!=r->crc) {
            error=FS_CRC;if(!fs_abort_upload()) error=FS_IO;
        } else if(r->type==3 && !timeline_validate((const uint8_t *)r->offset,r->length)) {
            error=FS_INVALID;if(!fs_abort_upload()) error=FS_IO;
        } else if(r->type==SAFE_POSE_FILE_TYPE && !safe_pose_decode((const uint8_t *)r->offset,r->length,NULL)) {
            error=FS_INVALID;if(!fs_abort_upload()) error=FS_IO;
        } else {r->state=FS_LIVE;checksum=r->crc;fs_upload=255;if(!fs_save()) error=FS_IO;}
    } else if(!error && op==FS_ABORT) {if(!fs_abort_upload()) error=FS_IO;
    } else if(!error && op==FS_LIST) {
        uint32_t cursor=get32(fs_request+8);value=UINT32_MAX;
        if(cursor<fs_cat.count) {memcpy(response,&fs_cat.files[cursor],sizeof(FS_Record));payload=sizeof(FS_Record);value=cursor+1;}
        else if(cursor>fs_cat.count) error=FS_INVALID;
    } else if(!error && op!=FS_INFO && op!=FS_FORMAT) {
        int i=fs_find(id);
        if(i<0) error=FS_MISSING;
        else {
            FS_Record *r=&fs_cat.files[i];checksum=r->crc;
            if(op==FS_DELETE) {r->state=FS_DELETED;r->length=0;fs_trim();if(!fs_save()) error=FS_IO;}
            else if(r->state!=FS_LIVE) error=FS_MISSING;
            else if(op==FS_SHOW) {
                if(r->version!=1 || (r->type!=1 && r->type!=2)) error=FS_VERSION;
                else error=Display_ShowFile((const uint8_t *)r->offset,r->length,r->type,r->layout,r->width,r->height);
            } else if(op==FS_READ) {
                value=get32(fs_request+8);
                if(value>=r->length) error=FS_INVALID;
                else {payload=(uint16_t)(r->length-value>208?208:r->length-value);memcpy(response,(const void *)(r->offset+value),payload);}
            } else error=FS_INVALID;
        }
    }
    /* The durable file is authoritative even if disconnect loses the reply. */
    if(!error && (op==FS_END || op==FS_DELETE || op==FS_FORMAT)) fs_refresh_safe_pose();
    if(fs_disconnected) {
        if(!fs_fault && fs_upload!=255 && !fs_abort_upload()) error=FS_IO;
        if(!error) error=FS_DISCONNECTED;
    }
    uint32_t key=enter();fs_status_base(op,seq,id,error);put32(fs_result+12,value);put32(fs_result+20,checksum);
    if(!error && payload) {memcpy(fs_result+32,response,payload);fs_result_size+=payload;}
    if(fs_upload==255 || fs_disconnected) fs_owner=NO_CONNECTION;
    fs_pending=false;leave(key);
}
bool ImageStore_ClaimOTA(void) {
    ImageStore_Init();
    uint32_t key=enter();
    if(fs_playback || fs_pending || fs_upload!=255 || fs_recover || fs_fault || pending || uploading) {leave(key);return false;}
    fs_ota=true;leave(key);
    if(fs_mode!=1) return LegacyStore_ClaimOTA();
    /* Both snapshots must be invalid before OTA erases any page. */
    for(unsigned i=0;i<2;i++) {
        uint32_t a=IMAGE_STORE_BASE+i*FS_PAGE;
        if(fs_catalog_valid(a) && !write16(a+16,0,0,0,0)) return false;
    }
    return true;
}

bool ImageStore_AcquireTimeline(uint32_t id,const uint8_t **payload,uint32_t *length) {
    ImageStore_Init();
    uint32_t key=enter();
    if(fs_playback || fs_pending || fs_upload!=255 || fs_recover || fs_fault ||
       fs_ota || pending || uploading || fs_mode!=1) {leave(key);return false;}
    int index=fs_find(id);
    if(index<0 || fs_cat.files[index].state!=FS_LIVE || fs_cat.files[index].type!=3 ||
       fs_cat.files[index].version!=1) {leave(key);return false;}
    FS_Record r=fs_cat.files[index];fs_playback=true;leave(key);
    const uint8_t *p=(const uint8_t *)r.offset;
    if(crc32(p,r.length)!=r.crc || !timeline_validate(p,r.length)) {
        ImageStore_ReleaseTimeline();return false;
    }
    *payload=p;*length=r.length;return true;
}
void ImageStore_ReleaseTimeline(void) {
    uint32_t key=enter();fs_playback=false;leave(key);
}
