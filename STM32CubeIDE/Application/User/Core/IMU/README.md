# LSM6DSV320X integration and next-stage interface

## Implemented

Official ST platform-independent C driver, unchanged, BSD-3-Clause license.
Pinned revision: `57ce0f99d048b1a30a7d82bc72e5f9cb45b5dca1`.
Source: https://github.com/STMicroelectronics/lsm6dsv320x-pid

The installed device responds at 7-bit I2C address `0x6B`, WHO_AM_I `0x73`.
I2C3 uses the existing shared-bus mutex wrappers. `DISABLE_I2C_IMU=1`
compiles out all IMU bus activity; the default is `0`.

`IMU_Step` is owned by the existing defaultTask (20 ms requested loop delay).
No new RTOS task or heap allocation is needed. BLE callbacks update subscription
and requested-mode state. Sensor configuration and reads happen in thread context.
The module lazily initializes on a nonzero mode request while subscribed, powers
down its motion engines after unsubscribe/disconnect, and retries I2C/setup
failures. It waits 250 ms for RAW or 1000 ms for SFLP after starting, then discards
the first held sample to avoid turn-on transients.

Fixed RAW profile: low-g ±4 g and gyro ±2000 dps at 120 Hz, high-g ±320 g at
480 Hz, block-data-update and register auto-increment enabled. Register reads
return the latest samples, not FIFO history. Streams are not captured atomically
across all engines. Under current servo/display/bus load delivery was around
11 snapshots/s in the initial test; neither 120 nor 480 complete samples/s are
promised over BLE. Temperature may be older than the motion data.

## Mode control and notifications

Control characteristic: `91bad4a3-b950-4226-aa2b-4ede9fa42f59`, read/write with
response. Write exactly one binary byte: `0` OFF, `1` RAW, `2` hardware SFLP.
ASCII digits and other values/lengths are invalid. Read returns the actual applied
mode, not a pending request. A write can be accepted while notifications are off;
the worker applies a nonzero request only once notifications are enabled.
Boot, unsubscribe and disconnect select OFF. Settings are not persisted.
Disconnect also clears requests made while unsubscribed. The Web changes modes
by stopping notifications, writing the mode, resuming notifications and polling
applied-mode readback. An event missed during the switch does not block the UI.

ATT write success means accepted, not yet configured. The existing data
characteristic emits a 48-byte mode event once the worker has applied the request,
or on error. A repeated request also produces an acknowledgement. Mode 0 stops
low-g, high-g, gyro and SFLP; the status event is the last notification, not a
measurement. SFLP uses low-g/gyro at 120 Hz and disables high-g. Allow one second
for fusion settling. RAW retains the original range and packet format below.

The first byte distinguishes 48-byte packets: `1` RAW, `2` SFLP, `3` mode event.

| Offset | SFLP packet (type 2) | Mode event (type 3) |
|---|---|---|
| 1 | Valid bits: quaternion/gravity/bias = 0x07 | Status: 0 applied, 2 configuration error |
| 2 | WHO_AM_I 0x73 | Requested mode |
| 3 | I2C address 0x6B | Actual mode |
| 4 | u32 sample sequence | u32 request generation |
| 8 | u32 MCU time in ms | u32 MCU time in ms |
| 12 | u32 sensor timestamp ticks | Reserved zero |
| 16 | Four little-endian IEEE binary16 values W/X/Y/Z | Reserved zero |
| 24 | Three i16 gravity values, 0.000061 g/count | Reserved zero |
| 30 | Three i16 gyro-bias values, 0.004375 dps/count | Reserved zero |
| 36 | SFLP ODR enum 3 = 120 Hz; bytes 37..39 zero | Reserved zero |
| 40 | u32 cumulative I2C/setup errors | Same |
| 44 | u32 cumulative BLE update failures | Same |

