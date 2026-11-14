"""Stop an uploaded counter on real link loss. --run cycles Windows Bluetooth.
Uses shared WinRT access; writes/deletes only its new temporary program ID.
"""
import argparse, asyncio, json, re, struct, zlib
from pathlib import Path
from imu_winrt_shared import SharedImuClient
from ble_radio_cycle import cycle_bluetooth
from check_file_store_hardware import COMMAND, DATA, STATUS, RX, TX

async def check(args, report):
    client=SharedImuClient(args.address,['6e400001-b5a3-f393-e0a9-e50e24dcca9e','91bad4d0-b950-4226-aa2b-4ede9fa42f59'])
    replies=asyncio.Queue();seq=3000;file_id=0;opened=False
    async def connect():
        nonlocal opened
        await client.__aenter__();opened=True
        await client.start_notify(TX,lambda _,p:replies.put_nowait(bytes(p).decode()))
    async def nus(text):
        assert replies.empty()
        await client.write_gatt_char(RX,text.encode());answer=''
        while True:
            answer+=await asyncio.wait_for(replies.get(),10)
            if text!='WASM_STATUS' or answer.endswith('END'):return answer
    async def status():
        text=await nus('WASM_STATUS')
        fields={k:int(v) for k,v in re.findall(r'(id|n|r|calls|pool|clean|stack|max_ms|stop_ms|imu|tof)=(-?\d+)',text)}
        fields['state']=re.match(r'OK WASM_STATUS (IDLE|QUEUED|LOADING|RUNNING|DONE|STOPPED|TRAP|ERROR)',text)[1]
        fields['raw']=text;return fields
    async def wait(op,token):
        for _ in range(200):
            value=await client.read_gatt_char(STATUS)
            if len(value)>=32 and value[2]==op and struct.unpack_from('<H',value,4)[0]==token:
                assert value[1]!=3,value.hex()
                if value[1]==2:return value
            await asyncio.sleep(.02)
        raise TimeoutError('Storage response')
    async def cmd(op,id=0,values=(),extra=b'',kind=0):
        nonlocal seq
        seq+=1
        await client.write_gatt_char(COMMAND,struct.pack('<BBHI',op,kind,seq,id)+b''.join(struct.pack('<I',v) for v in values)+extra)
        return await wait(op,seq)
    try:
        await connect();assert await nus('STOP')=='OK STOP'
        payload=args.module.read_bytes()
        begin=await cmd(0x20,values=(len(payload),zlib.crc32(payload)),extra=b'\0\0\0\1',kind=5)
        file_id=struct.unpack_from('<I',begin,8)[0];report['created']=file_id
        await cmd(0x2b,file_id,(0,),b'VM LINK')
        for at in range(0,len(payload),16):
            seq+=1;await client.write_gatt_char(DATA,struct.pack('<HH',seq,at//16)+payload[at:at+16]);await wait(0x28,seq)
        await cmd(0x21,file_id)
        assert await nus(f'WASM_FILE {file_id}')==f'OK WASM_FILE {file_id}'
        await asyncio.sleep(1);before=await status();assert before['state']=='RUNNING',before
        report['before']=before
        await client.__aexit__();opened=False
        await cycle_bluetooth()
        await connect();after=await status();report['after']=after
        assert after['state']=='STOPPED' and after['clean']==24,after
        await asyncio.sleep(.3);again=await status();assert again['n']==after['n'],again
        print('PASS uploaded counter stopped on physical BLE link loss',flush=True)
    finally:
        if not opened:await connect()
        try:
            await nus('STOP');await cmd(0x27)
            if file_id:await cmd(0x23,file_id)
            records=[]
            for index in range(65):
                value=await cmd(0x25,values=(index,))
                if len(value)==32:break
                v=struct.unpack_from('<6I2H4B',value,32)
                records.append(dict(zip(('id','offset','length','capacity','crc','revision','width','height','type','layout','state','version'),v)))
            report['files']=records
        finally:await client.__aexit__()
    report['status']='passed'

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true');p.add_argument('--address',default='00:80:E1:2A:26:24');p.add_argument('--module',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
    if not a.run:p.error('--run required; interrupts Bluetooth links')
    report={'status':'running'}
    try:asyncio.run(check(a,report))
    except BaseException as error:report.update(status='failed',error=repr(error));raise
    finally:a.output.write_text(json.dumps(report,indent=2)+'\n')
