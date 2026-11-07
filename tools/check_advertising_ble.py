"""Check current advertisements, GAP name, NUS ping, and advertising after disconnect."""
import asyncio
import json
import re
from bleak import BleakClient, BleakScanner

NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
NAME = '00002a00-0000-1000-8000-00805f9b34fb'

async def scan(address=None):
    found = await BleakScanner.discover(timeout=8, return_adv=True)
    candidates = [(device, adv) for device, adv in found.values()
                  if NUS in adv.service_uuids and re.fullmatch(r'NL\d{6}', adv.local_name or '')
                  and (address is None or device.address == address)]
    print('Advertisements:', [(d.address, a.local_name) for d,a in candidates], flush=True)
    assert len(candidates) == 1, 'Expected one matching device; specify target if multiple boards are present'
    return candidates[0]

async def main():
    device, adv = await scan()
    replies = asyncio.Queue()
    async with BleakClient(device, timeout=20, winrt={'use_cached_services': False}) as client:
        name = bytes(await client.read_gatt_char(NAME)).decode()
        assert name == adv.local_name, (name, adv.local_name)
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data)))
        await client.write_gatt_char(RX, b'PING', response=True)
        reply = await asyncio.wait_for(replies.get(), 5)
        assert reply.strip() == b'OK PING', reply
        print('Connected, GAP name matched, PING passed:', name, flush=True)
    _, after = await scan(device.address)
    assert after.local_name == name
    print(json.dumps({'status':'PASS','address':device.address,'name':name,
                      'checks':['advertised_name_and_nus','gap_name','connect_ping','advertise_after_disconnect']}), flush=True)

if __name__ == '__main__':
    asyncio.run(main())
