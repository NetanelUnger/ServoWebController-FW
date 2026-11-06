"""ARM test of the actual bootloader candidate check (no hardware writes)."""
import argparse,subprocess,tempfile,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
s=(ROOT/'Bootloader/BLE_ApplicationInstallManager/Core/Src/main.c').read_text()
start=s.index('static uint8_t CheckFwAppValidity( uint8_t page_idx )\n{');end=s.index('\n}\n',start)+3
source='#include <stdint.h>\n#define FLASH_BASE 0x08000000UL\n#define FLASH_SIZE 0x100000U\n#define FLASH_PAGE_SIZE 8192U\n#define SRAM1_BASE 0x20000000UL\n#define KEYWORD_ADDRESS_OFFSET 0x160U\n#define CFG_ACTIVE_SLOT_START_SECTOR_INDEX 3U\n#define DOWNLOAD_SLOT_START_SECTOR_INDEX 64U\n#define APP_SLOT_PAGE_SIZE 61U\n'+s[start:end]+'\nuint8_t check(uint8_t p){return CheckFwAppValidity(p);}\n'
ota=(ROOT/'STM32_WPAN/App/ota_app.c').read_text()
init_start=ota.index('void OTA_APP_Init(void)\n{');init_end=ota.index('\n}\n',init_start)+3
source+='''
#include <stdbool.h>
#define UNUSED(x) ((void)(x))
#define FM_OPERATION_AVAILABLE 1
static unsigned OTA_APP_Context, FM_WriteStatus, size_left, address_offset, accepted_end;
static bool upload_accepted;
static volatile unsigned erase_calls, init_calls;
void OTA_Init(void) {init_calls++;}
void DeleteSlot(uint8_t page) {erase_calls++;}
'''+ota[init_start:init_end]+'''
unsigned check_init(void) {erase_calls=init_calls=0;OTA_APP_Init();return erase_calls || init_calls!=1;}
'''
with tempfile.TemporaryDirectory(prefix='boot-image-guard-') as d:
 d=Path(d);(d/'guard.c').write_text(source);elf=d/'guard.elf'
 subprocess.run([a.cc,'-mcpu=cortex-m3','-mthumb','-O1','-nostdlib',str(d/'guard.c'),'-Wl,-Ttext=0x10000,-e,check','-o',str(elf)],check=True)
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB);u.mem_map(0x10000,0x80000);u.mem_map(0x08000000,0x100000);u.mem_map(0x20000000,0x20000)
 with open(elf,'rb') as f:
  e=ELFFile(f)
  for segment in e.iter_segments():
   if segment['p_type']=='PT_LOAD' and segment['p_filesz']:u.mem_write(segment['p_vaddr'],segment.data())
  entry=next(x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols() if x.name=='check')
  init_entry=next(x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols() if x.name=='check_init')
 def check(page,entry_point=entry):
  u.reg_write(UC_ARM_REG_SP,0x2001f000);u.reg_write(UC_ARM_REG_LR,0x80001);u.reg_write(UC_ARM_REG_R0,page)
  u.emu_start(entry_point|1,0x80000,count=10000);assert u.reg_read(UC_ARM_REG_PC)==0x80000;return u.reg_read(UC_ARM_REG_R0)
 assert check(0,init_entry)==0, 'OTA initialization erased persistent images'
 app=(ROOT/'STM32CubeIDE/Debug/BleServoController.bin').read_bytes()
 u.mem_write(0x08006000,app);u.mem_write(0x08080000,app)
 assert check(3)==1 and check(64)==1
 assert all(check(p)==0 for p in (0,1,2,4,63,65,124,125,127,255))
 for tag in (0xffffffff,0xffff0000,0x08000100,0x08006163,0x08090000):
  u.mem_write(0x08080160,struct.pack('<I',tag));assert check(64)==0
 u.mem_write(0x08080000,bytes([255])*8192);u.mem_write(0x08080000,struct.pack('<4I',0x31474d49,1,91384,5))
 assert check(64)==0 and check(3)==1
 print('PASS: OTA startup preserves Flash; actual firmware accepted in both slots; image catalog, arbitrary pages and malformed tag pointers rejected')
