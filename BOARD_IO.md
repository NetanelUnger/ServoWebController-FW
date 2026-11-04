# Board pin map and BLE diagnostics

The Git firmware uses the following mapping in `BleServoController.ioc`,
`Core/Inc/main.h`, startup code and the diagnostic command allowlist.

| Pin | Signal | Peripheral / mode | Startup state |
| --- | --- | --- | --- |
| PA7 | I2C_SDA | I2C3 SDA, AF4, open drain | Idle; external pull-up required |
| PA6 | I2C_SCL | I2C3 SCL, AF4, open drain | Idle; external pull-up required |
| PB4 | EN2 | GPIO output, push-pull | 0 |
| PB5 | EN1 | GPIO output, push-pull | 0 |
| PB8 | SPI_MOSI | SPI3 MOSI, AF6 | GC9A01 startup drawing |
| PB9 | ADC2 / CURRENT_SENSE2 | ADC4 channel 10, analog, no pull | On-demand sampling |
| PB11 | DISPLAY_DC | GPIO output, push-pull | 0 |
| PC13 | INT_TOF | EXTI13, falling edge, pull-up | Input; edge count starts at zero |
| PA0 | SPI_SCK | SPI3 SCK, AF6 | GC9A01 startup drawing |
| PA2 | DISPLAY_RES (actual RST) | GPIO output, push-pull | Initially 0, then 1 after display reset |
| PA3 | ADC1 / CURRENT_SENSE1 | ADC4 channel 6, analog, no pull | On-demand sampling |
| PA4 | DISPLAY_CS | GPIO output, push-pull | 1 (deselected) |
| PA5 | ADC_BAT | ADC4 channel 4, analog, no pull | On-demand sampling |
| PA9 | OE | GPIO output, push-pull | 1 (PCA9685 outputs disabled) |
| PA11 | DIAG_EN2 | GPIO output, push-pull | 0 |
| PA10 | DIAG_EN1 | GPIO output, push-pull | 0 |

Pin functions were checked against ST's [module datasheet](https://www.st.com/resource/en/datasheet/stm32wba5mmg.pdf),
[MCU alternate-function tables](https://www.st.com/resource/en/datasheet/stm32wba55cg.pdf)
and the installed CubeMX STM32WBA5MMGHx database. `ADC1` and `ADC2` are board signal
names, not separate ADC peripherals.

## Display wiring correction

The schematic labels do not match the connector functions on the purchased
GC9A01 module. Firmware names always describe the **actual function**:

| Schematic label | MCU pin | Actual display function | Firmware/BLE name |
| --- | --- | --- | --- |
| RES | PB11 | DC (0 = command, 1 = data) | DISPLAY_DC |
| D/C | PA4 | CS (active low) | DISPLAY_CS |
| CS | PA2 | RST (active low) | DISPLAY_RES |

This correction is reflected in CubeMX labels, GPIO port initialization, the
SPI driver and BLE physical-pin aliases. Earlier hardware validation JSON files
predate this correction; the corrected mapping has only been built offline
while the user works on the hardware. It has not been flashed or measured yet.

## Shared I2C

I2C3 uses PA6/PA7 and timing `0x10C0ECFF` with its 100 MHz PCLK7 clock
(nominal Standard-mode 100 kHz). Both pins use AF4 open drain, without internal
pull-ups; the board must supply pull-ups. PCA9685 and VL53L5CX continue to use
the same `hi2c3` and `i2c_os` mutex. `I2C_BUS` also points to `hi2c3`.

Both `DISABLE_I2C_PCA9685` and `DISABLE_I2C_VL53L5CX` still default to `1`.
Their tasks and transfers remain disabled. The shared bus itself now initializes
even with both devices disabled, so the configured pins match the board design.
Manual GPIO test commands do not start either driver. GPIO writes are direct
electrical controls: `OE 0` enables PWM outputs, and EN writes can change the
power paths. A reset restores the startup levels above.

## SPI and ADC

SPI3 is initialized as a transmit-only master: 8-bit, MSB first, mode 0, software
NSS and PCLK7 / 32 (3.125 MHz at the configured 100 MHz clock). PB9 remains an
analog input; no MISO pin is configured. `DISPLAY_SPI` names `hspi3`. The GC9A01
driver draws a white welcome screen with black text once after scheduler startup.
See [the display driver](STM32CubeIDE/Application/User/Core/display/README.md).

ADC4 uses 12-bit right-aligned, single software-triggered conversions, HCLK / 8,
and 814.5-cycle acquisition time. Startup performs ADC calibration. Each command
selects one input, discards one settling sample, and returns the next sample.
No ADC DMA or periodic acquisition is started. `commTask` serializes diagnostic
calls; future ADC clients must share an owner or mutex. Radio temperature-based
calibration is disabled; enabling it requires integration with ST's ADC arbiter.

ADC results are raw `0..4095` codes. They are not battery volts or amperes:
those need the board's VDDA/reference, battery divider and FT/CS scaling. The
software test checks conversion completion, not the accuracy of external analog
circuitry. INT_TOF counts falling edges without accessing the sensor in the ISR.

## Commands

Send plain text to the existing NUS RX characteristic and subscribe to NUS TX.
See [the command reference](STM32CubeIDE/Application/User/Core/comm/commands.md).

```text
HELP
GPIO_GET EN1
GPIO_SET EN1 0
GPIO_GET PB5
GPIO_GET INT_TOF
GPIO_SET DISPLAY_DC 1
GPIO_SET DISPLAY_DC 0
ADC_READ ADC1
ADC_READ PB9
ADC_READ ADC_BAT
```

GPIO access is limited to the eight output signals and INT_TOF listed above.
Peripheral pins, SWD and unrelated board GPIO cannot be changed by these commands.
Outputs are read through the pin input register, not just their commanded latch.
For INT_TOF the reply also includes the interrupt count since reset.

## Verification

Build with `tools/build.ps1`; validate linked images with
`tools/validate_firmware.py --nm <arm-none-eabi-nm.exe>`.
After flashing, `tools/check_board_ble.py --address <BLE address>` checks the
startup levels, pin aliases, ADC conversions, disabled-device replies and rejected
writes. It requires Python with `bleak`. Optional `--exercise-display-dc` toggles
only DISPLAY_DC while DISPLAY_CS is high, and restores DC
to zero. It never enables EN1, EN2 or OE. Run on the intended board after reset. Add `--display-disabled` for an
`ENABLE_DISPLAY=0` build (DISPLAY_RES remains low).

The older DMA linked-list helpers are unused legacy code; do not start the GPIO
mux DMA with this pin map. SPI waveforms, TOF interrupt edges and analog scaling
still need physical instruments or known external stimuli for electrical validation.
