# GC9A01 display

Native STM32 HAL driver for the round 1.28-inch, 240x240 GC9A01 module on SPI3.
No Arduino, Adafruit_GFX, LVGL, framebuffer, heap allocation or new RTOS task is
required. A reusable 480-byte scanline buffer sends RGB565 pixels in wire order.

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

## Startup

`ENABLE_DISPLAY=1` and `DISPLAY_ROTATION=0` are the defaults in `config.h`.
Rotation values `0..3` select successive quarter turns. To suppress all display
initialization/drawing, build with `ENABLE_DISPLAY=0`; SPI3 remains configured.

At the beginning of `commTask_Entry`, `Display_ShowWelcome()` resets the panel,
initializes its registers, exits sleep, fills its memory white and draws two
centered black lines using the existing ST Font24:

```text
Welcome To
NatiLab 1.0
```

The display is turned on only after the frame is complete. It is not cleared or
periodically redrawn afterward. CS is left high, DC low and RES high.
`g_display_boot_status` records the HAL transfer status. A transfer error returns
to the normal communication loop and does not prevent BLE operation.

This project initializes HAL's RTC timebase in `MX_APPE_Init()`. The welcome
sequence therefore runs after platform/scheduler startup, not in `main()`'s
pre-RTOS peripheral initialization. BLE radio work can run while the frame is
drawn; queued GPIO diagnostics are processed afterward by the same task.

## Driver API

- `GC9A01_Init()` performs reset and register initialization, leaving the display off.
- `GC9A01_SetRotation()` selects orientation `0..3`.
- `GC9A01_SetDisplayEnabled()` enables/disables display output.
- `GC9A01_FillRect()` draws a solid rectangle; a 240x240 rectangle clears the screen.
- `GC9A01_DrawText()` draws printable ASCII with an ST `sFONT`, foreground and background.
- `GC9A01_BlitRGB565()` sends a row-major array of native `uint16_t` RGB565 pixels.

All calls return `HAL_StatusTypeDef`. Rectangles must fit within 240x240 and have
nonzero dimensions; invalid arguments return `HAL_ERROR`. The bitmap caller must
provide at least `width * height` pixels. The display's circular mask hides the
corners of the square address space.

The API is synchronous and requires a single caller or external serialization.
Current startup drawing and GPIO diagnostic writes are serialized by commTask.
Future display tasks must coordinate with that owner, including manual CS/DC/RES
commands. As expected, `GPIO_SET DISPLAY_RES 0` resets and clears the panel's
configuration; a subsequent initialization/redraw is required.

## Provenance

The initialization data in `gc9a01_init.h` is adapted from
[Adafruit_GC9A01A v1.2.0](https://github.com/adafruit/Adafruit_GC9A01A/blob/e742c29db8d4b84e1bbca16005aa7bfd59cf0b3c/Adafruit_GC9A01A.cpp),
commit `e742c29db8d4b84e1bbca16005aa7bfd59cf0b3c`. Upstream BSD attribution is
retained in that file. The only initialization-table change is deferring DISPON
until the welcome frame is ready. The original register sequence includes the
inversion setting used by these IPS panels; application RGB565 white and black
are still `0xFFFF` and `0x0000`.

Font24 comes from the repository's existing `Utilities/Fonts`; its ST license is
retained there. The controller reference is the
[GC9A01A datasheet](https://files.waveshare.com/wiki/common/GC9A01A.pdf).

## Validation scope

The display wiring correction above was built offline only while the user was
soldering. It has not been flashed or electrically verified. The earlier recorded
SPI/boot/BLE checks below used the previous pin mapping.


Build and OTA layout checks, startup SPI status, BLE advertising, HELP responses
and GPIO/ADC regression checks are performed on the STM32. The user confirmed
that the physical display is not connected yet, so visual output, orientation,
backlight and actual color order still need checking after connection and reset.
