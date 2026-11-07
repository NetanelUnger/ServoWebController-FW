# GC9A01 display and BLE protocol v1

240x240 RGB565 display. Startup draws NatiLab, version and the six-digit device
identifier using Font24, then sleeps until work or a connection event arrives. The existing face animation sources are no
longer referenced by the application and their assets are removed by linker GC.
No framebuffer, image assembly buffer, heap allocation or periodic refresh.
The existing 480-byte scanline is used for startup/text/solid fills only.
The display task retains its static 2048-byte stack for startup/recovery.

| Display signal | STM32 pin |
| --- | --- |
| SCL / SCK | PA0 (SPI3 SCK) |
| SDA / MOSI | PB8 (SPI3 MOSI; not the I2C SDA pin) |
| CS | PA4 |
| DC | PB11 |
| RST (firmware name DISPLAY_RES) | PA2 |

**Hardware wiring correction:** the schematic's RES label at PB11 actually
connects to the purchased module's DC pin; D/C at PA4 connects to CS; CS at PA2
connects to RST. Firmware names refer to the actual module functions. This is
intentional; do not restore the earlier mapping just to match schematic names.
DC means Data/Command: low sends a controller command, high sends its parameters
or pixel data. CS selects the module and RST resets it; both are active low.

SPI3 uses mode 0, MSB first, 8-bit words, software CS and 3.125 MHz. No MISO or
backlight-control GPIO was specified. The module's supply, ground and backlight
must be connected according to its board requirements. SPI transmit success does
not prove that a panel is connected, powered or displaying an image.

## BLE contract (Web integration planned separately)

Service: `91bad4c0-b950-4226-aa2b-4ede9fa42f59`.
Command characteristic: `91bad4c1-b950-4226-aa2b-4ede9fa42f59`, WRITE with response only.
The display uses the dedicated `91bad4c*` range; `91bad4a*` belongs to IMU
and `91bad4b*` to ADC. Do not reuse those ranges for display control.
The service is discovered after connecting using the existing advertised NUS
service; it is not added to the advertising payload.

Protocol v1 uses fixed constants in the client: 240x240 pixels, RGB565 high-byte
first, maximum command length 247 bytes, FILL_RECT up to 240x240, WRITE_RECT up to 120 pixels,
maximum WRITE_ROW length 122 pixels.
The service exposes only Command; no capability discovery is required.

Each ordinary ATT write is one complete, independent command.

### WRITE_ROW (opcode 3)

`03 X Y RGB565 RGB565 ...`: X and Y are one byte each. All remaining bytes
are pixel pairs, high byte first. Pixel count = (packet length - 3) / 2.
At least one pixel is required; odd payload lengths are rejected. Pixels are
written horizontally from (X,Y), with no wrapping to the next row. The entire
command is rejected if X + pixel count > 240 or Y >= 240. No width/height
fields, reassembly, pixel copies or conversion. The borrowed payload at offset
3 goes directly to GC9A01_BlitWire with height 1.

Example: red then green at (10,20): `03 0A 14 F8 00 07 E0`.
With MTU 251, a 247-byte command carries 122 pixels. At the default MTU 23,
a 19-byte command carries 8 pixels. Use floor((min(247, MTU-3)-3)/2)
pixels per command, additionally limited by 240-X. Split full rows as needed.

### Existing rectangle commands


| Byte | Meaning |
| --- | --- |
| 0 | 1 = FILL_RECT; 2 = WRITE_RECT |
| 1 | x, unsigned byte |
| 2 | y, unsigned byte |
| 3 | width, unsigned byte, nonzero |
| 4 | height, unsigned byte, nonzero |
| 5 onward | FILL: exactly one RGB565 color (2 bytes); WRITE: exactly width*height*2 bytes |

Both coordinates and dimensions are in the configured display rotation. All
rectangles must fit within 240x240. FILL_RECT permits the entire screen;
WRITE_RECT contains at most 120 pixels. Pixels are
row-major, top-to-bottom, left-to-right. Every RGB565 color is HIGH BYTE FIRST.
No alpha, compression, palette, sequence counter or frame commit is used.

Examples (hex):

- Red pixel at (10,20): `01 0A 14 01 01 F8 00`.
- Red then green at (0,0): `02 00 00 02 01 F8 00 07 E0`.
- White horizontal span of 120 pixels: `01 00 00 78 01 FF FF`.

