# BLE transport validation

Date: 2026-11-07. Status: **INCOMPLETE: final ATT-image API run passed; browser acceptance and real recovery-branch coverage remain pending.**

This report separates the original Chrome reproduction, deterministic host tests, and direct Bleak API load tests. Overall acceptance remains incomplete. Both later API runs passed, including the final ATT image under every-cycle HELP/PING pressure, but neither exercised the resource-exhaustion path seen in the earlier failure. Build and mocked-test passes do not establish radio stability, browser playback correctness, or observed physical motor motion. Earlier reports and raw evidence are preserved.

## Original Chrome baseline

Source: [validation-timeline-repro-2026-11-07.json](validation-timeline-repro-2026-11-07.json), original investigation chat `01a1b5ab-7f19-7fb3-b34c-f2415e68f85c`.

- Firmware `012eda2`, Web `2b17a9c`, Chrome Web Bluetooth at `http://127.0.0.1:5173`, board `NL802915`.
- The 50-event document uses ten channels, initial 1250 us, target times 3/5/8/10 seconds, and a configured 1300 ms repeat. Twenty events fall in that repeat window.
- **Failed after 96.453 seconds** from the first GATT operation: a servo READ stalled, the browser's 8-second operation timeout fired, and the browser disconnected. There were 420 GATT operations: 241 NUS writes, 108 servo READs, and 71 snapshot writes. Successful latency median/p95/max was 169/420/1060 ms.
- UART recorded 63 TX-pool resource errors (`0x64`), disconnect reason `0x13`, and advertising restart. A later debug capture found an idle CPU with zero fault flags. A permanent MCU hard lock was not demonstrated.
- Comparison with servo notifications disabled ran 191.248 seconds and 1557 operations without errors; median/p95/max latency was 120/159/234 ms. Restoring notifications on that same connection then ran 177.718 seconds and 1318 operations without errors; latency was 124/190/283 ms. These are partial-duration observations, not a five-minute acceptance pass or proof of a unique cause.

## Build and software-only checks

- Application and diagnostic builds completed with **0 errors and 1 pre-existing warning**: `UpdDIS` assigns the const `serial_number` array to mutable `DIS_Data_t.p_Payload` in `app_ble.c`. A prior incremental build showing zero warnings did not recompile this file and is not a clean warning-free build.
- Firmware layout validation passed. The first repaired image was programmed and readback verification passed. Evidence: `%TEMP%\servo-ble-repair-flash-final.log`.
- **31 deterministic host scenarios passed for the diagnostic sources**, using MSVC `/W4 /WX`. This is not validation of the subsequent ATT-response recovery changes, which are covered separately below. The tests compile actual transport and RX code with mocked stack, RTOS, clocks, and parser effects. Coverage includes queue admission, FIFO order, callback matching, pool exhaustion, timeout recovery, stale-session rejection, subscriptions, MTU boundaries, and bounded diagnostics. See [tools/BLE_TRANSPORT_TESTS.md](tools/BLE_TRANSPORT_TESTS.md). These tests do not establish real radio timing or controller event behavior.
- Build logs: `%TEMP%\servo-ble-transport-finalbuild.log` and `%TEMP%\servo-ble-transport-diagnostic-build.log`.

## First repaired-image API load test

**FAILED. This was a real Bleak API test, not browser reproduction.** Sources: [raw API evidence](validation-ble-transport-api-2026-11-07.json), [live postfailure statistics](validation-ble-transport-api-failure-stats-2026-11-07.json), `%TEMP%\servo-ble-repair-api-acceptance.log`, and `%TEMP%\servo-ble-repair-uart-20261107.log`.

The run began at 13:24:18 UTC on address `00:80:E1:2A:26:24`, advertised as `NL802915`. Its dirty worktree HEAD was `b6b5a36b8cb5f4a5018a5cfd752367711486e2dc`; the recorded HEX SHA256 was `5bce03294b6dfefefecdc46619fb21bd561b38c27d00e29d92fd6bf8d8b797bd`, and ELF SHA256 was `e91c3cdd5269c7dd69a0704dbdf150d78eabfea7b3268bb6c3a85a5294fef728`. These hashes identify the artifacts used for this run, not subsequent diagnostic builds.

