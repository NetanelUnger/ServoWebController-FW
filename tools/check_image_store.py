"""Execute real image_store.c on ARM/Unicorn with strict mock Flash/RTOS.
Covers ownership, persistence, CRC, partial writes, reads, disconnect and OTA.
Does not model RF timing, Flash electrical faults or physical display output.
"""
import argparse, struct, subprocess, tempfile, zlib
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'STM32CubeIDE/Application/User/Core'
BASE=0x08080000
SIZE=0x7a000
FILES={
'main.h': r"""#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef int HAL_StatusTypeDef;
typedef void GPIO_TypeDef;
typedef int GPIO_PinState;
#define __get_PRIMASK() 0U
#define __disable_irq() ((void)0)
#define __set_PRIMASK(x) ((void)(x))
""",
'cmsis_os2.h': r"""#pragma once
#include <stdint.h>
typedef int osStatus_t;
typedef void *osThreadId_t;
uint32_t osThreadFlagsSet(osThreadId_t,uint32_t);
void osDelay(uint32_t);
""",
'app_freertos.h': '#include "cmsis_os2.h"\nextern osThreadId_t displayTaskHandle;\n',
'app_conf.h': '#define FLASH_PAGE_SIZE 8192U\n#define FLASH_BASE 0x08000000UL\n#define CFG_ACTIVE_SLOT_START_SECTOR_INDEX 3\n#define CFG_USER_DATA_SECTORS_NB 1\n',
'config.h':'#define ENABLE_DISPLAY 1\n',
'flash_manager.h': r"""#pragma once
#include <stdint.h>
typedef enum {FM_OK,FM_BUSY,FM_ERROR} FM_Cmd_Status_t;
typedef enum {FM_OPERATION_COMPLETE,FM_OPERATION_AVAILABLE} FM_FlashOp_Status_t;
typedef struct {struct {void *next,*prev;} NodeList; void (*Callback)(FM_FlashOp_Status_t);} FM_CallbackNode_t;
FM_Cmd_Status_t FM_Write(uint32_t *,uint32_t *,int32_t,FM_CallbackNode_t *);
FM_Cmd_Status_t FM_Erase(uint32_t,uint32_t,FM_CallbackNode_t *);
""",
'stubs.c':r"""#include "main.h"
#include "flash_manager.h"
void *displayTaskHandle=(void *)1;
unsigned bad_flash, fail_next, busy_next, shows;
void *memcpy(void *d,const void *s,size_t n){uint8_t *a=d;const uint8_t *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int v,size_t n){uint8_t *a=d;while(n--)*a++=v;return d;}
uint32_t osThreadFlagsSet(void *t,uint32_t v){return v;}
void osDelay(uint32_t t){}
uint8_t Display_ShowStored(const uint8_t *p){shows++;return 0;}
FM_Cmd_Status_t FM_Write(uint32_t *s,uint32_t *d,int32_t words,FM_CallbackNode_t *cb){
 if(busy_next){busy_next=0;cb->Callback(FM_OPERATION_AVAILABLE);return FM_BUSY;}
 if(fail_next){fail_next=0;return FM_ERROR;}
 unsigned a=(unsigned)d,n=words*4;
 if(a<0x08080000 || a+n>0x080fa000 || (a&15) || (n&15)){bad_flash++;return FM_ERROR;}
 for(unsigned i=0;i<n;i++)if(((uint8_t*)d)[i]!=255){bad_flash++;return FM_ERROR;}
 memcpy(d,s,n);cb->Callback(FM_OPERATION_COMPLETE);return FM_OK;
}
FM_Cmd_Status_t FM_Erase(uint32_t page,uint32_t n,FM_CallbackNode_t *cb){
 if(fail_next){fail_next=0;return FM_ERROR;}
 if(page<64 || page+n>125){bad_flash++;return FM_ERROR;}
 memset((void*)(0x08000000+page*8192),255,n*8192);cb->Callback(FM_OPERATION_COMPLETE);return FM_OK;
}
"""}
class Machine:
 def __init__(self,elf,flash=None):
  self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
  self.uc.mem_map(0x10000,0x80000);self.uc.mem_map(0x20000000,0x20000);self.uc.mem_map(BASE,SIZE)
  self.uc.mem_write(BASE,flash or bytes([255])*SIZE)
  with open(elf,'rb') as f:
   e=ELFFile(f)
   for s in e.iter_segments():
    if s['p_type']=='PT_LOAD' and s['p_filesz']:self.uc.mem_write(s['p_vaddr'],s.data())
   self.sym={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  self.seq=0;self.call('ImageStore_Init')
 def call(self,name,*args):
  self.uc.reg_write(UC_ARM_REG_SP,0x2001f000);self.uc.reg_write(UC_ARM_REG_LR,0x80001)
  for r,a in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):self.uc.reg_write(r,a)
  self.uc.emu_start(self.sym[name]|1,0x80000,count=100000000)
  assert self.uc.reg_read(UC_ARM_REG_PC)==0x80000,name+' hung'
  return self.uc.reg_read(UC_ARM_REG_R0)
 def submit(self,p,data=False,conn=1):
  self.uc.mem_write(0x20018000,p)
  return self.call('ImageStore_Submit',conn,0x20018000,len(p),int(data))
 def status(self):
  n=self.call('ImageStore_Status',0x20018000)
  return bytes(self.uc.mem_read(0x20018000,n))
 def command(self,op,id=1,*args):
  self.seq+=1;p=struct.pack('<BBH',op,id,self.seq)+b''.join(struct.pack('<I',a) for a in args)
  assert self.submit(p)==0,(op,self.status())
  self.call('ImageStore_Process');return self.status()
 def upload(self,pixels,id=1,crc=None):
  assert self.command(1,id,len(pixels),zlib.crc32(pixels) if crc is None else crc)[1]==2
  for offset in range(0,len(pixels),224):
   self.seq+=1;p=struct.pack('<HH',self.seq,offset//16)+pixels[offset:offset+224]
   assert self.submit(p,True)==0
   self.call('ImageStore_Process');assert self.status()[1]==2
  completed=self.command(2,id)
  assert completed[1]==2 if crc is None else completed[6]==0x86
 def flash(self):return bytes(self.uc.mem_read(BASE,SIZE))
 def variable(self,name,value=None):
  if value is not None:self.uc.mem_write(self.sym[name],struct.pack('<I',value))
  return struct.unpack('<I',self.uc.mem_read(self.sym[name],4))[0]
def verify(elf):
 pixels=bytes((i*13)&255 for i in range(91384));m=Machine(elf)
 assert sum((240-2*m.call('ImageStore_RowLeft',y))*2 for y in range(240))==len(pixels)
 assert m.command(6,0)[7]==0
 assert m.command(3)[6]==0x87
 assert m.command(1,1,12,0)[6]==0x80
 m.variable('busy_next',1);m.upload(pixels)
 assert m.command(6,0)[7]==1
 assert m.command(3)[1]==2 and m.variable('shows')==1
 read=bytearray()
 for offset in range(0,len(pixels),224):
  p=m.command(5,1,offset);assert p[1]==2;read.extend(p[16:])
 assert read==pixels
 reboot=Machine(elf,m.flash());assert reboot.command(6,0)[7]==1
 # Another connection cannot interleave data; incomplete commit never validates.
 assert m.command(1,2,len(pixels),zlib.crc32(pixels))[1]==2
 assert m.submit(struct.pack('<BBH',6,0,44),conn=2)==0x81
 assert m.call('ImageStore_ClaimOTA')==0
 assert m.command(2,2)[6]==0x86
 assert m.command(1,2,len(pixels),zlib.crc32(pixels))[1]==2
 m.call('ImageStore_Disconnect',1);assert m.command(6,0)[7]==1
 m.upload(pixels,5);assert m.command(6,0)[7]==17
 assert m.variable('bad_flash')==0
 # Power loss after payload begins but before commit keeps slot invalid.
 assert m.command(1,2,len(pixels),zlib.crc32(pixels))[1]==2
 m.seq+=1;assert m.submit(struct.pack('<HH',m.seq,0)+pixels[:224],True)==0
 m.call('ImageStore_Process');partial=Machine(elf,m.flash());assert partial.command(6,0)[7]==17
 # CRC corruption is detected at reboot.
 damaged=bytearray(m.flash());damaged[8192+32]^=1
 corrupt=Machine(elf,bytes(damaged));assert corrupt.command(6,0)[7]==16
 # OTA durably invalidates catalog and blocks all future image writes, including reboot.
 assert partial.call('ImageStore_ClaimOTA')==1
 assert partial.submit(struct.pack('<BBH',6,0,99))==0x84
 locked=Machine(elf,partial.flash());assert locked.submit(struct.pack('<BBH',6,0,99))==0x84
 bad=Machine(elf);bad.upload(pixels,crc=zlib.crc32(pixels)^1);assert bad.command(6,0)[7]==0
 assert reboot.command(4,1)[1]==2 and reboot.command(6,0)[7]==0
 # Unknown download bytes are never automatically erased.
 unknown=bytearray([255])*SIZE;unknown[0]=0
 locked=Machine(elf,bytes(unknown));assert locked.submit(struct.pack('<BBHII',1,1,1,len(pixels),0))==0x84
 # Failed data write cannot be continued as if durable.
 f=Machine(elf);assert f.command(1,1,len(pixels),0)[1]==2
 f.variable('fail_next',1);assert f.submit(struct.pack('<HH',2,0)+pixels[:224],True)==0
 f.call('ImageStore_Process');assert f.status()[6]==0x82
 assert f.submit(struct.pack('<HH',3,0)+pixels[:224],True)!=0
 print('PASS: geometry, round trip, first/last slot bounds, persistence, CRC, partial commit, disconnect, ownership, flash failure and OTA exclusion')
def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='image-store-test-') as d:
  d=Path(d)
  for name,content in FILES.items():(d/name).write_text(content)
  elf=d/'store.elf'
  subprocess.run([a.cc,'-mcpu=cortex-m3','-mthumb','-O1','-g','-ffreestanding','-nostdlib','-I'+str(d),'-I'+str(APP),str(APP/'display/image_store.c'),str(d/'stubs.c'),'-Wl,-Ttext=0x10000,-Tdata=0x20000000,-e,ImageStore_Init','-lgcc','-o',str(elf)],check=True)
  verify(elf)
if __name__=='__main__':main()