SFLP uses ST's dedicated output registers, as in its sensor-fusion example. It
does not yet use FIFO batching; separate vectors are snapshots, not guaranteed
atomic across engine updates. Quaternion convention is scalar-first WXYZ, with
the application-axis mapping below. Absolute board mounting alignment beyond
this fixed mapping is not calibrated. The Web decodes binary16, rejects invalid quaternion norms,
normalizes for display, and derives roll/pitch/yaw for a bounded running graph.
In RAW mode the 3D body only estimates gravity tilt, with yaw fixed to zero;
linear acceleration can corrupt that estimate. In OFF mode live history clears.

## Application axes

Firmware maps all outgoing motion vectors from the native sensor axes to
`(X, -Y, -Z)`: low-g ACC, gyro, high-g ACC, SFLP gravity and gyro bias.
The mapping is applied once when encoding BLE packets; the official ST driver
and the internal SFLP algorithm keep their native coordinates. Signed 16-bit
`-32768` saturates to `+32767` when negated, avoiding sign wrap at full scale.

For orientation, both the body and reference bases are mapped by
`C = diag(1, -1, -1)`: `R_app = C R_sensor C^-1`, or in scalar-first form
`q_app = (w, x, -y, -z)`. This reverses Y/Z rotation directions while preserving
X and the identity pose. Binary16 quaternion components change only their sign
bits, preserving precision and quaternion norm. This is an axis convention,
not bias calibration, pose zeroing, or an added 180-degree display offset.

Gravity is expressed in the remapped axes: sensor gravity near `+Z` is now
reported near `-Z`. A consumer deriving absolute tilt from RAW acceleration must
take that reference convention into account; RAW tilt is not the SFLP reference
pose. Packet lengths, types, units, control modes and UUIDs are unchanged.

## RAW contract v1

Service: `91bad4a0-b950-4226-aa2b-4ede9fa42f59`.
RAW characteristic (read + notify): `91bad4a1-b950-4226-aa2b-4ede9fa42f59`.
The NUS advertised UUID remains unchanged. Web Bluetooth must request IMU in
optionalServices; reconnect after updating the Web application. A notification
requires ATT MTU at least 51 bytes. No fragmentation is implemented.

Every frame is 48 bytes, little-endian. Read returns the latest cached packet,
which may be RAW, SFLP or a mode event. Before the first event it is invalid;
after stopping it contains a type-3 event reporting mode 0.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u8 | Version = 1 |
| 1 | u8 | Fresh flags: bit0 ACC, bit1 gyro, bit2 high-g, bit3 temperature, bit4 timestamp |
| 2 | u8 | WHO_AM_I = 0x73 |
| 3 | u8 | I2C address = 0x6B |
| 4 | u32 | Sample sequence, including failed BLE updates |
| 8 | u32 | MCU HAL time, milliseconds |
| 12 | u32 | Sensor timestamp ticks, nominal 21.7 microseconds per tick |
| 16 | i16 x 3 | Low-g X/Y/Z raw counts |
| 22 | i16 x 3 | Gyro X/Y/Z raw counts |
| 28 | i16 x 3 | High-g X/Y/Z raw counts |
| 34 | i16 | Die temperature raw count |
| 36 | u8 | Low-g full-scale ST enum = 1 (4 g) |
| 37 | u8 | Gyro full-scale ST enum = 4 (2000 dps) |
| 38 | u8 | High-g full-scale ST enum = 4 (320 g) |
| 39 | u8 | UI ODR ST enum = 6 (120 Hz); high-g fixed at 480 Hz |
| 40 | u32 | Cumulative I2C/setup errors |
| 44 | u32 | Cumulative BLE update failures before this frame |

Conversion: ACC counts × 0.000122 g; gyro × 0.070 dps; high-g × 0.010417 g;
temperature = 25 + counts/256 °C. These are unit conversions, not fusion or
calibration. Vector counts use the application-axis mapping above; they are not
bit-for-bit native register values. Timestamps and counters wrap modulo 2^32. BLE update success is stack
acceptance, not proof the remote application received the notification.

## Further configuration (not implemented)

The current implementation multiplexes RAW, SFLP and mode events on `...a1`,
and uses `...a3` for mode control. Additional capabilities/status and model upload
characteristics remain future work; do not assume they exist in current GATT.