A full-width row can use WRITE_ROW spans of 122 and 118 pixels when the
negotiated MTU permits it (or two 120-pixel WRITE_RECT spans). Larger images are immediately visible piece by piece. Filling the
whole screen also requires multiple commands. No atomic frame swap or rollback.
Independent writes are idempotent; resending a span restores that span.
On disconnect the panel retains its last contents; a new client should send a
complete baseline because the firmware does not remember the remote image.
Multiple clients share one panel; UI clients must coordinate ownership externally.

## Flow control and failures

Use write WITH response and await completion before sending the next command.
Successful ATT response means SPI transmission completed, not physical panel
verification (there is no panel readback). Application ATT error codes:

- 0x80: invalid opcode, length, dimensions, bounds or pixel limit; nothing drawn.
- 0x81: startup/lock busy, stopped, or startup faulted; retry with bounded backoff only
  while startup/another operation is in progress. Stopped/faulted requires recovery.
- 0x82: SPI error; some pixels may have changed. The next valid command may retry
  the region; a transient transfer failure does not disable drawing.
- 0x83: compiled with ENABLE_DISPLAY=0.

`Display_Start()` or board reset recovers a stopped/faulted display by
reinitializing and showing the startup mark. `Display_Stop()` and manual display
GPIO writes disable remote drawing until that recovery. These remain firmware
APIs, not new text commands. Normal writes do not reset or clear the panel.

The callback validates before touching SPI, tries the display mutex WITHOUT
waiting, and transmits directly from the event buffer before returning. No event
pointer survives the callback. No payload queue, reassembly or application pixel
copy is involved in WRITE_RECT. The BLE stack/GATT database has its own storage;
this is application-level zero-copy, not a claim about stack internals.

This deliberately uses a small synchronous operation in BLE dispatch. Pixel
payload is at most 244 bytes (~0.625 ms of wire time at 3.125 MHz, plus commands,
HAL overhead and preemption); tall fills add per-row HAL calls. Transfers have a
100 ms per-call fault timeout on this path (startup uses 1000 ms). Real maximum
latency under interrupts must be measured on hardware. HAL time advances in
10 ms steps; a 10 ms deadline can falsely expire at the very next tick even
when less than a millisecond has elapsed. Future DMA or asynchronous
work must retain a valid buffer; never queue this borrowed pointer after returning.

## Future Web implementation checklist

1. Add the display service UUID to requestDevice optionalServices; reconnect with
   a fresh grant when an existing browser grant lacks access.
2. Discover Command; old firmware without the service keeps its
   existing features and reports display control unavailable.
3. Encode RGB565 on the client; send the bytes above via writeValueWithResponse.
4. Web Bluetooth has no portable negotiated-MTU getter. The Web sender tries
   up to 247-byte writes (122 WRITE_ROW pixels or 120 WRITE_RECT pixels).
   A size/unsupported queued-write rejection switches that connection to
   <=20-byte absolute spans: 8 WRITE_ROW or 7 WRITE_RECT pixels. Other errors
   fail the transfer. Native clients use min(247, negotiated ATT_MTU-3).
5. Share the BLE command scheduler with servo operations, yield between chunks,
   prioritize controls, and stop pending image writes on disconnect/errors.
6. Track only acknowledged pixels for client-side dirty-region optimization.
   The new page can provide image upload, pixels and rectangles later.

When regenerating CubeMX/IDE files, retain the display_service.c linked source,
service initialization, 11 GATT services, 84 attributes, 2560 value-array bytes,
and 8 service callback slots. These custom service settings are maintained in
app_conf.h, ble_conf.h and STM32CubeIDE/.project.

No Web runtime or page changes are part of this firmware delivery.

## Validation

`tools/check_display_animation.py --cc <arm-none-eabi-gcc>` now checks the
one-shot startup and real packet/task C code under ARM emulation (historical
filename retained). HAL/RTOS are mocked: bounds, lengths, idle behavior,
stop/start, diagnostics ownership and failure recovery are covered.
Firmware build and tools/validate_firmware.py verify linking and OTA layout.
Physical BLE timing, color/order and panel behavior require a hardware test.

Initialization data in gc9a01_init.h retains its Adafruit BSD attribution;
Font24 retains the existing ST license.


