#include "wasm_profile.h"
#include <string.h>
typedef struct { const uint8_t *p,*end; bool ok; } Reader;
typedef struct { uint8_t n, result, args[4]; } Signature;
static uint8_t byte(Reader *r) { if(r->p==r->end) {r->ok=false;return 0;} return *r->p++; }
static uint32_t leb(Reader *r) {
    uint32_t v=0;
    for(unsigned i=0;i<5;i++) {uint8_t b=byte(r);if(i==4 && (b&0xF0U)) r->ok=false;
        v|=(uint32_t)(b&127U)<<(7U*i);if(!(b&128U)) return v;}
    r->ok=false;return 0;
}
static int32_t signed_leb(Reader *r) {
    uint32_t v=0;unsigned shift=0;uint8_t b=0;
    for(unsigned i=0;i<5;i++) {b=byte(r);if(i==4 && (b&0x78U)!=0 && (b&0x78U)!=0x78U) r->ok=false;
        v|=(uint32_t)(b&127U)<<shift;shift+=7;if(!(b&128U)) {if(shift<32 && (b&64U))v|=~0U<<shift;return (int32_t)v;}}
    r->ok=false;return 0;
}
static bool name(Reader *r,const char *s) {
    uint32_t n=leb(r);if(n>(uint32_t)(r->end-r->p)) {r->ok=false;return false;}
    bool equal=n==strlen(s) && !memcmp(r->p,s,n);r->p+=n;return equal;
}
static bool numeric(uint8_t t) {return t>=0x7c && t<=0x7f;}
bool RobotWasm_Validate(const uint8_t *bytes,uint32_t length) {
    if(!bytes || length<8 || length>ROBOT_WASM_MAX_BYTES || memcmp(bytes,"\0asm\1\0\0\0",8)) return false;
    Reader file={bytes+8,bytes+length,true};
    Signature types[16]={0};uint8_t funcs[21]={0},gtype[16]={0},gmut[16]={0};int32_t globals[16]={0};
    uint32_t nt=0,nf=0,ni=0,ng=0,defined=0;uint8_t last=0;unsigned seen=0, exports=0;
    int32_t heap=-1,data_end=-1;
    while(file.p<file.end && file.ok) {
        uint8_t id=byte(&file);uint32_t size=leb(&file);
        if(!file.ok || size>(uint32_t)(file.end-file.p)) return false;
        Reader r={file.p,file.p+size,true};file.p+=size;
        if(!id) continue;
        if(id<=last || id>11 || (seen&(1U<<id))) return false;
        last=id;seen|=1U<<id;
        uint32_t count=leb(&r);
        switch(id) {
        case 1:
            if(count>16) return false;
            nt=count;
            for(uint32_t i=0;i<count;i++) {
                if(byte(&r)!=0x60) return false;
                uint32_t n=leb(&r);if(n>4) return false;types[i].n=(uint8_t)n;
                for(uint32_t j=0;j<n;j++) {types[i].args[j]=byte(&r);if(!numeric(types[i].args[j]))return false;}
                n=leb(&r);if(n>1)return false;types[i].result=n?byte(&r):0;
                if(n && !numeric(types[i].result)) return false;
            }break;
        case 2:
            if(count>5)return false;
            ni=count;
            for(uint32_t i=0;i<count;i++) {
                if(!name(&r,"robot"))return false;
                Reader n=r;int arity=-1;
                if(name(&n,"imu_word") || (n=r,name(&n,"tof_mm")) || (n=r,name(&n,"servo_get_us")))arity=1;
                else if((n=r,name(&n,"servo_set_us")))arity=2;
                else if((n=r,name(&n,"millis")))arity=0;
                r=n;if(arity<0 || byte(&r)!=0)return false;
                uint32_t t=leb(&r);if(t>=nt || types[t].n!=arity || types[t].result!=0x7f)return false;
                for(int j=0;j<arity;j++)if(types[t].args[j]!=0x7f)return false;
                funcs[nf++]=(uint8_t)t;
            }break;
        case 3:
            if(count<1 || count>16)return false;
            defined=count;
            for(uint32_t i=0;i<count;i++) {uint32_t t=leb(&r);if(t>=nt || nf>=21)return false;funcs[nf++]=(uint8_t)t;}break;
        case 4:
            /* LLD emits one empty, fixed funcref slot even without function
             * pointers. Element segments remain forbidden, so it stays null. */
            if(count!=1 || byte(&r)!=0x70 || leb(&r)!=1 || leb(&r)!=1 || leb(&r)!=1)return false;
            break;
        case 5:
            if(count!=1 || leb(&r)!=1 || leb(&r)!=1 || leb(&r)!=1)return false;
            break;
        case 6:
            if(count>16)return false;
            ng=count;
            for(uint32_t i=0;i<count;i++) {
                gtype[i]=byte(&r);gmut[i]=byte(&r);if(!numeric(gtype[i]) || gmut[i]>1)return false;
                uint8_t op=byte(&r);
                if(gtype[i]==0x7f && op==0x41)globals[i]=signed_leb(&r);
                else if(gtype[i]==0x7d && op==0x43) {for(unsigned j=0;j<4;j++)byte(&r);}
                else if(gtype[i]==0x7c && op==0x44) {for(unsigned j=0;j<8;j++)byte(&r);}
                else return false;
                if(byte(&r)!=0x0b)return false;
            }break;
        case 7:
            if(count<3 || count>4)return false;
            for(uint32_t i=0;i<count;i++) {
                Reader n=r;unsigned flag=0;
                if(name(&n,"step"))flag=1;
                else if((n=r,name(&n,"__heap_base")))flag=2;
                else if((n=r,name(&n,"__data_end")))flag=4;
                else if((n=r,name(&n,"memory")))flag=8;
                r=n;if(!flag || (exports&flag))return false;exports|=flag;
                uint8_t kind=byte(&r);uint32_t idx=leb(&r);
                if(flag==1) {if(kind || idx<ni || idx>=nf)return false;Signature *t=&types[funcs[idx]];
                    if(t->n!=1 || t->args[0]!=0x7f || t->result!=0x7f)return false;}
                else if(flag==8) {if(kind!=2 || idx!=0)return false;}
                else {if(kind!=3 || idx>=ng || gtype[idx]!=0x7f || gmut[idx])return false;
                    if(flag==2)heap=globals[idx];else data_end=globals[idx];}
            }break;
        case 10:
            if(count!=defined || !count)return false;
            for(uint32_t i=0;i<count;i++) {
                uint32_t n=leb(&r);if(n>1536 || n>(uint32_t)(r.end-r.p))return false;
                Reader body={r.p,r.p+n,true};r.p+=n;
                uint32_t groups=leb(&body),locals=0;if(groups>32)return false;
                for(uint32_t j=0;j<groups;j++) {uint32_t amount=leb(&body);if(amount>32 || locals+amount>32 || !numeric(byte(&body)))return false;locals+=amount;}
                if(!body.ok || body.p>=body.end || body.end[-1]!=0x0b)return false;
            }break;
        case 11:
            if(count>1)return false;
            if(count) {if(leb(&r)!=0 || byte(&r)!=0x41)return false;int32_t at=signed_leb(&r);
                if(byte(&r)!=0x0b || at<0)return false;
                uint32_t n=leb(&r);
                if(n>1024 || n>(uint32_t)(r.end-r.p) || data_end<0 || (uint32_t)at>(uint32_t)data_end || n>(uint32_t)data_end-(uint32_t)at)return false;
                r.p+=n;}
            break;
        default:return false; /* Includes start functions and elements. */
        }
        if(!r.ok || r.p!=r.end)return false;
    }
    return file.ok && (exports&7)==7 && (seen&(1U<<5)) && (seen&(1U<<10)) &&
           data_end>=0 && data_end<=1024 && heap>=data_end+1024 && heap<=2048;
}
