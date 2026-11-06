# comm_interface Commands

`PWM_GET <channel>` reads the actual PCA9685 channel registers (0..15). The reply
is `OK PWM_GET <channel> <on> <off>`; bit 12 represents FULL_ON/FULL_OFF. This is
read-only diagnostics, also exposed in the Web console. `ADC_READ` now reads the
latest individual DMA sample; the BLE ADC characteristic contains weighted window
means instead. See `ADC_RAW.md` and `SERVO_SCHEDULING.md` in the repository root.

## Protocol Notes

- Commands are plain-text and case-sensitive.
- Arguments may be separated by spaces, tabs, commas, colons, semicolons, `\r`, or `\n`.
- Integer arguments are parsed with base `0`, so both decimal and `0x...` hex are accepted.
- Success replies start with `OK`.
- Error replies are:
  - `- BAD ARGUMENTS`
  - `- NOT SUPPORTED`
  - `- PCA9685 DISABLED` when servo hardware is disabled in `config.h`
  - `- VL53L8CX DISABLED` when TOF hardware is disabled in `config.h`
- Servo channels are `0..15`.

## Supported Commands

| Command | Arguments | Description | Success Response |
| --- | --- | --- | --- |
| `HELP` | none | List every command, syntax and legacy alias, including commands for disabled devices. | Multiple notifications, from `OK HELP` through `OK HELP END`. |
| `PING` | none | Connectivity check. | `OK PING` |
| `DEMO1` | none | All 16 PCA9685 channels start at 500 us and toggle together to 2000 us and back every 2000 ms. Runs in servoTask until STOP or BLE disconnect. Repeating DEMO1 leaves the current phase unchanged. | `OK DEMO1` |
| `STOP` | none | Cancel DEMO1 and all timed moves, holding the last pulse widths. Safe to repeat. | `OK STOP` |
| `TOF_MODE` | `<0|1|3>` | 0 off; 1 range while BLE subscribed (default); 3 live depth on screen plus optional BLE. 2 reserved. Disconnect/unsubscribe returns to 1. | `OK TOF_MODE <mode>` |
| `TOF_SET_MIN_MAX` | `<min_mm> <max_mm>` | Set distance-map scaling; 0 <= min < max <= 4000. Default 0..500 mm. | `OK TOF_SET_MIN_MAX <min_mm> <max_mm>` |
| `GPIO_GET` | `<signal_or_pin>` | Read the electrical GPIO level. INT_TOF adds its falling-edge counter. | `OK GPIO_GET <signal> <0_or_1>`; INT_TOF adds ` IRQ <count>` |
| `GPIO_SET` | `<signal_or_pin> <0_or_1>` | Write a board output. The level must be the literal `0` or `1`. | `OK GPIO_SET <signal> <0_or_1>` |
| `ADC_READ` | `<signal_or_pin>` | Single on-demand 12-bit ADC reading. | `OK ADC_READ <signal> <raw_0_to_4095>` |
| `SET_DIRECT` | `<channel> <pulse_us>` | Immediate pulse update. Any timed move currently running on that channel is cancelled and the new pulse is applied right away. | `OK SET_DIRECT <channel> <pulse_us>` |
| `SET_TIMING` | `<channel> <pulse_us> <duration_ms>` | Linear timed move. The channel moves from its current pulse to `pulse_us` over exactly `duration_ms` milliseconds. A duration of `0` behaves like an immediate move. | `OK SET_TIMING <channel> <pulse_us> <duration_ms>` |

## Legacy Aliases

`demo1` and `stop` are also accepted in lowercase. Both commands reject arguments.
DEMO1 requires an active BLE connection and a ready PCA9685; otherwise it replies
`- DEMO1 NOT READY` (or `- PCA9685 DISABLED`). While DEMO1 owns the outputs,
SET_DIRECT and SET_TIMING return `- DEMO1 ACTIVE: STOP`, and binary servo writes
are rejected. Send STOP before manual control. STOP keeps PWM holding its last
value; disconnect additionally disables PWM and the MPQ power paths. Reconnection
does not resume DEMO1 or cancelled timed moves. All channels means PCA LED0..LED15,
including channels wired for RGB/backlight on this board.

- `PCA9685_SET_SERVO_US` is accepted as an alias for `SET_DIRECT`.
- `servo_StartMove` is accepted as an alias for `SET_TIMING`.

## Examples

```text
HELP
PING
SET_DIRECT 3 1500
SET_TIMING 3 2100 1000
GPIO_GET PB5
GPIO_SET EN1 0
GPIO_GET INT_TOF
ADC_READ ADC1
ADC_READ ADC2
ADC_READ ADC_BAT
```

## Board diagnostic arguments

