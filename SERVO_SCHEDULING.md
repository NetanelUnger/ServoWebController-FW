# Motor ownership and scheduling

Previously, command handlers and servoTask both wrote PCA9685 registers. The
worker could snapshot an old value, yield during I2C, then overwrite a newly
received command with that stale snapshot. Hardware testing reproduced four lost
updates in twelve alternating binary writes.

Now command handlers atomically update desired values and wake servoTask. Only
servoTask writes normal PWM outputs. It clears a pending bit before the I2C write;
a command arriving during that write sets the bit again and remains pending. It
never copies an old I2C snapshot back into desired state. A successful GATT write
means the command was accepted; it does not prove motor motion or power delivery.
Timed moves use elapsed scheduler time instead of assuming each loop took 20 ms.
When any channel is dirty, the worker captures all 16 targets and sends one
64-byte auto-increment transaction to LED0..LED15. MODE2.OCH=0 latches these
registers together on the same I2C STOP, so TOF/IMU transactions cannot interleave
between channels. No frame is sent when all channels are unchanged.
The Web `updateServos` helper reads the current snapshot, patches the requested
channels and writes one 32-byte characteristic value (16 little-endian uint16s),
even when only ten motors change. The FW rejects incomplete payloads. This is
desired-state scheduling: a newer complete snapshot may supersede a pending one.
The task never splits an accepted snapshot into individual motor transactions.
PCA initialization/recovery keeps OE disabled until a full desired frame succeeds.

IMU mode 0 (also selected when notifications are disabled) performs its shutdown
configuration once, then returns without sensor reads. TOF without notifications
stops ranging once and performs no polling/init; the old debug force-ranging
bypass has been removed. An already-started sensor transaction must finish before
the servo task obtains the bus. BLE High3, servo High2 and command High1 priorities
keep sensor/display work below the motor path; the radio retains higher priority.
This covers the binary BLE command containing all 16 target values as well as
DEMO1. The same `i2cMutexHandle` protects PCA9685, IMU, TOF, readiness probes and
bring-up diagnostics. It remains held through the completion interrupt, or
through STOP/abort and peripheral recovery on timeout. Foreign-handle callbacks
cannot complete the current transfer. `tools/test_i2c_mutex.c` injects completion,
NACK, missing completion and stuck-abort cases and checks ownership on every exit.

`DEMO1` / `demo1` starts all 16 outputs at 500 us and toggles to 2000 us and back
every 2000 ms. Repeated starts are idempotent. `STOP` / `stop` cancels the demo and
timed interpolation, holding the last pulses. Manual text and binary servo writes
are rejected while DEMO1 is active. BLE disconnect cancels motion and disables
PWM/power; reconnect never resumes the demo. HELP lists both commands.

The RTC binary counter uses LSE 32768 Hz / (AsynchPrediv 7 + 1) = 4096 Hz.
`RTC_N_PREDIV_S` must therefore be 12. The previous value 10 assumed 1024 Hz,
making software timers too fast (DEMO1 was observed near 1.5 seconds). DEMO1 now
measures elapsed raw RTC ticks rather than counting quantized scheduler callbacks.
`tools/check_demo1_ble.py` verifies BLE intervals, all 16 PWM registers at both
endpoints, STOP/busy/argument handling, and cancellation across reconnection.

| Worker | CMSIS priority | Purpose |
| --- | --- | --- |
| Radio link layer | Realtime2 | Radio deadlines |
| BLE host / event dispatch / BLE timer | High3 | Receive commands promptly |
| Servo | High2 | PCA PWM writes and timed motion |
| Text commands | High1 | Receive before queued text replies |
| TOF | AboveNormal | Sensor work |
| Default | Normal | IMU polling and ADC window publication |
| Display | BelowNormal | Animation |

The shared I2C mutex uses priority inheritance. TOF accesses are already chunked
at 256 bytes: an in-progress bus transaction must finish before motor access.
Raising priorities does not bypass physical I2C transfer time or BLE connection
intervals. Motor writes do not perform ADC conversions or wait for telemetry.

`PWM_GET <0..15>` returns `OK PWM_GET <channel> <on> <off>`, read directly from the
PCA9685 channel registers. Values include bit 12 FULL_ON/FULL_OFF; normal servo
outputs have ON=0 and OFF=round(pulse_us * 50 * 4096 / 1000000).

Validation: `tools/check_servo_priority.py` sends small alternating steps, checks
all commanded values, and reads actual registers on CH0..15 while ADC, IMU and TOF
notifications are active. It restores initial targets. Physical motion still
requires observation; correct register readback does not verify the servo supply.


## Controller-stored timeline playback

The TML1 plan is validated once before acceptance and accessed through a
memory-mapped read-only flash lease. Execution belongs to the existing servoTask;
there is no second PCA writer or new RTOS task. Events due together update one
coherent desired frame. Linear moves derive from absolute logical event times,
including an exact logical starting value, rather than accumulating rounded
worker delays. RTC ticks are extended into uint64 elapsed time, so uint32
millisecond documents and repeated playback are not limited by one RTC wrap.
This assumes the task runs at least once per full hardware timer wrap.

While active the task waits at most one FreeRTOS tick (nominal 1 ms at configured
1000 Hz), then interpolates and writes only a changed coherent PCA frame. The
RTC is the timeline clock, not that scheduler tick. Actual dispatch/output jitter
includes RTOS/radio preemption, I2C mutex contention, frame transfer duration and
PCA9685 PWM frame phase. The servos themselves have no position feedback. This is
millisecond-resolution scheduling, not guaranteed millisecond physical motion
or hard real time. Hardware measurement under sensor/radio load is still needed.

Repeat advances its absolute origin by the specified period, clips unfinished
ramps at the boundary, holds their interpolated value and applies time-zero
commands again. Events at the repeat boundary execute before time-zero events restart. Repeat
may extend beyond the last command/guide, holding the current values. A safety work budget
allows at most 64 overdue events per wake; two missed repeat boundaries or an
exhausted budget freezes at the last computed value and reports FAULT rather
than executing an unbounded catch-up burst with interrupts masked. Dense plans
or extremely short loops may therefore fault under load. An I2C frame failure
also cancels the timeline and reports FAULT, with the existing PWM-ready gate.

STOP/disconnect cancel pending preparation as well as active playback. Disconnect
keeps the existing electrical output gate protection. A STOP cannot retract a
frame transfer already in progress. COMPLETE indicates logical plan completion,
not measured motor arrival. All flash-store operations and OTA require playback
to stop first. No automatic motion resumes after reconnect or reboot.


Due events with different timestamps can be advanced logically within one worker
wake and coalesced into one physical PCA frame containing the latest values.
Intermediate desired values are not guaranteed to appear as separate PWM frames.
Consequently max_late_ms is a dispatch metric, not measured physical output
jitter, and there is no strict delivery guarantee for events spaced below the
I2C/PWM cadence (PCA servo frequency is currently 50 Hz). Validate the required
sequence on hardware before relying on closely spaced actions.

Regression sources: `tools/check_timeline_host.py` builds the actual servo
source with a fake clock/PCA/store and the exact decimal ID parser; its C cases
are in `tools/timeline_host_test.c`. `tools/check_file_store.py` adds actual
storage-C emulation for mixed image/timeline files, CRC, validation and leases.
These passed on 2026-11-07 after explicit permission. They do not contact hardware.
See `validation-timeline-verification-2026-11-07.json` for software results and the
SWD hardware blocker. Physical output timing is still unverified.
