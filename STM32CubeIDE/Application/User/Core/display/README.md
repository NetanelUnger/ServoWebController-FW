# GC9A01 display and BLE protocol v1

240x240 RGB565 display. Startup draws the NatiLab typographic mark once using
Font24, then sleeps indefinitely. The existing face animation sources are no
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
service initialization, 10 GATT services, 76 attributes, 1856 value-array bytes,
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
