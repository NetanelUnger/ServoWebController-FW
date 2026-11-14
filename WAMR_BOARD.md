# WAMR on the STM32WBA5MMGH

Experimental branch based on RAM checkpoint `ac8e374`. This is an **on-board
interpreter integration** with a restricted uploaded-program ABI, not a qualified
balancing controller. The board keeps its existing BLE, sensor, display, storage and servo
tasks. A permanent, statically allocated `wasmTask` at CMSIS `osPriorityLow`
runs one embedded fixture or uploaded type-5 Wasm file on request. It is idle after boot.

## Build and run

The runtime source is pinned to WAMR revision
`ffc22889948b1f063d7258d34d4e636ee0914925` (Apache-2.0 WITH LLVM-exception),
from <https://github.com/wasm-micro-runtime/wasm-micro-runtime>.
It is the revision used in the earlier feasibility probe, not a qualified stable
release. Preserve the upstream LICENSE and notices in that checkout.

Prepare the ignored dependency once, if it is not already present:

```powershell
git clone https://github.com/wasm-micro-runtime/wasm-micro-runtime.git artifacts/wamr/source
git -C artifacts/wamr/source checkout ffc22889948b1f063d7258d34d4e636ee0914925
```

Install CMake and Ninja on PATH alongside STM32CubeIDE. Both Debug and Release
invoke `tools/build_wamr.ps1` as a pre-build step. It rejects a different revision
or modified tracked upstream files, and builds `librobot_wamr.a` with the same
Cortex-M33/Thumb/FPv5 hard-float ABI as the firmware. The archive is an explicit
link dependency, including for incremental builds. `tools/build.ps1` remains the
full firmware/bootloader build entry point. Direct IDE builds use its selected
ARM compiler directory; the standalone helper also accepts `-CubeIdeRoot`.

The checked-in `robot_guest.inc` is generated from `tools/wamr_board/guest.c`.
Clang with its sibling wasm-ld is only needed when changing that guest:

```powershell
./tools/wamr_board/build_guest.ps1 -Clang 'C:/path/to/llvm/clang.exe'
./tools/build.ps1
```

Use the existing NUS/Web text console:

```text
HELP WASM
WASM_RUN MATH
WASM_STATUS
WASM_RUN SENSORS
WASM_STOP
WASM_STATUS
```

RUN acknowledges queue acceptance, not completion. STATUS ends with `END` and
reports IDLE/QUEUED/LOADING/RUNNING/DONE/STOPPED/TRAP/ERROR. Only one run is allowed.
MATH returns 3700. SENSORS repeats reads; FLOAT repeats a float/double check
(496768). LOOP deliberately exhausts its instruction budget; OOB deliberately
accesses invalid memory. HOLD passes CH0's current target through the real servo
producer API without choosing a different target; BAD_CHANNEL/BAD_PULSE return
-1 without changing outputs. STOP acknowledges cancellation; poll STATUS for
cleanup completion. The Web console parser/builder matches these commands;
Web demo mode reports HARDWARE_REQUIRED instead of pretending to run WAMR.

## Ownership and limits

- Classic interpreter, software bounds checking, instruction metering, shrunk
  linear memory, no JIT/AOT/WASI/libc imports/guest threads. Only Wasm 1.0 features.
- One 12,288-byte static pool holds module copy, loader objects, linear memory,
  instance and 2,048-byte Wasm execution stack. No runtime fallback to FreeRTOS
  or libc malloc. One separate 4,096-byte native task stack; existing stacks,
  queues, 32,000-byte FreeRTOS heap and 4-KiB MSP reservation remain unchanged.
- The loader modifies bytecode, so the embedded Flash module is copied into the
  pool before loading. Every exit unloads/frees all objects in the owning task.
- At most 20,000 interpreter instructions per `step`. Repeating programs delay
  20 ms **after** each step; that is not a guaranteed 50-Hz control period.
  Instruction budget is not a wall-clock deadline, nor a parsing-time bound.
- Stop requests revoke the native motor API atomically. STOP, SAFE_POSE and
  disconnect call the servo cancellation hook. The task exits the bounded call
  and frees its own resources; it is never forcibly deleted or suspended while
  holding a native mutex. Higher-priority tasks can delay cleanup.
- Sensor imports use a coherent snapshot captured before each step. TOF invalid
  zones return -1, unavailable IMU words return zero; data older than 500 ms is
  unavailable. IMU packet words preserve the existing versioned BLE encoding.
  APIs are documented in `tools/wamr_board/robot_api.h` (experimental ABI 0).
