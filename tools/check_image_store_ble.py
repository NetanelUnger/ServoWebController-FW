"""Hardware round-trip check in slot 5; refuses to overwrite existing content.
Requires bleak. --verify-existing checks the exact test pattern after a reset,
shows it and deletes it. Does not erase other slots or perform an OTA update.
"""
import argparse, asyncio, json, struct, time, zlib
from pathlib import Path
from bleak import BleakClient,BleakScanner
SERVICE='91bad4d0-b950-4226-aa2b-4ede9fa42f59'
COMMAND=SERVICE.replace('d0-','d1-');DATA=SERVICE.replace('d0-','d2-');STATUS=SERVICE.replace('d0-','d3-')
def pattern():
 b=bytearray()
 for y in range(240):
  dy=119-y if y<120 else y-120
  left=0
  while (119-left)**2+dy*dy>=14400:left+=1
  for x in range(left,240-left):b.extend(struct.pack('>H',((x*31//239)<<11)|((y*63//239)<<5)|15))
 return bytes(b)
async def main(a):
 d=await BleakScanner.find_device_by_address(a.address,timeout=15)
 if not d:raise RuntimeError('Board not advertising; disconnect the Web client')
 async with BleakClient(d,winrt={'use_cached_services':False}) as c:
  if c.services.get_characteristic(COMMAND) is None:raise RuntimeError('Image service unavailable in Windows discovery')
  seq=0
  async def wait(op,slot,token):
   deadline=time.monotonic()+60
   while time.monotonic()<deadline:
    p=bytes(await c.read_gatt_char(STATUS))
    if len(p)>=16 and p[2:4]==bytes([op,slot]) and struct.unpack_from('<H',p,4)[0]==token:
     if p[1]==3:raise RuntimeError('Image operation failed: '+p.hex())
     if p[1]==2:return p
    await asyncio.sleep(.015)
   raise TimeoutError('Image worker did not finish')
  async def cmd(op,slot=5,*values):
   nonlocal seq
   seq+=1
   await c.write_gatt_char(COMMAND,struct.pack('<BBH',op,slot,seq)+b''.join(struct.pack('<I',v) for v in values),response=True)
   return await wait(op,slot,seq)
  expected=pattern();report={'mtu':c.mtu_size,'bytes':len(expected),'checks':[]}
  mask=(await cmd(6,0))[7]
  if not a.verify_existing:
   if mask&16:raise RuntimeError('Slot 5 occupied; not overwriting it')
   start=time.monotonic();await cmd(1,5,len(expected),zlib.crc32(expected))
   for offset in range(0,len(expected),224):
    seq+=1
    await c.write_gatt_char(DATA,struct.pack('<HH',seq,offset//16)+expected[offset:offset+224],response=True)
    p=await wait(8,5,seq)
    assert struct.unpack_from('<I',p,8)[0]==min(offset+224,len(expected))
    if offset%11200==0:print('uploaded',offset,flush=True)
   await cmd(2);report['upload_seconds']=round(time.monotonic()-start,2);report['checks'].append('upload_commit')
  else:assert mask&16,'Test image not retained across reset'
  actual=bytearray();start=time.monotonic()
  for offset in range(0,len(expected),224):
   p=await cmd(5,5,offset);assert struct.unpack_from('<I',p,8)[0]==offset
   actual.extend(p[16:])
  assert actual==expected,'Readback mismatch'
  report['read_seconds']=round(time.monotonic()-start,2);report['checks'].append('readback_exact')
  start=time.monotonic();await cmd(3);report['show_seconds']=round(time.monotonic()-start,3);report['checks'].append('show_completed')
  if a.verify_existing:
   await cmd(4);assert (await cmd(6,0))[7]==(mask&~16);report['checks']+=['survives_reset','delete_slot5_preserves_others']
  report['status']='PASS';print(json.dumps(report),flush=True)
  if a.output:Path(a.output).write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--address',default='00:80:E1:2A:26:24');p.add_argument('--verify-existing',action='store_true');p.add_argument('--output');asyncio.run(main(p.parse_args()))
