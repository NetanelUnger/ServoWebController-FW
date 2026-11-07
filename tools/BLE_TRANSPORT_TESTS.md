# Software-only BLE transport regression

Run from the firmware repository root:

    python tools/check_ble_transport.py
    python tools/check_ble_transport.py --links 8

The runner locates an installed native GCC/Clang compiler, or Microsoft Visual
Studio Build Tools on Windows. It can also take `--cc <host-compiler>`. It does
not install packages, import Bleak, connect to a board, flash firmware, or move
motors. All generated compiler files are isolated in a temporary directory.

## What actually executes

- `STM32_WPAN/App/ble_tx.c` is compiled directly into the test translation unit.
- The current `NUS_APP_EnqueueRX` function from `Core/Src/app_freertos.c` and
  `NusRxTask` from `STM32_WPAN/App/nus_app.c` are extracted verbatim at test time
  and compiled with source/line references. The actual NUS_PermitRead and
  NUS_PermitWrite helpers from nus.c are compiled the same way. No replacement
  transport, permission-recovery or RX state machine is used.
- The stack, RTOS queue/event APIs, tick clock, and command-parser side effects
  are mocked. The production RX callback has no mocked heap-allocation API:
  an accidental allocation dependency fails the strict host compile.
- Each scenario executes in a new process. Lifecycle changes use the public
  production APIs. Only diagnostic counter extremes are injected directly.
- Every transport step is checked for balanced critical sections, stack calls
  outside critical sections, and nonblocking queue access. A ten-second
  process timeout catches a hung scenario.

## Coverage

The executable prints its exact scenario list with `--list` and emits a PASS
line for every completed case. Cases cover:

- Accepted submission waits for its matching completion and is never retried.
- Wrong, delayed, duplicate, and synchronous completion events; pool exhaustion
  waits for a pool event, including a pool event during the submit call.
- Completion timeout, failed termination retries, pool-timeout teardown,
  disconnect while submitting, and reconnect without a final old completion.
- Reused connection handles reject old-session TX/RX. The real RX worker skips
  the parser for stale commands and releases their reserved response credits.
- CCCD values 0/1/2/3, unsubscribing and resubscribing, changing mode while a
  packet is in flight, two-link subscriptions, and OTA confirmation ownership.
- FIFO reply priority, telemetry latest-value coalescing, rate limits and
  round-robin link fairness.
- Fixed-size RX copying, immediate rejection for invalid/full queues, exact
  five-reply admission capacity, reservation transfer and release on failures.
- Null and invalid stream inputs, malformed CCCD length, MTU 23/247, payload
  length boundaries, oversize replies, and stack submission rejection.
- ATT read/write permission retry after 0x64 without a pool event, preserving
  copied callback bytes and exactly-once command/servo admission. Pending
  control responses precede new notifications, including pool-wait recovery.
- ATT absolute deadlines, expiry before commit, tick-zero rollover, session
  reuse, malformed input, collisions before side effects, fatal/ambiguous
  return values and shutdown with an already accepted notification.
- Reentrant next requests during an accepted controller call; contradictory
  previous results close the link rather than replaying either operation.
- Four-link and eight-link bounds, with round-robin fairness under repeated
  pool wakes and one fixed response slot per bearer.
- Non-1-kHz ticks, tick rollover, and bounded diagnostics at maximum counters
  with actual queue high-water limits.

## Limits

This is deterministic fault injection, not a firmware or browser integration
pass. The mocks do not establish real controller event ordering, radio timing,
peer reception, hardware safety, interrupt latency, or physical servo behavior.
In particular, tests model the serialized dispatcher drain boundary; they
cannot prove the controller never produces an old completion after that
boundary. Actual firmware build, controller/ATT behavior and hardware stress
checks are separate required evidence.

`check_ble_transport_hardware.py` is a separate, explicitly enabled hardware
load harness. Running this software-only test does not run that harness.
