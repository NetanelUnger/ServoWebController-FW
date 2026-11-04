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
| `PING` | none | Connectivity check. | `OK PING` |
| `SET_DIRECT` | `<channel> <pulse_us>` | Immediate pulse update. Any timed move currently running on that channel is cancelled and the new pulse is applied right away. | `OK SET_DIRECT <channel> <pulse_us>` |
| `SET_TIMING` | `<channel> <pulse_us> <duration_ms>` | Linear timed move. The channel moves from its current pulse to `pulse_us` over exactly `duration_ms` milliseconds. A duration of `0` behaves like an immediate move. | `OK SET_TIMING <channel> <pulse_us> <duration_ms>` |

## Legacy Aliases

- `PCA9685_SET_SERVO_US` is accepted as an alias for `SET_DIRECT`.
- `servo_StartMove` is accepted as an alias for `SET_TIMING`.

## Examples

```text
PING
SET_DIRECT 3 1500
SET_TIMING 3 2100 1000
```
