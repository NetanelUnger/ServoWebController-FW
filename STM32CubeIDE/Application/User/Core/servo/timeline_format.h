#ifndef TIMELINE_FORMAT_H
#define TIMELINE_FORMAT_H
#include <stdint.h>
#include <stdbool.h>
#define TIMELINE_MAX_EVENTS 4096U
#define TIMELINE_MAX_MS UINT32_MAX
static inline uint16_t timeline_u16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static inline uint32_t timeline_u32(const uint8_t *p) { return timeline_u16(p)|((uint32_t)timeline_u16(p+2)<<16); }
/* Binary execution plan is authoritative. JSON is opaque, round-trip editor data.
 * Identical dispatch/kind/channel and overlapping linear moves are forbidden.
 * A direct and a linear event at the same time/channel are intentionally legal. */
static inline bool timeline_validate(const uint8_t *p,uint32_t size) {
    if(!p || size<34 || timeline_u32(p)!=0x314c4d54U || timeline_u16(p+4)!=1 ||
       timeline_u16(p+6)!=32 || timeline_u32(p+24) || timeline_u32(p+28)) return false;
    uint32_t count=timeline_u32(p+8),duration=timeline_u32(p+12),json=timeline_u32(p+20);
    if(count>TIMELINE_MAX_EVENTS ||
       json<2 || count>(size-32)/16 || json!=size-32-count*16) return false;
    uint32_t ends[16]={0},starts[16]={0};
    uint32_t prev=0;uint8_t prev_kind=0,prev_ch=0;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *e=p+32+i*16;uint32_t at=timeline_u32(e),ms=timeline_u32(e+4);
        uint16_t pulse=timeline_u16(e+8);uint8_t ch=e[10],kind=e[11];
        if(ch>=16 || kind>1 || pulse<500 || pulse>2500 || at>duration ||
           ms>duration-at || (kind==0 && ms) || (kind==1 && !ms) || timeline_u32(e+12)) return false;
        if(i && (at<prev || (at==prev && (kind<prev_kind || (kind==prev_kind && ch<=prev_ch))))) return false;
        if(kind==1) {if(at<ends[ch]) return false;starts[ch]=at;ends[ch]=at+ms;}
        else if(at>starts[ch] && at<ends[ch]) return false;
        prev=at;prev_kind=kind;prev_ch=ch;
    }
    /* UTF-8 well-formedness, no NUL; the editor validates the JSON schema. */
    const uint8_t *j=p+32+count*16;uint32_t at=0;
    while(at<json) {
        uint32_t cp=j[at++],min=0;unsigned more=0;
        if(cp<128) {if(!cp) return false;continue;}
        if(cp>=0xc2 && cp<=0xdf) {cp&=31;more=1;min=128;}
        else if(cp>=0xe0 && cp<=0xef) {cp&=15;more=2;min=2048;}
        else if(cp>=0xf0 && cp<=0xf4) {cp&=7;more=3;min=65536;}
        else return false;
        if(more>json-at) return false;
        while(more--) {if((j[at]&0xc0)!=0x80) return false;cp=(cp<<6)|(j[at++]&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
#endif