- Twenty stable servo READs passed and generated **zero extra notifications**.
- The requested soak was 310 seconds. The failing READ began 166.263 seconds into the load loop and returned an error at **168.489 seconds**, approximately 170 seconds. UART recorded `BLE TX timeout conn=0x0000`, disconnect reason `0x16`, and successful advertising restart.
- Before disconnect, all **351 accepted NUS commands had matching checked replies**. The first connection received 459 NUS packets and **6284 telemetry notifications**: servo 1818, ADC 772, IMU 2838, and TOF 856. The harness reported no frame-length failures for the streams it validates.
- Successful servo READ latency median/p95/max was 240.4/481.6/841.3 ms. NUS write latency was 301.1/719.6/1380.0 ms. One servo READ failed with WinRT cancellation error `-2147023673`; the exception text alone does not imply a person canceled the test.
- The configured 1300 ms repeat was only a requested minimum in this harness. It serialized 15 ATT operations per cycle, drained ten command replies, then performed hold work; every tenth cycle also ran HELP/PING. Actual checkpoint averages were roughly **4.8 to 4.9 seconds per cycle** (31 completed at 152.3 seconds). Unlike Web playback, it did not cancel unsent commands at the repeat boundary. Exact per-cycle timing was not recorded in this first run; the harness now records completed and interrupted cycle timings for future runs.
- Cleanup reconnected without a board reset, acknowledged STOP, restored and verified all 16 original values at 1500 us and IMU mode 0, then disconnected. `restoration_verified=true`, with no cleanup errors. Subscription restart and the planned normal-end interruption checks were not reached.

A separate live BLE_STATS read after cleanup, without reset, recorded: `sent=6744`, `done=6744`, `pool=91`, `timeout=1`, `err=1`, `stale=0`, `merge=2231`, `rxrej=0`, `txrej=0`, RX/TX depths 0 and high-water marks 1, free/minimum heap 3376 bytes, unused TX/HCI stacks 3300/1184 bytes, and maximum completion lag 1 ms. `done` is a stack-completion counter, not proof of remote application receipt. These counters confirm a transport timeout despite intact command replies up to the failure.

## Diagnostic-image API retest

**PASSED for this API run; overall repair acceptance remains incomplete.** Source: [diagnostic API evidence](validation-ble-transport-api-diagnostic-2026-11-07.json). The flashed and verified ELF SHA256 was `9027378496522eaeb2694d571bef82016eedc700e609742ea20ee214561feedc`; HEX SHA256 was `c302d84c81be51ab97d6808cd248b833cb51a2705c4f6d07664061b7b45a6174`. The test ran from 13:37:06 to 13:42:56 UTC, including setup and cleanup.

- Sustained API load completed **311.225 seconds and 183 cycles**. Actual cycle mean was **1700.678 ms**, median 1620.641 ms, p95 2041.575 ms, and range 1559.081 to 2522.381 ms. The requested minimum remained 1300 ms; this was not an exact-timing browser reproduction.
- **40 stable servo READs generated zero extra notifications**, in two sets of 20. Across the complete run, 593 servo READs had no errors; successful median/p95/max latency was 119.9/121.9/300.2 ms.
- The first connection checked 1880 NUS command responses. Its one extra accepted request was HELP deliberately abandoned during the interruption test, not an unexplained lost reply. Telemetry subscription stop/start passed. Fresh advertising and reconnect without reset passed, with no stale NUS replies or resumed timed motion.
- The final BLE_STATS snapshot reported `sent=15900`, `done=15900`, `pool=0`, `timeout=0`, `err=0`, `stale=0`, `rxrej=0`, `txrej=0`, and `merge=4886`. RX/TX queue depths were 0 and high-water marks 1; free/minimum heap stayed 3376 bytes, unused TX/HCI stacks were 3340/1184 bytes, and maximum completion lag was 1 ms.
- STOP, original servo snapshot and IMU mode restoration, readback verification, and disconnect all passed. Cleanup reported `restored_and_disconnected`, `restoration_verified=true`, and no errors.

