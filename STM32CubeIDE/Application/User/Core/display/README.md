# GC9A01 display

Native STM32 HAL driver for the round 1.28-inch, 240x240 GC9A01 module on SPI3.
One statically allocated `displayTask` owns initialization and drawing. No
Arduino, Adafruit_GFX, LVGL, full framebuffer or dynamic allocation is required.
Original full-color artwork is stored in flash. The driver uses a 480-byte
scanline buffer; the face renderer uses 2400 bytes for index rows/RGB565 tiles
and the task has a 2048-byte stack.

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

At the beginning of `displayTask`, the worker resets the panel,
initializes its registers, exits sleep, fills its memory white and draws two
centered black lines using the existing ST Font24:

```text
Welcome To
NatiLab 1.0
```

The display is turned on only after the welcome frame is complete. After
`DISPLAY_WELCOME_MS` (2000 ms by default), `face_animation.c` plays a colorful
robot face with a shaded lavender shell, teal ear lights, pink cheeks, detailed
blue/teal irises, pupil reflections and eyebrows. Its twelve-second performance
includes left/right glances, blinks, looking up in surprise, an open smile,
closed happy eyes and a one-eye wink. Three mouth shapes accompany the eyes.
`DISPLAY_FRAME_PERIOD_MS` is 50 ms by default (at most 20 updates/second).
Unchanged poses send nothing. For changed poses, palette-decoded rows are
compared against the previous asset to find the actual changed rectangle.
Only that area is transmitted, independently for each eye and the mouth.
The full background (including the initial face) is drawn once on scene entry.
Playback uses elapsed time, so scheduling delays do not build up animation drift.
CS is high, DC low and RST high between transfers. SPI speed is unchanged.

`g_display_boot_status` records the welcome transfer result;
`g_display_frame_status` records the last scene transfer result, and
`g_display_frame_count` counts successful face update iterations (including
unchanged poses). A transfer error pauses the worker without a retry loop.
`Display_Start()` can retry after resolving the fault. BLE remains independent.

This project initializes HAL's RTC timebase in `MX_APPE_Init()`. The welcome
sequence therefore runs after platform/scheduler startup, not in `main()`'s
pre-RTOS peripheral initialization. BLE radio work can run while the frame is
drawn. The worker runs at `osPriorityBelowNormal`, below BLE and communication
tasks; SPI interrupts are not masked by drawing. Drawing uses polling HAL SPI,
so transfer time still consumes CPU. Actual frame rate/CPU load needs measurement
on hardware. The static display mutex serializes drawing with manual pin writes.

## Playback lifecycle

`display.c` owns playback/scheduling; `face_animation.c/.h` owns this scene's
pose sequence and incremental rendering. Future clips or BLE-triggered scenes should be selected by
the same display worker, without drawing from BLE callbacks.

- `Display_Stop()` waits for the current drawing operation, prevents further
  SPI transfers and wakes the worker into an indefinite flag wait. There is no
  periodic polling while stopped. The last image stays visible.
- `Display_Start()` after a stop reinitializes the panel, shows the welcome
  screen and restarts the face. Calling it while already running is idempotent.
- Both APIs are task-context calls, not ISR/callback drawing operations. Do not
  externally call `osThreadSuspend` or delete the task during a transfer.
- Stopping saves CPU/SPI work, but keeps the static task stack/buffers allocated.
  It does not cut panel/backlight power; no backlight GPIO is available.
- `ENABLE_DISPLAY=0` removes task/mutex allocation and display rendering from
  the linked application. SPI3 configuration and manual GPIO diagnostics remain.

The worker and mutex are recorded in `.ioc` and created in `app_freertos.c`.
Retain the `ENABLE_DISPLAY` guards and static allocation when regenerating.
It has no watchdog liveness bit because intentional indefinite waits are valid.

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

The driver API is synchronous and is owned by `displayTask`. Any `GPIO_SET` to
DISPLAY_CS/DC/RES (including physical aliases) waits for the current draw, pauses
playback and writes the pin under the same mutex. It stays paused until
`Display_Start()` or reset; no BLE playback command is introduced yet. Reads are
passive and do not pause playback; during animation they may observe active SPI
control levels. `GPIO_SET DISPLAY_RES 0` also resets the panel configuration.

## Provenance

The face is original project artwork generated by `tools/generate_face_assets.py`
(Python + Pillow + numpy). Drawing uses four-times supersampling, shaded layers,
iris fibers, highlights and antialiased outlines before reducing to 240x240.
Thirty opaque images share a 256-color RGB565 palette and lossless row-wise RLE.
Separate left/right sprite banks preserve the asymmetric backdrop under each
eye. The checked-in `face_assets.c/.h` need no Python during a firmware build.
Run the generator to edit/regenerate the art, then rebuild the firmware.

Generated asset tables occupy 155,378 bytes (about 151.7 KiB) of flash. Only a
few scanlines are decompressed into RAM. Sprite edges include their background,
so closing an eye or changing the mouth erases the previous shape completely.

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

The user flashed the corrected wiring build and confirmed the welcome screen
works on the physical display. The face/task change is validated offline; it
has not yet been flashed or visually confirmed on the panel.

`tools/check_display_animation.py --cc <arm-none-eabi-gcc> --output <preview-dir>`
compiles the actual C scene and worker and runs them with Unicorn ARM emulation
and mocked HAL/RTOS boundaries. It checks welcome timing, pause/resume, serialized
manual GPIO, SPI-error recovery, colors, glances, eyelid closure, one-eye winks,
mouth expressions, every packed asset row, dirty-region cleanup, unchanged-frame
suppression and tick rollover. It also generates a twelve-second
GIF and PNG preview from actual rendered pixels. Requirements: `unicorn`,
`pyelftools`, `Pillow`. This is not a hardware timing or RTOS scheduling test.

Firmware build and `tools/validate_firmware.py` check the OTA layout and disabled
I2C drivers. The older board/display JSON reports predate animation (and the pin
correction); they do not establish animation performance on hardware.
