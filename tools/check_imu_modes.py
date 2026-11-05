"""On-board RAW/SFLP/OFF transitions and control validation. No servo writes."""
import argparse, asyncio, math, struct
from bleak import BleakClient, BleakScanner
DATA='91bad4a1-b950-4226-aa2b-4ede9fa42f59'
MODE='91bad4a3-b950-4226-aa2b-4ede9fa42f59'
async def main(address,shared=False):
    if shared:
        from imu_winrt_shared import SharedImuClient
        client=SharedImuClient(address)
    else:
        d=await BleakScanner.find_device_by_address(address,timeout=15)
        assert d,'No advertisement'
        client=BleakClient(d,winrt={'use_cached_services':False})
    async with client as c:
        q=asyncio.Queue()
        await c.start_notify(DATA,lambda _,b:q.put_nowait(bytes(b)))
        print('Subscribed to IMU',flush=True)
        for invalid in [b'\x03',b'\xff']:
            print('Reject invalid',invalid.hex(),flush=True)
            try: await c.write_gatt_char(MODE,invalid,response=True)
            except Exception: pass
            else: raise AssertionError('Invalid mode accepted '+invalid.hex())
        try:
            for mode in [1,2,0,2,1,0]:
                print('Request mode',mode,flush=True)
                while not q.empty(): q.get_nowait()
                await c.write_gatt_char(MODE,bytes([mode]),response=True)
                while True:
                    p=await asyncio.wait_for(q.get(),10)
                    assert len(p)==48
                    if p[0]==3:
                        assert p[1]==0,('Mode failed',p.hex())
                        if p[2]==mode and p[3]==mode: break
                assert bytes(await c.read_gatt_char(MODE))==bytes([mode])
                if mode==0:
                    try:
                        while True:
                            p=await asyncio.wait_for(q.get(),0.6)
                            assert p[0]==3, 'Samples after OFF acknowledgement'
                    except asyncio.TimeoutError: pass
                else:
                    for i in range(12):
                        p=await asyncio.wait_for(q.get(),6)
                        assert p[0]==mode,p.hex()
                        assert struct.unpack_from('<I',p,40)[0]==0,p.hex()
                        if mode==2:
                            quat=struct.unpack_from('<4e',p,16)
                            norm=math.sqrt(sum(x*x for x in quat))
                            assert math.isfinite(norm) and 0.95<norm<1.05,(quat,norm)
                            gravity=[v*0.000061 for v in struct.unpack_from('<3h',p,24)]
                            assert 0.8<math.sqrt(sum(v*v for v in gravity))<1.2,gravity
                            # Both SFLP bases now use (X,-Y,-Z), so reference
                            # gravity is -Z. Verify quaternion/vector agreement
                            # at any board pose, not just the sign at rest.
                            w,x,y,z=(v/norm for v in quat)
                            expected_gravity=(2*(w*y-x*z),-2*(w*x+y*z),-1+2*(x*x+y*y))
                            mismatch=math.sqrt(sum((a-b)**2 for a,b in zip(gravity,expected_gravity)))
                            assert mismatch<0.12,('Quaternion/gravity axis mismatch',quat,gravity,mismatch)
                            if i==11: print('SFLP WXYZ',quat,'gravity g',gravity,flush=True)
                print('PASS mode',mode,flush=True)
        finally:
            await c.write_gatt_char(MODE,b'\x00',response=True)
            await asyncio.sleep(0.4)
            await c.stop_notify(DATA)
        print('PASS invalid writes, applied events, readback, RAW/SFLP/OFF transitions')
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--address',required=True)
    a.add_argument('--winrt-shared',action='store_true')
    args=a.parse_args()
    asyncio.run(main(args.address,args.winrt_shared))
