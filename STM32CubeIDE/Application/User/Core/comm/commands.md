# comm_interface Commands

## Protocol Notes

- Commands are plain-text and case-sensitive.
- Arguments may be separated by spaces, tabs, commas, colons, semicolons, `\r`, or `\n`.
- Integer arguments are parsed with base `0`, so both decimal and `0x...` hex are accepted.
- Success replies start with `OK`.
- Error replies are:
  - `- BAD ARGUMENTS`
  - `- NOT SUPPORTED`
  - `- PCA9685 DISABLED` when servo hardware is disabled in `config.h`
  - `- VL53L5CX DISABLED` when TOF hardware is disabled in `config.h`
- Servo channels are `0..15`.

## Supported Commands

| Command | Arguments | Description | Success Response |
| --- | --- | --- | --- |
| `HELP` | none | List every command, syntax and legacy alias, including commands for disabled devices. | Multiple notifications, from `OK HELP` through `OK HELP END`. |
| `PING` | none | Connectivity check. | `OK PING` |
| `GPIO_GET` | `<signal_or_pin>` | Read the electrical GPIO level. INT_TOF adds its falling-edge counter. | `OK GPIO_GET <signal> <0_or_1>`; INT_TOF adds ` IRQ <count>` |
| `GPIO_SET` | `<signal_or_pin> <0_or_1>` | Write a board output. The level must be the literal `0` or `1`. | `OK GPIO_SET <signal> <0_or_1>` |
| `ADC_READ` | `<signal_or_pin>` | Single on-demand 12-bit ADC reading. | `OK ADC_READ <signal> <raw_0_to_4095>` |
| `SET_DIRECT` | `<channel> <pulse_us>` | Immediate pulse update. Any timed move currently running on that channel is cancelled and the new pulse is applied right away. | `OK SET_DIRECT <channel> <pulse_us>` |
| `SET_TIMING` | `<channel> <pulse_us> <duration_ms>` | Linear timed move. The channel moves from its current pulse to `pulse_us` over exactly `duration_ms` milliseconds. A duration of `0` behaves like an immediate move. | `OK SET_TIMING <channel> <pulse_us> <duration_ms>` |

## Legacy Aliases

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
| EN1 | PB5 | GPIO_GET / GPIO_SET |
| EN2 | PB4 | GPIO_GET / GPIO_SET |
| DIAG_EN1 | PA10 | GPIO_GET / GPIO_SET |
| DIAG_EN2 | PA11 | GPIO_GET / GPIO_SET |
| OE | PA9 | GPIO_GET / GPIO_SET (0 enables PCA9685 outputs) |
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

The board starts with EN1/EN2 and DIAG_EN1/DIAG_EN2 low, OE high, DISPLAY_CS high,
DISPLAY_RES initially low and DISPLAY_DC low. With ENABLE_DISPLAY=1, startup
drawing releases DISPLAY_RES high before command handling. Manual commands can override these levels
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