### Hardware regression, 2026-11-05

Confirmed the installed application matched the build byte-for-byte, then read
HAL_TIMEOUT in the SPI diagnostic and running=false. HAL tick runs at 100 Hz;
the original 10 ms deadline expired on a tick boundary. The 100 ms deadline
and non-latching transfer failure fix were flashed and verified via ST-LINK.
Direct BLE tests passed 480 acknowledged FILL_RECT writes for a complete
240x240 image (56.12 s), plus 245 writes exercising WRITE_ROW, WRITE_RECT,
malformed-packet rejection and recovery. No physical panel readback is available.
Run `python tools/check_display_ble.py --output report.json` with bleak installed;
disconnect the browser first. The test draws patterns but does not move servos.

## Link throughput (2026-11-05)

Command remains WRITE with an application response after SPI completes. There
is no characteristic "fast" flag. On the first valid-handle write per connection,
the service requests a 7.5-15 ms interval, zero peripheral latency, 4 s supervision
timeout and 251-byte link-layer data length. These requests are advisory; the
central determines the actual link settings. Existing PHY preference is 2M.
A reconnect clears the request guard. Pixel payloads still go directly to SPI,
without a framebuffer. Increasing the client chunk to 122 pixels reduces a full
raw image to 480 writes; transport response latency still limits throughput.

## Full-screen fill (2026-11-06)

`01 00 00 F0 F0 FF FF` fills the entire 240x240 driver area white with one
seven-byte BLE write. Only WRITE_RECT retains the 120-pixel payload limit.
The driver reuses its existing 480-byte scanline, sets one address window and
streams the solid color over SPI. The BLE acknowledgement follows completion;
this is not an atomic panel refresh. Full fill holds the display mutex and BLE
callback for the duration of the SPI operation (roughly 0.3 s wire time at
3.125 MHz, plus overhead). SPI timeout remains 100 ms per scanline call, not
for the complete rectangle. No framebuffer or deferred payload storage.
The Web sender requires this firmware update for background-first transfers.

## Persistent image cache (2026-11-06)

The image cache shares ONLY the OTA download interval [0x08080000,0x080FA000).
It never uses active application, bootloader, DATA or NVM pages. Catalog page:
0x08080000, 8 KiB. Slots 1-5 start at 0x08082000, 0x0809A000, 0x080B2000,
0x080CA000 and 0x080E2000, each 96 KiB (12 erase pages).

Format version 1 stores 91,384 RGB565 big-endian bytes, concatenated visible row
spans. ImageStore_RowLeft and Web visibleRow implement the same pixel-square
intersection with the radius-120 aperture. No JPEG decoding or framebuffer.
Slot header (16 bytes, four LE uint32 values) is written LAST: magic 0x31544C53,
length 91384, CRC-32/ISO-HDLC (reflected 0xEDB88320, init/final xor FFFFFFFF),
format 1. Payload starts at slot+32. A final 8-byte tail is padded with FF to a
16-byte Flash programming unit; CRC excludes padding. Each upload erases that
slot first; replacing an image does not preserve its old copy on failure.

Catalog header: magic 0x31474D49, version 1, payload length, slot count 5.
Catalog+16..31 is the OTA invalidation quadword, initially all FF. The firmware
boot tag location at catalog+0x160 remains erased. No image payload occupies
this page. Initial creation is allowed ONLY when the entire download region is
erased. Unknown/incomplete OTA content is never automatically reclaimed.
Startup validates each committed slot CRC. An interrupted upload is not visible.
Playback is read-only and does not consume Flash erase cycles.

Image service UUID: 91bad4d0-b950-4226-aa2b-4ede9fa42f59.
Command d1: WRITE with response, max 12 bytes. Data d2: WRITE with response,
max 228 bytes. Status d3: READ, max 240 bytes. Suffix remains identical.
Status is polled (no Capabilities and no notification subscription required).
A GATT write response means accepted into the single-job mailbox, NOT durable
completion. Poll matching sequence/opcode/id until DONE or ERROR. One upload
lease per BLE connection prevents interleaving; disconnect releases it after
any already accepted Flash operation finishes. No unbounded queue.

All command integers are little endian. Command prefix: op:u8, id:u8,
sequence:u16 (nonzero). IDs 1-5; LIST uses 0.