**No TX-pool exhaustion occurred in this passing run.** It therefore did not validate recovery from the earlier resource failure or failed ATT operation. That path remains untested on hardware and unresolved by this evidence. Reduced or suppressed logging can change load and timing; the later pass alone does not prove that the original root cause was fixed. The first failed image, its timeout counters, and raw evidence remain preserved above.

## Diagnostic-image PWM regression

**PASSED on the same diagnostic image**, ELF SHA256 `9027378496522eaeb2694d571bef82016eedc700e609742ea20ee214561feedc`. `tools/check_servo_priority.py` exited 0. The original output is preserved in [PWM regression evidence](validation-ble-transport-pwm-2026-11-07.txt).

- Forty atomic 16-channel snapshots passed under simultaneous ADC, IMU and TOF load. Distinct per-channel targets and actual PCA9685 register readback passed for channels 0 through 15, including the expected ON and OFF register values.
- Timed interpolation progressed, and an immediate command canceled it without the old move resuming. The final channel-0 register check also passed.
- Received notifications were ADC 65, IMU 250 and TOF 69. Snapshot write latency median/max was 119.669/239.550 ms.
- The script's `finally` completed the baseline snapshot write, IMU mode-0 write, and notification shutdown. This cleanup restores the starting command values; it does not provide an additional independent physical-motion observation. **Actual motor movement was not visually inspected.**

This pass is tied to the diagnostic image. A definite missing recovery path for failed ATT replies was subsequently corrected, as described below; it has not been established as the original failure's cause. Hardware acceptance of the resulting final image remains pending.

## Diagnostic-image ADC and IMU regressions

**Both passed with exit code 0 on diagnostic ELF `9027378496522eaeb2694d571bef82016eedc700e609742ea20ee214561feedc`.** These results do not validate the later ATT-response recovery changes.

- `tools/check_adc_ble.py`: 25 valid ADC frames at **4.98 Hz**; NUS `ADC_READ` coexistence passed for battery, ADC1 and ADC2. Characteristic READ, notification stop, stopped-state invalidation, and restart passed. Source: [ADC regression evidence](validation-ble-transport-adc-2026-11-07.txt).
- `tools/check_imu_subscription_switch.py`: all nine stop/write/start/read mode cycles passed in order **1, 2, 0, 1, 0, 2, 1, 2, 0**, with reported transition times 0.180 to 0.840 seconds. Reconnect read OFF, and a mode request queued while unsubscribed did not leak into the new connection. Source: [IMU regression evidence](validation-ble-transport-imu-2026-11-07.txt).
- After these suites the board was disconnected and IMU mode was 0. No final image was yet available for these tests.

## Final ATT recovery image

The bounded ATT-response correction is implemented, and the new image has been flashed with readback verification. **Its API soak and all four broader hardware reruns passed; browser acceptance is still pending.** Image identifiers:

- ELF SHA256: `43911d876ee5bef3770f2fe16aafc449c940fe89b4c56651cf22a6f3efa5c8d2`.
- BIN SHA256: `7a223686e7cb10ffdd8343d81a59fe8f6fb095f78df9079bd1fdb806155cb14b`.
- Incremental build: 0 errors, 0 warnings. This does not remove the earlier clean-rebuild caveat for the existing DIS const warning.
- Layout validation passed: text 348616 bytes, data 1720 bytes, BSS 127368 bytes. RAM-layout end is `0x2001F868`, leaving 1944 bytes after the reserved 4096-byte MSP. These are linker-layout figures, not measured runtime stack margins.

### Bounded ATT response behavior

NUS now reserves a static permission-response slot for the connection before admitting RX or applying a direct-servo write. The slot copies the response bytes and records connection session and request serial; a collision closes the affected session before new side effects. The callback commits the decision, and the transport owner submits it ahead of new notifications, including while notification sending waits for pool space.

Only an explicit unaccepted `BLE_STATUS_INSUFFICIENT_RESOURCES` (`0x64`) response is retried. Pool/completion events wake the retry; because ST does not guarantee a pool event for permission-response APIs, these replies also use a 50 ms probe. The absolute two-second deadline is never extended. Success releases the slot; other or ambiguous errors and expiry close only the matching session. Failed termination is retried at one-second intervals. Session/serial checks prevent a stale return or reused connection handle from clearing newer work.

