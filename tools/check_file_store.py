"""Execute v2 storage C under ARM emulation, including power-cut recovery.
Uses installed unicorn + pyelftools. Never connects to or erases real hardware.
"""
import argparse, json, struct, subprocess, tempfile, zlib, sys
sys.dont_write_bytecode = True
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
from check_image_store import Machine, FILES, APP, BASE, SIZE

class Store(Machine):
 def __init__(self, elf, flash=None):
  super().__init__(elf, flash)
  self.call('ImageStore_Process')
 def cmd(self, op, id=0, args=(), extra=b'', type=0, error=0):
  self.seq += 1
  p=struct.pack('<BBHI',op,type,self.seq,id)+b''.join(struct.pack('<I',a) for a in args)+extra
  assert self.submit(p)==0,(op,'rejected',self.status())
  self.call('ImageStore_Process');s=self.status()
  assert s[0]==2 and s[2]==op and s[3]==error,(op,s)
  assert s[1]==(3 if error else 2)
  return s
 def format(self):self.cmd(0x2a,args=(0x46534f4b,))
 def files(self):
  result=[]
  for i in range(65):
   s=self.cmd(0x25,args=(i,))
   if len(s)==32:return result
   p=s[32:];v=struct.unpack_from('<6I2H4B',p)
   result.append(dict(zip(('id','offset','length','capacity','crc','revision','width','height','type','layout','state','version'),v)))
  raise AssertionError('Unbounded list')
 def begin(self,pixels,w,h,type=2,id=0,layout=0,error=0,plan=False):
  s=self.cmd(0x29 if plan else 0x20,id,(len(pixels),zlib.crc32(pixels)),bytes((w,h,layout,1)),type,error)
  return struct.unpack_from('<I',s,8)[0]
 def data(self,pixels,at=0,error=0):
  for offset in range(at,len(pixels),224):
   self.seq+=1
   assert self.submit(struct.pack('<HH',self.seq,offset//16)+pixels[offset:offset+224],True)==0
   self.call('ImageStore_Process');assert self.status()[3]==error,self.status()
   if error:break
 def save(self,pixels,w,h,type=2,id=0,layout=0,name=''):
  id=self.begin(pixels,w,h,type,id,layout)
  name=name.encode()
  for at in range(0,len(name),8):self.cmd(0x2b,id,(at,),name[at:at+8])
  self.data(pixels);self.cmd(0x21,id);return id
 def read(self,id):
  f=next(f for f in self.files() if f['id']==id);out=bytearray()
  for at in range(0,f['length'],208):
   s=self.cmd(0x24,id,(at,));assert struct.unpack_from('<I',s,12)[0]==at;out.extend(s[32:])
  assert zlib.crc32(out)==f['crc'];return bytes(out)

def verify_timelines(elf):
 # Real storage C, simulated flash only. No hardware operations.
 m=Store(elf);m.format()
 def plan(events=(),duration=0,repeat=0):
  source=b'{"name":"test","commands":[]}'
  return struct.pack('<IHHIIIIII',0x314c4d54,1,32,len(events),duration,repeat,len(source),0,0)+b''.join(struct.pack('<IIHBBI',*e,0) for e in events)+source
 image=m.save(b'\x15'*16,4,4)
 payload=plan(((0,0,1500,0,0),(0,100,2000,0,1)),100,50)
 file=m.save(payload,0,0,type=3,name='timeline')
 assert m.read(file)==payload and m.read(image)==b'\x15'*16
 assert struct.unpack_from('<I',m.cmd(0x26),40)[0]==14
 m.cmd(0x22,file,error=0x89)
 assert m.call('ImageStore_AcquireTimeline',file,0x20017000,0x20017004)==1
 assert struct.unpack('<I',m.uc.mem_read(0x20017004,4))[0]==len(payload)
 before=m.flash()
 for op,args in ((0x23,()),(0x2a,(0x46534f4b,)),(0x26,())):
  packet=struct.pack('<BBHI',op,0,99,file)+b''.join(struct.pack('<I',v) for v in args)
  assert m.submit(packet)==0x81
 assert m.call('ImageStore_ClaimOTA')==0 and m.flash()==before
 assert m.call('ImageStore_AcquireTimeline',file,0x20017000,0x20017004)==0
 m.call('ImageStore_ReleaseTimeline');assert m.read(file)==payload
 reboot=Store(elf,m.flash());assert reboot.read(file)==payload
 assert reboot.call('ImageStore_AcquireTimeline',image,0x20017000,0x20017004)==0
 for data in (plan(),plan(repeat=1000),plan(((0,0xffffffff,2500,15,1),),0xffffffff)):
  m.save(data,0,0,type=3)
 invalid=[plan(((0,0,499,0,0),)),plan(((0,0,1500,16,0),)),
          plan(((0,100,2000,0,1),(50,0,1700,0,0)),100),
          plan(((0,100,2000,0,1),(50,100,1700,0,1)),150),
          plan(((0,0,1500,0,0),(0,0,1700,0,0)))]
 for data in invalid:
  bad=m.begin(data,0,0,type=3);m.data(data);m.cmd(0x21,bad,error=0x80)
 assert m.read(image)==b'\x15'*16 and m.read(file)==payload
 descriptor=next(f for f in m.files() if f['id']==file)
 m.uc.mem_write(descriptor['offset']+32,b'\x01')
 assert m.call('ImageStore_AcquireTimeline',file,0x20017000,0x20017004)==0
 m.cmd(0x26)
 print('PASS: timeline/image coexistence, round trip, validation, flash/OTA lease and CRC')

def verify(elf):
 verify_timelines(elf)
 m=Store(elf);assert m.cmd(0x26)[6]==0
 m.format();assert m.files()==[]
 a=bytes((i*13)&255 for i in range(31*17));b=bytes((i*7)&255 for i in range(33*19));c=b'\x96'*(23*21)
 first=m.save(a,31,17,name='תמונה א');middle=m.save(b,33,19);last=m.save(c,23,21)
 assert [f['id'] for f in m.files()]==[first,middle,last]
 assert m.read(first)==a and m.read(middle)==b and m.read(last)==c
 assert m.variable('bad_flash')==0
 m.begin(b,33,19,id=middle,error=0x8b)
 before=m.flash();m.begin(a,31,17,plan=True);assert m.flash()==before
 m.cmd(0x23,middle);hole=next(f for f in m.files() if f['id']==middle)
 assert hole['capacity']==640 and hole['state']==3
 m.begin(b'\x12'*800,40,20,id=middle,error=0x8b)
 small=b'\x34'*(13*17);replacement=m.save(small,13,17,id=middle)
 assert replacement!=middle and m.read(first)==a and m.read(last)==c and m.read(replacement)==small
 assert next(f for f in m.files() if f['id']==replacement)['capacity']==640
 m.cmd(0x22,replacement);assert m.variable('shows')==1
 reboot=Store(elf,m.flash());assert reboot.read(first)==a and reboot.read(replacement)==small
 # Larger and smaller last-file replacements preserve identity and the preceding page.
 grow=b'\xa3'*(99*101);assert m.save(grow,99,101,id=last)==last
 assert m.read(first)==a and m.read(replacement)==small
 assert m.save(c,23,21,id=last)==last
 m.cmd(0x23,last);m.cmd(0x23,replacement);assert len(m.files())==1
 m.cmd(0x23,first);assert m.files()==[] and struct.unpack_from('<I',m.cmd(0x26),24)[0]==464*1024
 # Reserved capacity is not chosen automatically; explicit use releases its identity.
 reserve=m.cmd(0x2c,args=(4096,));reserved=struct.unpack_from('<I',reserve,8)[0]
 other=m.save(c,23,21);rid=m.save(a,31,17,id=reserved)
 assert rid!=reserved and m.read(other)==c
 # Incomplete uploads and owner disconnect cannot publish partial bytes.
 pending=m.begin(b,33,19);m.data(b[:224]);assert m.call('ImageStore_ClaimOTA')==0
 assert m.submit(struct.pack('<BBHI',0x26,0,99,0),conn=2)==0x81
 disconnected=Store(elf,m.flash());assert pending not in [f['id'] for f in disconnected.files() if f['state']==1]
 m.call('ImageStore_Disconnect',1);m.call('ImageStore_Process');assert m.files()==disconnected.files()
 bad=m.begin(b,33,19);m.data(b'\0'*len(b));m.cmd(0x21,bad,error=0x86)
 invalid_name=m.begin(c,23,21);m.cmd(0x2b,invalid_name,(0,),b'\xc0\x80');m.data(c)
 m.cmd(0x21,invalid_name,error=0x80);m.cmd(0x27,invalid_name)
 # Unknown OTA bytes cannot be formatted or automatically erased.
 raw=bytearray(b'\xff'*SIZE);raw[0]=0
 locked=Store(elf,bytes(raw));locked.cmd(0x2a,args=(0x46534f4b,),error=0x84);assert locked.flash()==bytes(raw)
 # Both catalog copies are invalidated before OTA starts (and after reboot).
 assert m.call('ImageStore_ClaimOTA')==1
 locked=Store(elf,m.flash());assert locked.cmd(0x26)[6]==3
 locked.cmd(0x2a,args=(0x46534f4b,),error=0x84)
 print('PASS: v2 round trips, RGB formats, names, allocation, stable ids, holes, reserve, tail resize, CRC, reconnect and OTA')
 # Capacity and record count are distinct limits.
 full=Store(elf);full.format();low=bytes((i*5)&255 for i in range(45692))
 for _ in range(10):full.save(low,240,240,layout=1)
 assert len(full.files())==10
 full.begin(low,240,240,layout=1,error=0x8a)
 full.format()
 for _ in range(64):full.save(b'\x09',1,1)
 full.begin(b'\x01',1,1,error=0x8c)
 assert full.variable('bad_flash')==0
 print('PASS: ten 8-bit round images, full-storage rejection, 64-file catalog limit')
 # Snapshot each meaningful stage of a shared-page rewrite. Stop before an FM
 # mutation, reboot actual C against those bytes, and require both neighbours.
 base=Store(elf);base.format();f1=base.save(a,31,17)
 wide=bytes((i*11)&255 for i in range(120*120*2))
 f2=base.save(wide,120,120,type=1);f3=base.save(c,23,21);base.cmd(0x23,f2)
 original=base.flash();probe=Store(elf,original);trace=[]
 def record(uc,address,size,data):
  if address==(probe.sym['FM_Erase']&~1):trace.append(('erase',uc.reg_read(UC_ARM_REG_R0)))
  else:trace.append(('write',uc.reg_read(UC_ARM_REG_R1)))
 for name in ('FM_Write','FM_Erase'):
  addr=probe.sym[name]&~1;probe.uc.hook_add(UC_HOOK_CODE,record,begin=addr,end=addr)
 probe.begin(small,13,17,id=f2)
 cuts={0,len(trace)-1}
 for index,(kind,address) in enumerate(trace):
  if kind=='erase' or address in (BASE+6656,BASE+8192+6656,0x08086000,0x08086010):
   cuts.update((index,min(index+1,len(trace)-1)))
 for cut in sorted(cuts):
  trial=Store(elf,original);count=[0]
  def stop(uc,address,size,data):
   if count[0]==cut:uc.emu_stop()
   count[0]+=1
  for name in ('FM_Write','FM_Erase'):
   addr=trial.sym[name]&~1;trial.uc.hook_add(UC_HOOK_CODE,stop,begin=addr,end=addr)
  try:trial.begin(small,13,17,id=f2)
  except AssertionError as e:
   assert 'hung' in str(e),e
  recovered=Store(elf,trial.flash())
  assert recovered.read(f1)==a and recovered.read(f3)==c,(cut,trace[cut])
  assert recovered.variable('bad_flash')==0
 print(f'PASS: {len(cuts)} power cuts around catalog commits, scratch backup, shared-page erase and neighbour restoration')
 return {'status':'passed','power_cut_points':len(cuts),'hardware_tested':False}

def main():
 p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='file-store-test-') as d:
  d=Path(d)
  for name,content in FILES.items():
   if name=='stubs.c':
    content=content.replace('unsigned bad_flash,', 'static uint8_t programmed[0x7a000/16];\nunsigned bad_flash,')
    content=content.replace('memcpy(d,s,n);cb->Callback', 'for(unsigned i=0;i<n;i+=16){unsigned k=(a-0x08080000+i)/16;if(programmed[k]){bad_flash++;return FM_ERROR;}programmed[k]=1;}\n memcpy(d,s,n);cb->Callback')
    content=content.replace('memset((void*)(0x08000000+page*8192),255,n*8192);', 'memset(programmed+(page-64)*512,0,n*512);\n memset((void*)(0x08000000+page*8192),255,n*8192);')
   (d/name).write_text(content)
  elf=d/'store.elf'
  subprocess.run([a.cc,'-mcpu=cortex-m3','-mthumb','-O1','-g','-Wall','-Wextra','-Wno-unused-parameter','-ffreestanding','-nostdlib','-I'+str(d),'-I'+str(APP),str(APP/'display/image_store.c'),str(d/'stubs.c'),'-Wl,-Ttext=0x10000,-Tdata=0x20000000,-e,ImageStore_Init','-lgcc','-o',str(elf)],check=True)
  result=verify(elf)
  if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
