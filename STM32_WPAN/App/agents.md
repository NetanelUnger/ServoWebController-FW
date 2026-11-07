# FW BLE App Agent Notes

This folder owns BLE service definitions and STM32 WPAN application glue. It mixes generated ST service code with project-specific service behavior, so keep edits focused and preserve `USER CODE` regions where present.

## Module Map

- `app_ble.c/.h`: BLE stack app setup, advertising data, connection/disconnection events, and service initialization.
- `nus.c/.h`: Nordic UART Service definition.
- `nus_app.c/.h`: NUS app state, notification enable state, TX notification retries, and command RX/TX task helpers.
- `tof.c/.h`: custom TOF BLE service definition and distance-map characteristic.
- `tof_app.c/.h`: TOF app state, notification enable state, and distance-map notification helper.
- `dis*` and `ota*`: Device Information Service and OTA support.

## BLE Contract

- Advertised NUS service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
- NUS servo-data characteristic UUID: `6E400004-B5A3-F393-E0A9-E50E24DCCA9E`.
- NUS servo-data payload is 16 little-endian `uint16` duty-cycle values, two bytes per servo channel, channel 0 first and channel 15 last.
- TOF service UUID: `91BAD492-B950-4226-AA2B-4EDE9FA42F59`.
- TOF distance-map notify characteristic UUID: `91BAD493-B950-4226-AA2B-4EDE9FA42F59`.
- TOF distance-map notifications must remain exactly 64 bytes unless Web decoding and docs are updated at the same time.
- `ADV_NAME` comes from `config.h`; with the current advertising payload layout it must fit the static assertion in `app_ble.c`.

## NUS Flow

- NUS RX writes should stay lightweight and enqueue data through `NUS_APP_EnqueueRX()`.
- `NusRxTask()` calls `parser_ProcessOne()` and enqueues text replies through `NUS_APP_EnqueueTX()`.
- `NusTxTask()` sends NUS TX notifications only when connected and notifications/indications are enabled.
- Keep command parsing in `comm_interface.c`, not in BLE service callbacks.

## TOF Flow

- `TOF_Init()` creates the custom service and notify characteristic.
- `TOF_Notification()` tracks CCCD notification enable/disable events.
- `TOF_APP_IsNotificationEnabled()` gates sensor ranging in `vl53l5cx_task.c`.
- `TOF_APP_NotifyDistanceMap()` validates `length == 64` before calling `TOF_UpdateValue()`.

## Extending BLE

For a new characteristic:

- Add the UUID and characteristic in the service file, usually `tof.c` or a new service pair.
- Add app-level state and helpers in the matching `*_app.c`.
- Wire service init and connection/disconnection events through `app_ble.c` when needed.
- Update the Web `BleProvider` to request the service and resolve the characteristic.
- Keep BLE callbacks short; move parsing, retries, and long work into RTOS tasks.

## Display service

`display_service.c` exposes the v1 binary rectangle service. Its bounded synchronous
write path borrows the event buffer only until callback return. Use ordinary writes
with response; do not queue the pointer. Contract and Web integration:
`STM32CubeIDE/Application/User/Core/display/README.md`. Web helpers own packet
chunking and size-rejection fallback. The first display write per connection
requests a 7.5-15 ms interval and 251-byte data length; central acceptance is
not guaranteed. Preserve acknowledged drawing and the borrowed-buffer lifetime.

File storage service d0/d1/d2/d3 lives in image_service.c. The display worker owns
image_store.c / file_store.inc Flash jobs; BLE callbacks only copy one bounded request and return.
Never bypass ImageStore_ClaimOTA before firmware-slot erasure. Status READ is
polled for durable completion; ATT write acceptance alone is not success.
See display/FILE_STORE.md for the v2 map, protocol, recovery and legacy migration;
display/README.md retains the v1 protocol. OTA must invalidate both v2 catalogs.

OTA_APP_Init must never erase the download slot: that region also stores images.
Erasure belongs only to an accepted OTA upload after ImageStore_ClaimOTA, or
the bootloader installation/recovery paths. Test image persistence across a real
board reset after changing OTA initialization or generated startup code.


Advertising identity: ADV_NAME_PREFIX plus six UID-derived digits (8-byte full
name). DeviceIdentity_Format must initialize GAP name before GAP setup; advertising
copies that same name. Keep the 31-byte name + NUS UUID + stack Flags assertion. Forward both
successful connection-event types and matching disconnect to Display_SetBleConnected;
this callback-safe function only schedules work. Never draw in BLE event context.