| Stream | Available information | Required work |
|---|---|---|
| RAW | Low-g, high-g, gyro, temperature, timestamps | Implemented |
| SFLP | Game-rotation quaternion, gravity vector, gyro bias | Implemented via mode 2; FIFO batching remains future work |
| Host derived | Roll/pitch/yaw and acceleration minus gravity | Euler angles implemented in Web; linear acceleration and board alignment remain future work |
| Motion events | Steps, tilt, wake-up, free-fall, 4D/6D, taps, high-g shock | Configure thresholds, duration, event source and reporting |
| FSM | Application-specific motion sequences and impact tracking | Load a reviewed FSM program and expose output flags |
| MLC | Activity class and exportable features | Train/load a task-specific model; report model identity and class mapping |
| ASC | Automatic configuration transitions driven by sensor decisions | Define allowed states and report actual active configuration |
| External MAG | Magnetic heading / external magnetic raw samples | Requires a separately fitted magnetometer; none exists inside this IMU |

SFLP game rotation is six-axis fusion: yaw is relative and may drift, not an
absolute compass heading. Linear acceleration is a derived subtraction, not an
additional physical sensor. Motion does not directly provide reliable absolute
position: double integration accumulates bias/drift. MLC is not a pre-trained
general-purpose AI recognizer and should not expose meaningless class numbers.

Suggested settings: low-g range 2/4/8/16 g; high-g range 32/64/128/256/320 g;
gyro range 250/500/1000/2000/4000 dps; separate engine ODRs, power modes, filters,
FIFO batching, BLE report rate, axis mapping, calibration offsets and event
thresholds. Allow only validated combinations from ST constraints.

Control transactions should contain a schema version and request ID. Queue them
to the sensor owner, stop/drain streaming, validate/apply/read back settings, then
acknowledge with actual configuration and a new configuration generation ID.
Every subsequent frame must identify its generation and scaling. Reject unknown
fields; do not expose unrestricted register writes from Web. Keep settings in RAM
by default; make persistence an explicit operation to avoid flash wear.

Recommended next work: (1) FIFO batching, (2) validated runtime
range/rate/filter controls, (3) calibration and basic event thresholds, (4) FSM/MLC
model loading with a version/hash and a class dictionary. Use batching/decimation
for high-rate recording and measure bandwidth alongside TOF and servo traffic.

## References and verification

- ST product capabilities: https://www.st.com/en/mems-and-sensors/lsm6dsv320x.html
- ST examples (polling, FIFO, SFLP, FSM, MLC): https://github.com/STMicroelectronics/STMems_Standard_C_drivers/tree/master/lsm6dsv320x_STdC/examples
- `tools/check_imu_ble.py` validates identity, packet layout, time progression,
  I2C error count and unsubscribe/resubscribe on hardware.
- `tools/check_imu_modes.py` passed on hardware with transitions 1/2/0/2/1/0,
  applied events, mode readback, rejected 3/255 values, valid unit quaternions,
  gravity magnitude near 1 g, and no measurement notifications while OFF.
- `tools/check_imu_subscription_switch.py` passed nine stop/write/start/read
  transitions and a reconnect after a pending unsubscribed request. A fresh BLE
  advertisement is required to distinguish real disconnection from Windows
  retaining a shared GATT link. The new connection read OFF and stayed OFF after
  subscribing, confirming that the previous request was cleared.
- Initial real-board observation: ACC about [0.050, -0.032, 0.985] g,
  settled gyro about [-0.14, 0.21, 0.14] dps, die temperature about 26 °C.
- CubeIDE discovers this physical source directory when regenerating Debug
  makefiles. Keep both ST register files and LICENSE together.

Web source adds mode selection, hardware-acknowledged transitions, RAW/SFLP
decoding, running graphs, a quaternion-driven 3D board and equivalent Demo packets.
Range/filter/model configuration remains future work. The updated Web application
was built and deployed by its Azure GitHub Actions workflow. Browser Demo checks
passed mode transitions, disconnect during a switch, cleared plots and OFF on
reconnect; no local Web build or lint commands were run.