- Built-in SENSORS uses BLE sensor modes. Uploaded ABI 1 programs request local
  IMU/TOF sensing and exclusive motor ownership; BLE sensor modes take precedence.
  Disabled BLE streams stay disabled. No program continues after disconnection.
- The scalar native API exposes no guest pointers or direct hardware registers.
  Servo index/pulse validation is explicit; existing connected/demo/timeline
  checks remain. Only servoTask performs ordinary PWM I2C writes. HOLD rejects a
  target made stale by a concurrent BLE command between its GET and SET.

## Hardware evidence: earlier embedded-fixture build

Final linked Debug image (`-Os`, hard-float):

| Budget | RAM-optimized main | With WAMR |
| --- | ---: | ---: |
| GNU size text + data | 334,480 B | 398,756 B |
| GNU size data + BSS (includes reserved MSP) | 108,220 B | 125,172 B |
| `_end` to reserved MSP (libc region) | 22,808 B | 5,864 B |
| FreeRTOS heap free/minimum, live | 3,376 B | 3,376 B |

The libc region is separate from both the FreeRTOS heap and WAMR pool. Keep it
available for the existing BLE timer/event allocations. The 488-KiB application
Flash slot still has 100,952 bytes beyond the 398,760-byte binary (four bytes
of alignment beyond GNU size's text + data).

Application-only SWD programming and readback verification; no mass erase or
bootloader update. Evidence is under ignored `artifacts/wamr_board/`. The exact
image for the first full run is `tested-v1.*`, identified by `hardware.json`'s
binary SHA256. A small subsequent HOLD race guard is validated separately in the
final run. Runtime port/configuration and guest are the same in both builds.

The first full physical run passed:

- Actual ARM execution of compiled C, float/double math, real sensor imports,
  current-target servo API and invalid-channel/pulse rejection.
- Infinite loop trapped in approximately 10 ms; out-of-bounds memory trapped.
  Twenty LOOP/OOB/MATH recovery cycles returned the same results and allocations.
- SENSORS: 2,932 steps in approximately 60 seconds, 193,512 native calls.
- FLOAT: 2,364 steps in approximately 60 seconds, alongside real SFLP IMU
  quaternion validation, TOF, ADC, servo writes/readback and NUS traffic.
- 1,622 IMU, 625 TOF, 538 ADC and 551 servo notifications; no malformed frames
  or increased transport rejection/error/timeout counters. FreeRTOS free/minimum
  stayed at 3,376 bytes.
- Runtime pool peak 9,000/12,288 bytes. Cleanup returns to the initial 24-byte
  runtime registration allocation, then destroys the runtime. Native VM stack
  untouched margin 2,848/4,096 bytes in the tested paths.
- Requested normal stop-to-cleanup took 10–20 ms, measured using the board's
  coarse HAL clock. Real radio loss also stopped the program, but its cleanup
  took **720 ms** on that run while higher-priority work was active. Revocation
  prevents further motor writes even when cleanup is delayed. These are measured
  examples, not hard timing guarantees or a complete FPU-register coverage test.

The host servo/timeline/safe-pose regression also exercises the new cancellation
hook. The build retains the three existing application warnings; a clean runtime
build also emits two upstream GCC native-stack-watermark warnings. Those record
stack addresses for watermarking; no warning-free upstream qualification is
claimed. Web changes are reviewed as source only, per the Web repo instructions.

The final image includes the HOLD race guard. `final-hardware.json` passed the
same fault/cleanup/restart cases and two additional 20-second soaks (990 sensor
steps and 793 float steps). Its SHA256 is
`593f1f6dd9fc245b33b5307d35a1f3ca867b2d625e2e59b1519ea3b35837b1b1`.
Pool peak and native stack margin remained 9,000 and 2,848 bytes; regular stop
took 10 ms in both final soaks and link-loss cleanup took 710 ms. The 59-scenario
compiled production BLE transport regression also passed.

Final file-store regression passed RGB332 upload/read/show, a 4,096-event
timeline upload/read/play/stop and temporary safe-pose save/read/apply. Temporary
files were deleted and original files 6/7 remain byte-identical to their backups
from before RAM optimization. Full Flash readback verified the unchanged
bootloader, those original payloads and **every programmed HEX byte**. HEX leaves
four alignment bytes at `0x08006164..0x08006167` erased (`FF`), whereas objcopy's
BIN represents that unused gap as zeros; it is excluded from the HEX comparison.

`final_ram.json`, after both VM and storage tests, reports 16 tasks, FreeRTOS
free/minimum 3,376 bytes, libc capacity 5,864 bytes with 444 bytes committed and
5,420 uncommitted. WAMR stack untouched margin is 2,848 bytes; comm 2,224, default
2,536, servo 1,624, TOF 3,044, display 788 and Flash Manager 528 bytes. This is
observed use, not a bound for arbitrary future modules. After debug sampling the
board was reset and left advertising with the VM idle. The RAM-only rollback
image remains at `artifacts/ram_optimization/optimized.hex`.

Reproduce the board checks (requires Bleak/WinRT in `.venv-ota`):

```powershell
.venv-ota/Scripts/python.exe tools/check_wamr_hardware.py --run --cycle-radio --seconds 60 --output artifacts/wamr_board/hardware.json
```

`--cycle-radio` temporarily interrupts all Windows Bluetooth links and restores
the radio. No persistent files are written by this harness. Use the existing
RAM snapshot tool only after BLE tests, then reset before more RF testing.

## Uploaded-program ABI 1, 2026-11-14

The companion Web C page compiles locally with pinned Clang/LLD WASM assets,
uploads a CRC-checked type-5 file, and sends `WASM_FILE <id>`. `WASM_INFO`
identifies ABI 1 and limits. See `comm/commands.md` and Web `WAMR_CONSOLE.md`.

`wasm_profile.c` bounds module size (2048 bytes), types/functions/imports/globals,
locals, data (1024 bytes) and C stack/linker bounds (1024-byte stack, heap base
<=2048). Only five scalar robot imports with exact signatures and a defined
`int32_t step(uint32_t)` are accepted. Start/element sections, imported memory,
WASI, unknown imports and extended features are rejected. LLD's one fixed null
table slot is accepted. WAMR validates bytecode using only the existing 12 KB
pool. Allocation failure never falls back to another heap. Passing preflight
does not guarantee that every module fits the pool.

An exclusive storage lease protects CRC checking and the mutable RAM copy;
timeline release cannot clear it. After copying, deleting the source file does
not invalidate the program. Uploaded programs activate local sensing and own
motor commands exclusively. Cleanup releases both; STOP/disconnect revoke
writes before cleanup. Disabled BLE sensor streams remain disabled.

The new firmware builds. Actual browser outputs pass native profile tests,
invalid profiles, truncation and mutation fuzz. Servo ownership, 59 BLE transport
scenarios and ARM-emulated storage/power-cut tests pass. Evidence is in
`artifacts/wasm_upload/`. The initial SWD attempt found no target; the user then
connected the board and the new image was flashed and verified.

The uploaded-program tests passed on physical hardware: persistent counter,
sensor snapshots without BLE subscriptions, unchanged-target motor writes,
conflicting motion rejection, instruction-budget trap, invalid module rejection,
stop/cleanup and a running RAM copy surviving deletion of its source file.
Cold TOF/IMU initialization needs several seconds before fresh data is available.
Real radio loss stopped an uploaded counter with 710 ms cleanup latency under
higher-priority work. These are measured examples, not hard deadlines.

The largest tested uploaded fixture used 6312/12288 pool bytes. After teardown,
24 bytes remain for runtime registration and are released by runtime destroy.
VM native stack untouched margin was 2952/4096 bytes. Final RAM snapshot:
FreeRTOS free/minimum 3376/32000 bytes; libc capacity 5880, committed 304,
uncommitted 5576; MSP reservation 4096. GNU size: text 400000, data 1932,
BSS 123224 bytes. No additional static task/pool budget was required.

Readback verified all 401932 programmed HEX bytes, the unchanged bootloader,
and SHA-256 of all original live files (6, 7, 18). All test files were deleted.
Final BIN SHA-256 is
`79ee08729b95db32e4f6f6600a0870fa0a968a70990d483ef71c6415b5dca4eb`.
The board was reset after SWD inspection and left with the VM idle. The full
React view was not built/tested under the Web repository's code-only rule;
browser compiler tests ran the actual Worker from a static server, and BLE
upload/execution tests used the Python hardware harness.

Once connected, run the additional physical test:

```powershell
.venv-ota/Scripts/python.exe tools/check_wasm_upload_hardware.py --run --winrt-shared --modules artifacts/wasm_upload --output artifacts/wasm_upload/hardware.json
.venv-ota/Scripts/python.exe tools/check_wasm_disconnect_hardware.py --run --module artifacts/wasm_upload/counter.wasm --output artifacts/wasm_upload/disconnect.json
```

It hashes existing files, uploads temporary browser fixtures, checks repeated
execution, local sensors, current-target servo API, rejection, traps, cleanup,
and the RAM copy after file deletion. It deletes only its test IDs and verifies
original hashes. `--winrt-shared` requests explicit shared GATT access when
Windows otherwise hides custom services owned by another client. The separate
disconnect test cycles Windows Bluetooth, interrupting all local Bluetooth links.
