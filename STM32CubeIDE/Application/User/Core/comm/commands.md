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
| `VERSION` | none | Running version, image CRC32, padded size and OTA protocol. | Short packets from `OK VERSION` through `OK VERSION END`; see root `OTA.md`. |
| `HELP OTA` | none | List OTA diagnostics. | `VERSION` between HELP markers. |
| `BLE_STATS` | none | Read transport and ATT recovery counters (`cp/cr/cf`), queue depths, heap and stack headroom; does not reset counters. Displayed monotonic counts saturate at9999999. See `BLE_TRANSPORT.md`. | Short notification lines from `OK BLE_STATS` through `OK BLE_STATS END`. |
| `DEMO1` | none | All 16 PCA9685 channels start at 500 us and toggle together to 2000 us and back every 2000 ms. Runs in servoTask until STOP or BLE disconnect. Repeating DEMO1 leaves the current phase unchanged. | `OK DEMO1` |
| `STOP` | none | Cancel timeline playback, DEMO1 and all timed moves, holding the last pulse widths. Safe to repeat. | `OK STOP` |
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


## Stored timeline commands

- `HELP TIMELINE`: list the timeline commands separately from the legacy HELP page.
- `TIMELINE_PLAY <id>`: validate and start an existing live type-3 file. IDs are
  unsigned decimal, 1..4294967295, with no signs, overflow, hex or extra arguments.
  Reply `OK TIMELINE_PLAY <id>\n` means accepted, not completed. Starting while
  another timeline or DEMO1 owns servos is rejected. Stop first to restart.
- `TIMELINE_STOP`: reply `OK TIMELINE_STOP\n`. Same holding/cancellation behavior
  as STOP. Cancels preparation too; a disconnect also cancels and disables PWM.
- `TIMELINE_STATUS`: reply
  `OK TIMELINE_STATUS <state> <id> <elapsed_ms> <event_index> <event_count> <max_late_ms>\n`.
  State is IDLE, PLAYING, COMPLETE, STOPPED or FAULT. Elapsed/index are within the
  current repeat cycle; index is the next event (processed count). Counters remain
  after stopping. Max lateness is measured logical event dispatch lateness, not a
  physical PWM or shaft-position measurement.

Every timeline success/error response ends in newline so clients can assemble
fragmented NUS notifications. Errors are `ERR TIMELINE_<command> <reason>\n`.
PLAY reasons: BAD_ARGUMENTS, BUSY, NOT_READY, DISABLED, INVALID_FILE, CANCELLED.
INVALID_FILE includes an unavailable storage lease/recovery/upload, missing or
wrong-type file, CRC failure or invalid plan. No uncertain command is retried
automatically. Direct, timed, binary servo writes and DEMO1 are rejected while
playback or preparation owns servo state. STOP remains available.

## SAFE_POSE

`SAFE_POSE` cancels DEMO1, stored timeline playback and every timed move, then
atomically queues all 16 values from the saved type-4 safe-pose file. Response:
`OK SAFE_POSE APPLIED\n` if a validated pose was queued, or
`OK SAFE_POSE MISSING\n` if none exists (motion still stops, holding targets).
`APPLIED` acknowledges the target update, not a physical position measurement
or completion of the PCA transfer. Arguments/embedded NUL are rejected with
`ERR SAFE_POSE BAD_ARGUMENTS\n`. `HELP SAFE_POSE` lists the command (the legacy
unqualified HELP response is already at its 255-byte limit).

BLE disconnect applies the same stored pose autonomously and holds PWM/supply
on only after servoTask writes the safe frame. The browser also sends SAFE_POSE
after Code completion/Stop/error and Timeline completion/Stop/unmount, after
settling its outgoing motion commands. Repeating timeline boundaries do not
enter safety between iterations. Missing/corrupt files never supply targets.
See `../display/FILE_STORE.md` for the file format, OTA lifetime and save ordering.

The NUS TX owner strips LF and sends each reply line as one notification. Web
accepts complete notification payloads as well as newline-delimited demo replies.

## Experimental on-board WebAssembly

`HELP WASM` lists the qualification commands on the WAMR branch:

- `WASM_RUN MATH`: one-shot integer/float/double result, expected 3700.
- `WASM_RUN SENSORS`: repeat sensor snapshots every 20 ms plus execution time.
- `WASM_RUN FLOAT`: repeat a float/double self-check, expected 496768 each step.
- `WASM_RUN LOOP` / `WASM_RUN OOB`: intentional instruction-budget/bounds traps.
- `WASM_RUN HOLD`: resubmit CH0's current target through the real servo API.
- `WASM_RUN BAD_CHANNEL` / `WASM_RUN BAD_PULSE`: verify actuator rejection.
- `WASM_STOP`: request cooperative cancellation; `OK` acknowledges the request.
- `WASM_STATUS`: multiline counters/state, terminated by a line containing `END`.

RUN returns `OK WASM_RUN` on queue acceptance, not completion. Only one program
may be active; a second RUN returns `ERR WASM_RUN BUSY`. STOP, SAFE_POSE and real
BLE disconnect also revoke the VM. Poll STATUS for DONE/STOPPED/TRAP/ERROR.
Built-in SENSORS requires the existing BLE modes/subscriptions.

`WASM_INFO` returns `OK WASM_INFO 2 2048 12288 20000 20` followed by
`OK WASM_INFO END`: ABI, maximum module bytes, pool size, instruction budget,
and delay after each step. `WASM_FILE <id>` runs a live type-5 file from the
existing file store; metadata is width/height/layout 0, version 1, length 8..2048.
`OK WASM_FILE <id>` acknowledges queueing only. STATUS includes `id=<id>` (zero
for built-in fixtures), and reports ERROR if the file/profile/load is rejected.
The VM checks CRC, bounds and ABI, copies bytes into its existing pool under a
short Flash lease, then releases it. Start functions are rejected. Uploaded
`step(uint32_t now_ms)` repeats until stop/trap.

ABI 2 programs optionally export `int32_t init(void)`, called once before step.
Return 0 to continue, otherwise ERROR `INIT_FAILED <value>`; init has the same
instruction budget as step. Compiler-supplied `robot_abi_v2` identifies the new
lifecycle. Sensor requests start disabled; `tof_enable(1)` / `imu_enable(1)`
request them asynchronously, and 0 releases them. Readiness must be polled across
steps. Existing ABI 1 files retain automatic sensor activation. All uploaded
programs acquire exclusive motor ownership.
BLE sensor modes take precedence; local sensing does not enable disabled BLE
streams. Direct, all-channel, timed, demo and timeline motion commands are busy
while the program owns motors. STOP/safe pose/disconnect revoke it atomically.
See root `WAMR_BOARD.md`; the companion Web C page and console use this protocol.
