# PCA9685 bring-up findings, 2026-11-04

Subsequent TOF migration: `VL53L8CX_MIGRATION.md` records the now-enabled
VL53L8CX ranging driver and successful BLE distance-map tests. The historical
identification-only/disabled-TOF descriptions below no longer describe TOF
operation. PCA fixed-address and neutral-output behavior is unchanged.

## Current verified result, 2026-11-05 (supersedes historical findings below)

The user confirmed that only the ground-side address resistors are fitted.
PCA9685 therefore uses fixed 7-bit address 0x40 (STM32 HAL argument 0x80).
The latest diagnostic was built, flashed with verification, and tested again
after a software reset. Both runs reached stage=4, verified=74,
PRE_SCALE=121, cycles=10, last_error=0. PA9 is high to enable outputs via Q6.
LED0 / PWM_CH1 is configured for a nominal 1498 us pulse at 50 Hz; all other
channels are full-off. EN1/EN2 remain low. Mechanical movement, servo power,
and the actual waveform have not been measured. Normal BLE servo commands
remain disabled; this is a fixed neutral-output diagnostic, not a sweep.

| Device | 7-bit address | Identification | Readback result per boot |
| --- | --- | --- | --- |
| LSM6DSV320X | 0x6B | WHO_AM_I (0x0F) = 0x73 | 20/20 |
| VL53L8CX | 0x29 | Model = 0xF0, revision = 0x0C | 20/20 |
| PCA9685 | 0x40 | MODE1/MODE2 and PWM register verification | 74 writes verified |

The earlier inference that 0x6B was a changed PCA address was incorrect: it
belongs to the IMU. 0x70 is the PCA default All Call group address, not its
strapped address. Group addressing is disabled by this diagnostic.
The tentative LSM6DSV32X identification was corrected to LSM6DSV320X using
the supplied marking and ST's WHO_AM_I definition (0x73).

Two diagnostic problems were corrected: MODE1 RESTART is hardware-managed
(writing zero does not clear it), so configuration readback masks bit 7;
zero-byte HAL readiness scans returned no devices even when actual sensor
register reads succeeded. The diagnostic now uses a real register read at
fixed 0x40, with no arbitrary-address fallback. The cause of the readiness
scan discrepancy has not been established; it is not proof of bad hardware.
The old ack/error arrays now describe only the fixed PCA register probe,
not a full bus scan. Stage 2 means that fixed-address read failed.

Sensor checks are identification only: no IMU sampling or TOF ranging is
started. The existing normal TOF driver is VL53L5CX and stays disabled;
the actual VL53L8CX needs its matching driver before ranging is enabled.
Firmware layout/OTA/disabled-driver validation passed.

References:
- https://www.st.com/resource/en/datasheet/lsm6dsv320x.pdf
- https://www.st.com/en/imaging-and-photonics-solutions/vl53l8cx.html
- https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf

## Historical experiments (not the current firmware state)

The expected 7-bit address with A0..A5 grounded is 0x40 (HAL argument 0x80).
Hardware observations through ST-LINK SWD after verified flashes:

1. With original no-pull configuration, scan ACKs were 0x29 and 0x70.
   Reads at 0x70 returned MODE1=0x11, MODE2=0x04, PRE_SCALE=0x1e.
2. One run completed 114 verified register writes and 7 RGB cycles after
   configuring 50 Hz and asserting active-low OE. This was not repeatable.
3. Later resets showed 0x00/0x29/0x6b, with 0x40 and 0x70 absent.
4. With weak MCU-side pull-ups enabled, scan ACKs changed to
   0x00/0x30/0x40/0x6b/0x7f. Reading the expected device then failed
   (HAL error 1, bus error); report stage=3 and OE stayed high.

These inconsistent address sets do NOT establish an alternate PCA address.
In particular, the earlier inference that 0x70 was its hardware address was
withdrawn after reset testing. Pull-up/translator/signal integrity or changing
hardware connections must be investigated before relying on scan results.
Idle-high digital levels do not prove adequate pull-ups or valid rise times.
The user identified the translator as NLA9306MU3TCG, confirmed no hardware changes during testing, and reported that the RGB LED was not populated. The translator datasheet explicitly requires SDA/SCL pull-ups on both sides. External resistor population and both voltage rails must be checked before further register-write tests.

Current firmware contains a diagnostic in the default task, enabled by
ENABLE_PCA9685_DIAGNOSTIC=1. It is an explicit exception to the disabled normal
servo/TOF tasks. It scans addresses with no payload (including reserved addresses
for diagnosis only), records ACK/error arrays in g_pca_diagnostic, and enables OE
only after successful configuration/readback. RGB tests touch channels 12..14;
all other channels are programmed full-off first. The current flashed image uses
weak PA6/PA7 MCU pull-ups as a diagnostic experiment, not a hardware substitute.
Set ENABLE_PCA9685_DIAGNOSTIC=0 to disable this diagnostic path.

Current state: build and flash verification passed, bus reliability failed,
OE disabled. Visual RGB operation and polarity have not been confirmed.
Reference: https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf

Translator reference: https://www.onsemi.com/download/data-sheet/pdf/nla9306-d.pdf
The typical bidirectional circuit ties EN to VREF2 and biases that node from the high-side rail through about 200 kohm. This bias resistor is separate from the four bus pull-ups. Verify actual schematic and rail voltages before choosing resistor values.

## Superseding schematic-based correction

The supplied schematic shows Q6 inverting MCU PA9 to PCA /OE: PA9=0 disables,
PA9=1 enables. Startup and diagnostic code now follow this polarity. Earlier
reports describing MCU-low as enabled were incorrect for this board.
The schematic includes SDA/SCL pull-ups R35/R36=2.21k to 3.3V and R22/R23=4.75k
to 5V, plus the 200k EN/VREF2 bias network. Actual population must be checked.
Each address input is drawn with 10k to 5V and 10k to ground. If both are fitted,
the resulting 2.5V is outside guaranteed logic levels (low <=1.5V, high >=3.5V
at VDD=5V). Only ground-side resistors should be populated for address 0x40.

Latest firmware uses fixed 0x40, removes all alternate-address write fallbacks
and the temporary MCU pull-ups, and corrects PA9 startup to low in main/.ioc.
Diagnostic success would output a fixed 1500us/50Hz pulse only on LED0/PWM_CH1;
other channels stay full-off. No automatic sweep. Normal servo tasks remain
disabled in this diagnostic build. Latest flashed test ACKed 0x40 and read
MODE1=0x20/MODE2=0x04, but register write/readback failed (stage=3), so PA9
remains low and outputs disabled. A fixed address alone has not resolved it.