Retries resend only the copied ATT decision. They do not parse or enqueue the command again, repeat a servo write, or replay an already accepted notification. A lost ACK can still follow an already executed command, so clients must not infer that lost acknowledgement means no side effect. See [transport contract](BLE_TRANSPORT.md). This fixes a definite forgotten-response path; it does not establish that path as the original timeout's cause. **The new ATT recovery is scoped to NUS/servo. ATT permission-failure recovery for other BLE services is unchanged and was not validated here.** Their normal sensor traffic checks are separate from error-path coverage.

### Final software evidence and hardware run status

**59 of 59 compiled production-code scenarios passed at both 4-link and 8-link mock capacities**, using MSVC `/W4 /WX`: the original 31 scenarios plus 28 ATT-recovery scenarios. Tests compile actual `ble_tx.c`, verbatim current RX functions, and NUS read/write permission helpers with deterministic stack, RTOS, parser and servo mocks. Full commands, results and five source hashes are preserved in [final host evidence](validation-ble-transport-host-att-recovery-2026-11-07.txt). The tested `ble_tx.c` SHA256 is `c899278523efd239ed2cb15909537e768d40afa7c9e18740e604e18680205e60`; `nus.c` is `fc0dc0b190035aa0f70a21f9628562bcdf1aa646ebd86d1a625991fd72dd62cc`.

**The final-image API run PASSED**, with HELP/PING after every motor cycle (`--help-every-cycles 1`). Source: [final ATT-image API evidence](validation-ble-transport-api-att-final-2026-11-07.json). The recorded ELF matches the hash above; HEX SHA256 is `97b6b98cc54501a2a854e3ebd919d412a192edd4f0bded3962ab6bda94f1ce30`.

- The soak completed **311.888 seconds and 154 cycles**. Actual cycle mean was **2025.239 ms**, median 1980.185 ms, p95 2280.559 ms, and range 1801.115 to 2760.825 ms. These are measured API timings, not a 1300 ms browser replay.
- Forty stable READs generated zero extra servo notifications. All 506 servo READs completed without errors; median/p95/max latency was 119.3/180.1/419.0 ms.
- The first connection checked 1862 NUS command responses; its one additional accepted HELP was deliberately abandoned during the interruption test. Subscription stop/start, fresh advertising, reconnect without reset, absence of stale replies or resumed timed motion, STOP, state restoration/readback, and final disconnect all passed.
- Final BLE_STATS reported `sent=17765`, `done=17765`, `pool=0`, `timeout=0`, `err=0`, `stale=0`, `rxrej=0`, `txrej=0`, `cp=0`, `cr=0`, and `cf=0`. Free/minimum heap was 3376 bytes; unused TX/HCI stacks were 2820/1200 bytes, maximum completion lag was 1 ms, and `merge=4400`. Cleanup reported `restored_and_disconnected`, `restoration_verified=true`, and no errors.

**`cr=0` means no real ATT retry occurred; `pool=0` means no real TX-pool exhaustion occurred.** The new hardware recovery branch was therefore not exercised, despite the greater reply pressure. Recovery behavior has deterministic fault-injection coverage in the 59 mocked scenarios, not demonstrated on-board failure recovery. This passing run does not establish the original failure's cause or replace the pending browser reproduction. All four final-image broader hardware reruns subsequently passed, as recorded below.

## Final-image broader hardware regressions and board state

**All four suites exited 0 on final ATT ELF `43911d876ee5bef3770f2fe16aafc449c940fe89b4c56651cf22a6f3efa5c8d2`.**

