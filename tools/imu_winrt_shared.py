"""Windows shared GATT adapter for IMU tests when another client owns the device.
Uses the same WinRT dependencies as Bleak, without modifying the installed library.
"""
import asyncio
from winrt.windows.devices.bluetooth import BluetoothLEDevice, BluetoothCacheMode
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattSharingMode, GattWriteOption, GattCommunicationStatus,
    GattClientCharacteristicConfigurationDescriptorValue as Cccd,
)
from winrt.windows.storage.streams import Buffer

class SharedImuClient:
    def __init__(self,address,service_uuid='91bad4a0-b950-4226-aa2b-4ede9fa42f59'):
        self.service_uuid=service_uuid
        self.service_uuids={service_uuid} if isinstance(service_uuid,str) else set(service_uuid)
        self.address=address; self.device=None; self.service=None; self.services=[]; self.chars={}; self.tokens={}
    async def __aenter__(self):
        self.device=await BluetoothLEDevice.from_bluetooth_address_async(int(self.address.replace(':',''),16))
        result=await self.device.get_gatt_services_async(BluetoothCacheMode.UNCACHED)
        for service in result.services:
            if str(service.uuid) in self.service_uuids: self.services.append(service)
            else: service.close()
        assert {str(s.uuid) for s in self.services}==self.service_uuids, 'GATT services missing'
        self.service=self.services[0]
        for service in self.services:
            await service.request_access_async()
            await service.open_async(GattSharingMode.SHARED_READ_AND_WRITE)
            result=await service.get_characteristics_async(BluetoothCacheMode.UNCACHED)
            assert result.status==GattCommunicationStatus.SUCCESS,result.status
            self.chars.update({str(c.uuid):c for c in result.characteristics})
        return self
    async def __aexit__(self,*args):
        for uuid in list(self.tokens): await self.stop_notify(uuid)
        self.chars.clear()
        for service in self.services: service.close()
        self.services.clear(); self.service=None
        if self.device: self.device.close()
    async def start_notify(self,uuid,callback):
        c=self.chars[uuid]; loop=asyncio.get_running_loop()
        self.tokens[uuid]=c.add_value_changed(lambda sender,args:loop.call_soon_threadsafe(callback,sender,bytes(args.characteristic_value)))
        result=await c.write_client_characteristic_configuration_descriptor_async(Cccd.NOTIFY)
        assert result==GattCommunicationStatus.SUCCESS,result
    async def stop_notify(self,uuid):
        c=self.chars[uuid]
        await c.write_client_characteristic_configuration_descriptor_async(Cccd.NONE)
        c.remove_value_changed(self.tokens.pop(uuid))
    async def write_gatt_char(self,uuid,data,response=True):
        buf=Buffer(len(data)); buf.length=len(data)
        memoryview(buf)[:]=data
        result=await self.chars[uuid].write_value_with_result_async(buf,GattWriteOption.WRITE_WITH_RESPONSE)
        if result.status!=GattCommunicationStatus.SUCCESS: raise RuntimeError((result.status,result.protocol_error))
    async def read_gatt_char(self,uuid):
        result=await self.chars[uuid].read_value_async(BluetoothCacheMode.UNCACHED)
        assert result.status==GattCommunicationStatus.SUCCESS,result.status
        return bytes(result.value)
