"""Small servo steps under simultaneous ADC/IMU/TOF traffic; restores starting targets.
Checks commanded values AND actual PCA9685 PWM registers, without claiming motion.
"""
import argparse, asyncio, struct, time
from bleak import BleakClient, BleakScanner
S='6e400004-b5a3-f393-e0a9-e50e24dcca9e'
RX='6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX='6e400003-b5a3-f393-e0a9-e50e24dcca9e'
ADC='91bad4b1-b950-4226-aa2b-4ede9fa42f59'
IMU='91bad4a1-b950-4226-aa2b-4ede9fa42f59'
MODE='91bad4a3-b950-4226-aa2b-4ede9fa42f59'
TOF='91bad493-b950-4226-aa2b-4ede9fa42f59'

async def main(address,winrt_shared=False):
 if winrt_shared:
  from imu_winrt_shared import SharedImuClient
  client=SharedImuClient(address,['6e400001-b5a3-f393-e0a9-e50e24dcca9e',
   '91bad4a0-b950-4226-aa2b-4ede9fa42f59','91bad4b0-b950-4226-aa2b-4ede9fa42f59',
   '91bad492-b950-4226-aa2b-4ede9fa42f59'])
 else:
  d=await BleakScanner.find_device_by_address(address,timeout=15)
  assert d,'No advertisement'
  client=BleakClient(d,winrt={'use_cached_services':False})
 async with client as c:
  replies=asyncio.Queue(); counts={ADC:0,IMU:0,TOF:0};latencies=[]
  await c.start_notify(TX,lambda _,v:replies.put_nowait(bytes(v).decode()))
  async def command(text):
   await c.write_gatt_char(RX,text.encode(),response=True)
   return await asyncio.wait_for(replies.get(),4)
  assert await command('STOP')=='OK STOP'
  for n in ['EN1','EN2','DIAG_EN1','DIAG_EN2','OE']:
   response=await command('GPIO_GET '+n)
   assert response==f'OK GPIO_GET {n} 1',response
  baseline=list(struct.unpack('<16H',await c.read_gatt_char(S)))
  def received(uuid):
   def handle(_,data):counts[uuid]+=1
   return handle
  try:
   for uuid in counts:await c.start_notify(uuid,received(uuid))
   await c.write_gatt_char(MODE,b'\x01',response=True)
   for k in range(40):
    # Distinct targets detect channel swaps or a torn multi-motor command.
    expected=[min(1900,max(600,v+((20+4*i) if k%2==0 else -(20+4*i)))) for i,v in enumerate(baseline)]
    start=time.monotonic()
    await c.write_gatt_char(S,struct.pack('<16H',*expected),response=True)
    latencies.append((time.monotonic()-start)*1000)
    await asyncio.sleep(.025)
    actual=list(struct.unpack('<16H',await c.read_gatt_char(S)))
    assert actual==expected,('Overwritten command',k,expected,actual)
   # Allow pending PWM writes to settle, then read the PCA registers themselves.
   await asyncio.sleep(.15)
   for i in range(16):
    response=await command(f'PWM_GET {i}')
    fields=response.split(); assert fields[:3]==['OK','PWM_GET',str(i)],response
    on,off=map(int,fields[3:]);want=(expected[i]*50*4096+500000)//1000000
    assert on==0 and off==want,('PCA mismatch',i,want,response)
    print(f'CH{i}: command={expected[i]} us; PCA ON={on} OFF={off}',flush=True)
   # A timed move must progress, and an immediate command must cancel it.
   target=1600 if expected[0]<1500 else 1400
   assert (await command(f'SET_TIMING 0 {target} 1500')).startswith('OK SET_TIMING')
   await asyncio.sleep(.2)
   moving=struct.unpack('<16H',await c.read_gatt_char(S))[0]
   assert min(expected[0],target) < moving < max(expected[0],target),('Timed interpolation',moving)
   assert (await command('SET_DIRECT 0 1500')).startswith('OK SET_DIRECT')
   await asyncio.sleep(1.6)
   assert struct.unpack('<16H',await c.read_gatt_char(S))[0]==1500,'Cancelled move resumed'
   assert (await command('PWM_GET 0'))=='OK PWM_GET 0 0 307'
   print('PASS timed interpolation and cancellation by immediate command',flush=True)
   deadline=time.monotonic()+35
   while not all(counts.values()) and time.monotonic()<deadline:await asyncio.sleep(.1)
   assert all(counts.values()),counts
   print('PASS 40 atomic 16-channel command frames and CH0..15 hardware readback under sensor load',flush=True)
   print('Notification counts',counts,'write latency median/max ms',sorted(latencies)[len(latencies)//2],max(latencies),flush=True)
  finally:
   await c.write_gatt_char(S,struct.pack('<16H',*baseline),response=True)
   await c.write_gatt_char(MODE,b'\x00',response=True)
   for uuid in counts:await c.stop_notify(uuid)
   await c.stop_notify(TX)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--address',default='00:80:E1:2A:26:24')
 p.add_argument('--winrt-shared',action='store_true');args=p.parse_args()
 asyncio.run(main(args.address,args.winrt_shared))
