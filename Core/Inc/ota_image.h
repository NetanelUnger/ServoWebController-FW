#ifndef OTA_IMAGE_H
#define OTA_IMAGE_H
#include <stdint.h>
#include <stddef.h>

/* Shared by application, install manager and host tests. See OTA.md. */
#define OTA_ACTIVE_ADDRESS 0x08006000UL
#define OTA_DOWNLOAD_ADDRESS 0x08080000UL
#define OTA_SLOT_SIZE (488UL * 1024UL)
#define OTA_COMMIT_ADDRESS (OTA_DOWNLOAD_ADDRESS + OTA_SLOT_SIZE - 16UL)
#define OTA_MAX_IMAGE_SIZE (OTA_SLOT_SIZE - 16UL)
#define OTA_TARGET_ID 0x35414257UL /* WBA5 */
#define OTA_COMMIT_MAGIC 0x3241544FUL /* OTA2 */
#define OTA_TAG_OFFSET 0x160UL
#define OTA_TAG_MAGIC 0x94448A29UL
#define OTA_PROTOCOL_VERSION 2U

static inline uint32_t OTA_Get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void OTA_Put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static inline uint32_t OTA_Crc32(const uint8_t *p,uint32_t n) {
    uint32_t c=0xffffffffU;
    while(n--) {c^=*p++;for(unsigned b=0;b<8;b++) c=(c>>1)^((0U-(c&1U))&0xedb88320U);}
    return ~c;
}
static inline int OTA_ImageValid(const uint8_t *p,uint32_t n) {
    if(n<OTA_TAG_OFFSET+4U || n>OTA_MAX_IMAGE_SIZE || (n&15U)) return 0;
    uint32_t sp=OTA_Get32(p),pc=OTA_Get32(p+4),tag=OTA_Get32(p+OTA_TAG_OFFSET);
    if(sp<0x20000020U || sp>0x20020000U || (sp&7U) || !(pc&1U) ||
       (pc&~1U)<OTA_ACTIVE_ADDRESS || (pc&~1U)>=OTA_ACTIVE_ADDRESS+n ||
       (tag&3U) || tag<OTA_ACTIVE_ADDRESS+OTA_TAG_OFFSET+4U || tag>OTA_ACTIVE_ADDRESS+n-4U) return 0;
    uint32_t end=tag-OTA_ACTIVE_ADDRESS+4U;
    if(((end+15U)&~15U)!=n || OTA_Get32(p+end-4U)!=OTA_TAG_MAGIC) return 0;
    for(uint32_t i=end;i<n;i++) if(p[i]!=0xffU) return 0;
    return 1;
}
static inline int OTA_CommitValid(const uint8_t *image,const uint8_t *record) {
    uint32_t n=OTA_Get32(record+4),crc=OTA_Get32(record+8);
    return OTA_Get32(record)==OTA_COMMIT_MAGIC && OTA_Get32(record+12)==~crc &&
           OTA_ImageValid(image,n) && OTA_Crc32(image,n)==crc;
}
#endif