| Op | Name | Additional bytes |
|---|---|---|
| 1 | BEGIN | length:u32, crc:u32 |
| 2 | COMMIT | none |
| 3 | SHOW | none |
| 4 | DELETE | none |
| 5 | READ | byte offset:u32 |
| 6 | LIST | none |
| 7 | ABORT | none |

Data packet: sequence:u16, offset/16:u16, up to 224 payload bytes. It belongs
to the active upload and produces status opcode 8. All chunks are multiples of
16 bytes except the final 8-byte tail. The Web falls back to 16-byte payloads
(20-byte packets) on size rejection. Offsets must be exactly sequential; do not
blindly retry a failed/unknown write. Abort/reconnect and restart instead.

Status: version:u8=1, state:u8 (1 BUSY, 2 DONE, 3 ERROR), op:u8, id:u8,
sequence:u16, error:u8, valid-slot mask:u8, progress-or-read-offset:u32,
committed-slot CRC:u32, followed by up to 224 READ bytes. The read cursor is
explicit; reads do not advance it. SHOW DONE follows all SPI transfers. READ CRC
and offsets let the Web reject corrupt/mixed downloads before replacing pixels.
Errors: 80 malformed, 81 busy, 82 Flash/SPI, 83 display disabled, 84 OTA/unknown
region owns storage, 85 upload state/offset, 86 incomplete/CRC, 87 missing slot,
88 disconnect. Immediate rejection uses the corresponding ATT application error.

The display task executes storage work. It copies at most one small BLE packet
into a static mailbox and uses aligned static Flash scratch space. Accepted FM
operations retain their buffer until callback completion, including on disconnect.
FM_BUSY callbacks are awaited before retry. A stuck Flash manager keeps the job
owned; the client times out rather than reporting success or reusing its buffer.
SHOW holds the display mutex and streams Flash rows directly through BlitWire.
Manual drawing fails BUSY rather than interleaving. No animation scheduler yet.

OTA must claim ownership before erasing. It refuses while an image job/upload
is active, then durably invalidates the catalog before touching firmware bytes.
Images remain unavailable for that boot after OTA starts, including cancellation.
After reboot, interrupted/unknown OTA bytes keep the region locked until a proper
OTA recovery/update clears it. Successful OTA installation erases the download
slot using the existing bootloader flow; the Web project remains the backup.
The updated bootloader accepts only active/download slot starts, valid RAM stack
and active-slot reset vectors, and aligned in-slot tag pointers BEFORE relocation.
This prevents image catalogs/arbitrary bytes from being interpreted as firmware.
OTA raw writes now require an accepted bounded upload and 16-byte alignment;
firmware uploads must start at the download-slot base. Install BOTH bootloader
and application for the complete guard; service use requires updated application.

Verification: tools/check_image_store.py executes real storage C on ARM/Unicorn
with strict simulated Flash; tools/check_boot_image_guard.py executes the actual
boot candidate check; tools/check_display_animation.py covers display regression.
Hardware checker uses only an empty slot 5, then can verify across reset and delete
its exact test pattern. These simulations do not establish real Flash/RF timing.

Hardware verification after closing connected Web clients (2026-11-06):
91,384 bytes uploaded to empty slot 5, committed, read back byte-for-byte and
shown. Upload: 26.13 s; read: 33.96 s; SHOW: 0.344 s. After software reset the
same bytes were read back exactly, SHOW completed in 0.315 s, and only slot 5
was deleted; other slot validity bits were unchanged. MTU was 251.

The first reset test caught the legacy OTA_APP_Init unconditional DeleteSlot.
It erased images on every application startup. That call and unused direct-HAL
erase helper were removed: normal OTA service initialization now leaves Flash
untouched. Accepted OTA uploads still claim ownership and erase via Flash
Manager. The actual initializer is also exercised in check_boot_image_guard.py.
The corrected application was built, flashed and the full reset test repeated.
These checks confirm Flash content and driver completion, not visual inspection
of panel output; physical power-cut and end-to-end OTA installation remain untested.


## TOF depth learning mode

`TOF_MODE 3` enables sensor-driven rendering without uploading pixels over BLE.
`TOF_MODE 1` returns to normal subscription-gated ranging; `TOF_MODE 0` stops
ranging. Mode 2 is reserved. Disconnect or disabling TOF notifications exits
mode 3. Commands acknowledge the requested mode, not sensor initialization or
successful SPI drawing. Inspect sensor faults/display status during diagnostics.