- [Servo-characteristic regression](validation-ble-transport-final-servo-characteristic-2026-11-07.txt): with PCA control enabled, non-notifying servo READ, current-value write acceptance, malformed write rejection, HELP/PING replies, and four 64-byte TOF frames passed. The optional PCA-disabled branch was not exercised by this enabled-PCA run.
- [PWM regression](validation-ble-transport-final-pwm-2026-11-07.txt): 40 atomic 16-channel snapshots, distinct channel targets, actual PCA9685 ON/OFF register readback for channels 0 through 15, timed interpolation, and immediate-command cancellation passed under ADC/IMU/TOF load. Notification counts were 65/249/69; snapshot write latency median/max was 119.514/179.699 ms. The baseline snapshot and IMU mode 0 were restored in cleanup.
- [ADC regression](validation-ble-transport-final-adc-2026-11-07.txt): 25 valid frames at **4.99 Hz**, battery/ADC1/ADC2 NUS coexistence, READ, stopped-state invalidation, and notification restart passed.
- [IMU regression](validation-ble-transport-final-imu-2026-11-07.txt): all nine mode transitions **1, 2, 0, 1, 0, 2, 1, 2, 0** passed; fresh reconnect read OFF and cleared the pending unsubscribed-mode request. Reported transition times were 0.330 to 0.781 seconds.

The subsequent independent [final board-state read](validation-ble-transport-final-state-2026-11-07.json) confirmed **all 16 servo values at 1500 us, IMU mode 0, and disconnected=true**. Its BLE_STATS snapshot had `sent=18308`, `done=18308`, `pool=0`, `timeout=0`, `err=0`, `stale=0`, `rxrej=0`, `txrej=0`, `cp=0`, `cr=0`, and `cf=0`; free/minimum heap was 3376 bytes, unused TX/HCI stacks 2820/1200 bytes, and completion lag 1 ms.

These are real protocol, sensor, and PWM-register checks. **Actual motor movement was not visually inspected.** The unchanged zero retry and pool counters also leave real resource-exhaustion recovery unexercised; the mocked error-path results and the preserved first hardware failure still require their separate qualifications.

Final evidence is archived in the repository: [complete UART capture](validation-ble-transport-uart-2026-11-07.log) and [final programming/readback verification](validation-ble-transport-flash-final-2026-11-07.log). The UART capture was stopped and COM4 released. Final HEAD remained `b6b5a36b8cb5f4a5018a5cfd752367711486e2dc`, with no commit or push; the final ELF hash remained `43911d876ee5bef3770f2fe16aafc449c940fe89b4c56651cf22a6f3efa5c8d2`. `git diff --check` reported no whitespace errors.


## Remaining browser and error-path validation

**PENDING. Diagnostic and final-image API passes are recorded above; no overall acceptance claim is made.**

- The original investigation chat resumed with explicit user approval. Chrome DevTools and the native Computer Use tools worked, but a Windows UAC prompt blocked access to the Bluetooth chooser. No board was selected or connected, and **real browser playback ran for 0 seconds**. READ, command-reply, 300-second playback and reconnect tests were not executed. Evidence: [browser blocker and cleanup](validation-browser-blocked-2026-11-07T1337Z.json). This attempt establishes neither a firmware pass nor a firmware failure.
- The pending chooser was canceled by reloading the same app tab. At 13:37:37 UTC, Chrome was verified **Disconnected / Ready**, with Connect enabled and Stop disabled. The original 50-command timeline, ten channels and configured 1300 ms repeat remained present; local storage was not cleared or overwritten. Motor values were not changed.
- User handling of the UAC prompt is pending, followed by a coordinated browser retest. Preserve the timeline and record at least 300 seconds with notifications enabled, actual elapsed/cycle timing, command replies, READ latency, UART diagnostics, interruption, and disconnect/reconnect behavior.
- The blocked browser attempt reported the older image hash without independent verification and did not test either image. Browser acceptance must identify the actual final image independently; the diagnostic API pass cannot substitute for it.
- Capture the failure state and distinguish pool-wait timeout from accepted-transmission completion timeout. Repeat acceptance testing against the final identified image after any correction. Updated API harness recovery reads BLE_STATS before restoration and preserves the original failure if diagnostics also fail.
- Final-image API, servo-characteristic, PWM, ADC and IMU checks have passed, and final restored/disconnected state is independently recorded above. Browser reproduction and real NUS/servo resource-failure recovery remain separate outstanding evidence. ATT recovery for other services is outside this correction's validated scope.