| Signal | Physical pin alias | Access |
| --- | --- | --- |
| EN1 | PB5 | GPIO_GET; BLE-managed, GPIO_SET returns READ ONLY |
| EN2 | PB4 | GPIO_GET; BLE-managed, GPIO_SET returns READ ONLY |
| DIAG_EN1 | PA10 | GPIO_GET; BLE-managed, GPIO_SET returns READ ONLY |
| DIAG_EN2 | PA11 | GPIO_GET; BLE-managed, GPIO_SET returns READ ONLY |
| OE | PA9 | GPIO_GET / GPIO_SET (1 requires BLE connected and PCA ready; otherwise READ ONLY) |
| DISPLAY_RES | PA2 | GPIO_GET / GPIO_SET |
| DISPLAY_CS | PA4 | GPIO_GET / GPIO_SET |
| DISPLAY_DC | PB11 | GPIO_GET / GPIO_SET |
| INT_TOF | PC13 | GPIO_GET only |
| ADC1 | PA3 | ADC_READ (ADC4 channel 6) |
| ADC2 | PB9 | ADC_READ (ADC4 channel 10) |
| ADC_BAT | PA5 | ADC_READ (ADC4 channel 4) |

Display signal names describe the actual purchased module, not the schematic
labels: schematic RES/PB11 is DC, D/C/PA4 is CS, and CS/PA2 is RST.
`DISPLAY_RES` remains the command name for actual RST.

Replies use canonical signal names even when a physical pin alias was sent.
For example, `GPIO_GET PB5` returns `OK GPIO_GET EN1 0` when the input reads low.
`GPIO_GET PC13` returns e.g. `OK GPIO_GET INT_TOF 1 IRQ 0`.

Additional errors: `- UNKNOWN PIN`, `- READ ONLY`, `- ADC ERROR`.
Missing arguments, extra tokens, embedded NUL bytes or invalid output levels
return `- BAD ARGUMENTS` without writing any output. GPIO commands do not change
pin modes. Analog inputs cannot be written. These commands run in commTask,
outside BLE callbacks. A GPIO_SET acknowledgement echoes the commanded level;
use GPIO_GET to read back the actual pin.

The board starts with EN1/EN2 and DIAG_EN1/DIAG_EN2 low, OE low, DISPLAY_CS high,
DISPLAY_RES initially low and DISPLAY_DC low. With ENABLE_DISPLAY=1, the display
worker releases DISPLAY_RES high during initialization and draws the NatiLab mark once. GPIO_SET on any display pin waits for the current
draw and disables remote display writes before writing; drawing stays stopped until a firmware
Display_Start() call or reset. GPIO_GET is passive and may read a transient level
during SPI drawing. Manual commands can override these levels
even while I2C device drivers are disabled. ADC readings are raw codes, not volts
or amperes. See [BOARD_IO.md](../../../../../BOARD_IO.md) for the peripheral setup.


## HELP response

`HELP` takes no arguments. Subscribe to NUS TX before sending it. The reply is
sent as separate short text notifications, in order, beginning with `OK HELP`
and ending with `OK HELP END`. Each HELP notification is at most 20 bytes so it
also fits the default ATT MTU. Syntax that does not fit on one line continues
on the next indented line. Aliases are followed by `= <canonical_command>`.
`<pin>` accepts the signal names and physical aliases in the table above;
`ch` is the servo channel, `us` is microseconds and `ms` is milliseconds.
Commands for disabled devices remain listed and still return `DISABLED` if used.
`HELP extra` returns `- BAD ARGUMENTS` without a partial help listing.

Hardware correction: GPIO_SET OE 1 enables PCA outputs through inverting Q6; GPIO_SET OE 0 disables them. Startup PA9 is now low.

## MPQ power follows BLE connection

EN1 (PB5), EN2 (PB4), DIAG_EN1 (PA10), DIAG_EN2 (PA11) start low.
A successful BLE connection sets all four high; its disconnect event clears
OE first, then EN and DIAG_EN. Failed connection events cannot enable power.
PWM also remains disabled at boot and while disconnected, including after PCA
initialization/retry. A new connection enables PWM once the PCA is ready.
GPIO_SET OE 0/1 remains available for diagnostics during a connected session;
it cannot bypass the BLE/PCA readiness gate. A fresh connection restores OE=1.
Signal names and physical-pin aliases are read-only through GPIO_SET, so queued
commands cannot override disconnect shutdown. HELP is delivered as short
notifications, ending with OK HELP END.

## Binary display service

Pixel and rectangle commands use a separate GATT service, not NUS text or HELP.
See [the display protocol](../display/README.md) for UUIDs, byte layouts,
limits, errors and the future Web integration checklist.