The existing TOF task borrows its current 64-element millimeter array for a
synchronous `Display_DrawDepthMap` call. No pointer escapes and no new frame,
queue, task stack or heap allocation is needed. A nonblocking display mutex
skips frames when busy. 64 solid 21x21 fills reuse the driver's existing 480-byte
scratch buffer. The panel is cleared black once on entry; the 168x168 map begins
at (36,36), fully inside the round 240px screen. 180px would clip the corners.

RGB565 is calculated with integer arithmetic, no palette: 0 mm red, 2000 mm
yellow, 4000 mm green, clamped above 4000; UINT16_MAX invalid is black. The
0..4 m color scale matches the VL53L8CX rated range:
https://www.st.com/resource/en/datasheet/vl53l8cx.pdf . It is independent of
TOF_SET_MIN_MAX, which remains limited to 4000 mm for the existing BLE protocol.

Depth mode rejects remote rectangle/row and stored-image draws with BUSY. Exit
mode 3 first. A stop/disconnect cancels at a tile boundary; a started SPI write
finishes under the display lock. The last view remains visible after exit.

Validation: ARM-emulated real display C tests cover red/yellow/green, invalid
and clipped ranges, all 64 tile bounds, one-time clear, mutex busy, SPI failure
recovery and return to BLE drawing. Before the identity feature, the depth build added 648 bytes to the
application binary and 4 bytes of linked static RAM versus the pre-change local
build (338448 -> 339096 bytes Flash image; data+bss 119308 -> 119312 bytes).
No task stack sizes or driver buffers changed. Physical rendering not yet tested.


TOF orientation: the acquisition loop maps output zone `(row,col)` to sensor
zone `(7-row,col)` using `zone ^ 56`. This flips vertically only, preserving
column order. Validity checks use the same source zone. Runtime millimeters,
64-byte BLE frames and the local depth view all share the corrected orientation.
No extra array, frame copy, second pass or I2C transaction is added. The bundled
ULD exposes no orientation-setting API; this is a host-side normalization.


## Device identity and disconnected screen

`device_identity.h` formats a stable six-digit decimal identifier from all three
factory UID words. It needs no persistent Flash record, random-number peripheral
or static identity cache. Leading zeros are retained. This short human-facing
label is not a collision-free global ID or a security credential.

The GAP name and complete advertised local name are `NLXXXXXX` (8 bytes).
The name record is 10 bytes; the NUS UUID record is 18 bytes, and the stack retains
3 bytes of Flags: exactly 31 bytes.
Both legacy and enhanced successful connection events notify the display.
On disconnection, the BLE callback sets a flag and wakes the display worker;
it does not acquire a mutex or perform SPI transfers. The worker reinitializes
and draws the three centered lines: NatiLab / 1.0 / XXXXXX. No continuous refresh.
Drawing requests (including queued stored images) are BUSY while disconnected
or a welcome redraw is pending, preventing stale work from replacing identity.
A rapid reconnect still completes the requested welcome before accepting draws.

Validation: the real C ARM tests verify stable six-digit formatting, different
UID input, name/screen agreement, all three line positions, startup, deferred
SPI on disconnect, idle sleep and reconnect. Production build and linked-layout
checks passed. No physical-board verification or flashing in this change.
The identity/welcome addition increases the prior local binary by 456 bytes
and linked static RAM by 12 bytes; no additional framebuffer or task stack.


### Advertising budget regression fix

The initial 11-character identity name overflowed the legacy packet: the
31-byte name/UUID update was merged with the stack-owned 3-byte Flags record,
not used as a complete replacement. ST rejects updates exceeding 31 bytes,
leaving the old advertisement without the expected NUS/name fields. The Web
filters on the NUS UUID, so the board disappeared from its device picker.

The corrected name is `NL` plus six digits (8 characters). The static assertion
now budgets all records: Flags 3 + name 10 + NUS UUID 18 = 31 bytes. TX Power is
still removed before the update. The identifier itself and the screen text are
unchanged. The real-device check is `tools/check_advertising_ble.py`: scan for
name + NUS, connect, compare GAP name, PING, disconnect and scan again.
