# Local BLE OTA

Run **`build_ota.bat`** at the repository root. It builds the bootloader Release
and application Debug configurations, checks the linked memory layout, and prints
the absolute path and `file:///` URL of the upload file. The file is named
`artifacts/ota/NatiLab-<version>-<CRC32>.ota.bin`. `latest.json` identifies the
latest successful package. A failed build returns a nonzero exit code and never
packages an old application. Logs are beside the package. Set `OTA_NO_PAUSE=1`
when invoking the BAT from automation. `-CubeIdeRoot <path>` overrides the default
STM32CubeIDE 2.2.0 installation. Python 3 is required for packaging.

Change the version in `Core/Inc/firmware_version.h` before building a release.
Version and image CRC are separate: changing code without changing the version
still produces a distinguishable image. This workflow makes no Git changes to
history, commits, pushes, or deployments.

## First installation

The previous ST example protocol cannot safely bootstrap these changes over BLE.
Install **both** new images once using ST-LINK:

```powershell
./build_ota.bat
powershell -ExecutionPolicy Bypass -File tools/flash.ps1
```

The flash script writes the bootloader at `0x08000000` and the address-bearing
application HEX at `0x08006000`. It verifies both, resets the MCU, preserves the
download/file area, and does not mass-erase or change option bytes. Do not select
the bootloader BIN, raw application BIN, ELF, or HEX in the OTA UI.

## Website

In the sibling Web project, open **Tools → Firmware update** (Hebrew:
**כלים → עדכון קושחה**). Connect to the controller, select the `.ota.bin`, review
its version/size/CRC, acknowledge the file-storage erasure, and upload.
Progress advances only after the MCU reports durable Flash completion. Cancel is
available before the installation request. After reboot, reconnect to the same
controller and select **Verify installed firmware**. Only a matching VERSION,
size and CRC is reported as success. BLE disconnection alone is not success.

Use Chrome/Edge with Web Bluetooth on HTTPS or localhost. OTA discovery is
optional, so older firmware and demo mode can still use the rest of the app.
Demo mode cannot install firmware. The shared bridge serializes ATT operations
and blocks other writes during an upload. Stored playback is stopped first.

## Python CLI

The packaging/check-only path uses the Python standard library. Hardware access
requires Bleak; this workstation already has it under `py -3.13`.

```powershell
py -3.13 tools/ota_ble.py --scan
py -3.13 tools/ota_ble.py --version --name NL802915
py -3.13 tools/ota_ble.py --probe --name NL802915
py -3.13 tools/ota_ble.py --check-file "C:\full\path\firmware.ota.bin"
py -3.13 tools/ota_ble.py --file "C:\full\path\firmware.ota.bin" --name NL802915 --erase-files
```

`--address` is an alternative to `--name`. Automatic selection works only when
exactly one NUS controller is discovered. `--chunk 16 --stop-after 512` exercises
a small-MTU upload and cancellation without installation. It **still erases
stored files**, and therefore also requires `--erase-files`. The CLI reconnects
after installation and compares the running image with the package. It fails if
that comparison cannot be completed. Close other BLE clients before a test.
The upload client sends STOP first to release stored playback. `--probe` only
exercises rejected requests and confirms unchanged status and VERSION; it never
submits a valid BEGIN or intentionally erases storage.

## Shared file storage and backup

All device images and timelines occupy the same physical area as the candidate
firmware. OTA therefore removes them, even if the update is cancelled. Export
them through the existing device-file UI before starting, and restore them after
the update. An aborted upload leaves storage locked until reset and reformat.
Normal application startup never erases this area.

During this task a complete 1 MiB pre-test Flash snapshot was saved locally as
`artifacts/hardware/before-ota-full-flash.bin`; its SHA-256 is
`650123d7f42468e3bc14ffedf7265630018cd8d38ba748781bd0491179ac9795`.
It contains two catalog records. Treat raw Flash restoration as a controlled
recovery operation: only restore the exact download-area slice to `0x08080000`
when restoring files, preserving the new bootloader/application and NVM.

## Research and implementation choices

The board is STM32WBA5MMG (1 MiB Flash, 128 KiB RAM). This is the **WBA** update
architecture, not the STM32WB dual-core FUS workflow. ST's WBA sample separates
the application install manager, active slot and download slot. Its tail keyword
checks transfer completion, but does not check the complete image contents.
This implementation keeps that memory layout and tail contract, adds whole-image
CRC32 and an explicit persistent installation record, and disables legacy
unverified upload commands. [ST WBA FUOTA](https://wiki.st.com/stm32mcu/wiki/Connectivity:STM32WBA_FUOTA)

Flash operations use the existing Flash Manager from task context so BLE event
processing can continue. Each BLE callback copies at most one bounded request;
the existing default task owns programming. Requests with an active owner or
pending operation cannot overwrite that buffer. An accepted Flash Manager
operation retains its callback and source buffer until completion, including
disconnect. [ST Flash management](https://wiki.st.com/stm32mcu/wiki/Connectivity:STM32CubeWBA_Flash_management)

Browser writes explicitly use `writeValueWithResponse`; the Python client uses
`response=True`. These acknowledge request acceptance, so both clients also poll
the MCU's status for durable completion. Windows reads bypass the GATT value
cache. The Web client falls back to 16 data bytes only after a size rejection and
proof that the rejected packet was not written. [Web Bluetooth specification](https://webbluetoothcg.github.io/web-bluetooth/),
[Bleak client API](https://bleak.readthedocs.io/en/stable/api/client.html),
[Bleak Windows backend](https://bleak.readthedocs.io/en/stable/backends/windows.html)

## Memory layout

| Region | Address range | Purpose |
|---|---|---|
| Bootloader | `0x08000000–0x08005FFF` | 24 KiB, never written over BLE |
| Active slot | `0x08006000–0x0807FFFF` | 488 KiB application region |
| Download / files | `0x08080000–0x080F9FFF` | 488 KiB staging/file region |
| OTA commit record | `0x080F9FF0–0x080F9FFF` | Last 16 bytes of download slot |
| User configuration | `0x080FA000–0x080FBFFF` | Preserved |
| NVM | `0x080FC000–0x080FFFFF` | Preserved |

Maximum padded OTA image: **499696 bytes**, reserving the final quadword for the
commit record. All programming is 16-byte aligned. Firmware vectors must target
the active slot, and offset `0x160` contains the active-address pointer to the
final `0x94448A29` word. Padding after that word must be `0xFF`.

The commit record is four little-endian uint32 values:
`0x3241544F, padded_length, image_crc32, ~image_crc32`. No valid record exists
during downloading or verification. INSTALL writes it only after verification.

## OTA container

All integers are little endian. The 64-byte header is followed by the padded
application bytes. CRC32 is IEEE/ISO-HDLC, matching Python `binascii.crc32`.

| Offset | Size | Field |
|---|---:|---|
| 0 | 8 | ASCII `NSOTA2` followed by two zero bytes |
| 8 | 2 | Container version `2` |
| 10 | 2 | Header size `64` |
| 12 | 4 | Target `0x35414257` (WBA5) |
| 16 | 4 | Link address `0x08006000` |
| 20 | 4 | Padded image length |
| 24 | 4 | Image CRC32 |
| 28 | 24 | ASCII firmware version, zero padded (1–23 characters) |
| 52 | 8 | Reserved, zero |
| 60 | 4 | CRC32 of header bytes 0–59 |

## BLE protocol v2

Service: `0000fe20-cc7a-482a-984a-7f2ed5b3e58f`.
Characteristics keep the ST UUIDs but use project protocol v2:

| Channel | UUID suffix before `-8e22-4541-9d4c-21edae82ed19` | Properties |
|---|---|---|
| Command | `0000fe22` | WRITE with response, 20 bytes max |
| Status | `0000fe23` | READ, exactly 20 bytes |
| Data | `0000fe24` | WRITE with response, 228 bytes max |

BEGIN (20 bytes): `[0x10, 2, 1, 0, length:u32, crc:u32, session:u32, target:u32]`.
Flag `1` explicitly acknowledges shared-file erasure; session must be nonzero.
Other commands (8 bytes): `[opcode, 2, 0, 0, session:u32]`:
VERIFY=`0x11`, INSTALL=`0x12`, ABORT=`0x13`.
Data: `[offset:u32, bytes...]`, 16–224 data bytes, a multiple of 16.
The exact next offset and connection owner must match. Long/prepared writes are
rejected. The server rejects writes without response by characteristic property.

Status: `[2, state:u8, op:u8, error:u8, session:u32, received:u32, length:u32, crc:u32]`.
Data jobs report op `0x14`. States: 0 idle, 1 busy, 2 ready, 3 receiving,
4 verified, 5 installing, 6 error, 7 aborted. Errors: `0x80` invalid,
`0x81` busy, `0x82` Flash I/O, `0x83` state/owner/session, `0x84` offset,
`0x85` CRC, `0x86` image layout, `0x87` disconnected.

BEGIN invalidates both file catalogs, erases the commit page first and then the
remaining download pages. VERIFY checks the stored bytes, vectors, bounds and
tail. INSTALL writes/read-verifies the persistent record and resets after 500 ms.
Cancel/disconnect before INSTALL never commits a candidate. A fresh BEGIN may
restart an aborted/failed transfer; byte-level resume is intentionally absent.

## Boot and failure behavior

On every reset cause the manager checks the persistent download record and CRC.
It erases/copies only a valid committed candidate. It compares the entire active
image before deleting the candidate. If erase/copy is interrupted, the candidate
and its record remain, allowing a fresh full copy on the next reset. Candidate
cleanup erases the commit page first; interrupted cleanup cannot request an
installation from a half-erased image. Flash errors stop bounded operations;
they do not cause the old unbounded program-retry loop.

With no committed candidate, a structurally valid active application starts.
With neither image valid, the board waits for ST-LINK recovery. The manager does
not contain a BLE recovery stack. This is not A/B rollback: a well-formed image
that boots but malfunctions requires a subsequent update or ST-LINK recovery.

CRC checks corruption, **not authenticity**. There is no firmware signature,
anti-rollback counter, or new pairing policy in this local-testing implementation.
Physical brownouts can also cause Flash ECC faults, which are distinct from the
byte-level power-cut simulations; such faults and real supply removal have not
been hardware qualified. [ST ECC testing guidance](https://www.st.com/content/st_com/en/technical-documents/an5342--how-to-use-error-correction-code-ecc-management-for-internal-memories-protection-on-stm32-mcus-stmicroelectronics_asset/chunk-003-topic-an5342-test-ecc-flash.html)

## VERSION and validation

`VERSION` and `HELP OTA` are NUS RX commands. VERSION replies in short NUS TX
packets (newlines are stripped by the existing TX dispatcher): `OK VERSION`,
`VER=<first 16 version characters>`, `VER+=<remaining characters>`,
`IMAGE=<8 hex digits>`, `SIZE=<decimal>`, `OTA=2`, `OK VERSION END`.
Clients normalize those packets into a single comparison string.

Run `python tools/check_ota.py` on Windows with Visual Studio's C compiler for
actual receiver/installer C tests. It models Flash and RTOS calls and exercises
372 half-page/half-quadword interruption boundaries. It never connects to hardware.
In the Web repo run `node --test tests/ota.test.mjs` for format, transfer, CRC,
fallback, cancellation and acknowledgement tests. The real-package interoperability
case requires a local FW build. `node --test tests/*.test.mjs` also checks the
existing file-store, demo and timeline clients.

Hardware test evidence and any remaining limits are recorded under
`artifacts/hardware/` and in the final task report; simulator passes alone do not
establish electrical power-loss reliability.

### Local verification, 2026-11-11

- `build_ota.bat`: bootloader Release and application Debug built successfully;
  linker layout validation passed. Three existing application warnings remain in
  `stm32_lpm_if.c` and `app_ble.c`, outside the OTA changes.
- Upload artifact: `NatiLab-1.1.0-ota.2-F8BB9BAC.ota.bin`, 351952 image bytes,
  CRC32 `F8BB9BAC`; container SHA-256
  `e5e875af717a15e85ce4d1f08bfd4950bc2edca0b4eb9aa85b6679232ebea780`.
- Host receiver/installer tests passed, including all 372 modeled interruption
  boundaries, ownership/offset errors, abort, CRC failure and Flash I/O failure.
- All 48 Web functional tests passed; the eight OTA tests also passed after
  the final package rebuild. The actual Python package was accepted by the Web
  parser. No production Web build, lint, or dependency installation was run.
- Local Vite UI checked in Chrome, including Hebrew RTL at 390x844, synthetic
  package selection and invalid package rejection. No console errors or
  horizontal overflow were observed. Browser upload to physical hardware has
  not yet been exercised.
- Hardware `NL802915` (`00:80:E1:2A:26:24`) was backed up, then the new bootloader
  and initial application `1.1.0-ota.1` were programmed and verified through
  ST-LINK. Real BLE VERSION returned image `41D834AC`, size 351920, OTA protocol 2.
- Real BLE rejected legacy/malformed BEGIN, missing erase consent, wrong target,
  premature data and premature INSTALL, with unchanged status and VERSION.
- The user explicitly authorized erasing all controller Flash and stored files,
  without restoration. The earlier erasure-approval blocker is resolved.
- Full hardware upload and upload-abort tests are still incomplete. The previous
  session stopped at Windows BLE service discovery (`Access Denied`) before a
  valid OTA BEGIN. Its last verified running application was `1.1.0-ota.1`;
  `.2` remains the prepared update. Do not report a successful OTA until the
  browser reconnects and VERSION matches the package version, size and CRC.

### Continuation recovery, 2026-11-11

- The earlier Codex chat was found intact and pinned, titled
  `ממש OTA מלא דרך BLE`, ID `01a1cbd9-499c-7f52-9fdd-ad3bf12f9e90`.
- Firmware and sibling Web changes remain local and uncommitted. Both repositories
  pass `git diff --check`. The `.2` container hash still matches the value above.
- The attached STLINK-V3SET serial is `002100313331510D34333834`, UART `COM4`.
  The last BLE identity was `NL802915`, address `00:80:E1:2A:26:24`.
- No local Vite server was listening on ports 5173-5175 during recovery.
- An automatic approval review rejected the combined local Vite startup and
  ST-LINK reset, citing AGENTS.md execution restrictions. Neither action ran.
  A specific user override for local server, browser tests and ST-LINK tools was
  requested in the continuation chat. This is separate from Flash-erasure consent.
- These were the recovery steps pending at the end of that session. The browser
  upload and installed-image verification were subsequently completed as recorded
  below. Keep logs under `artifacts/hardware/`; do not commit or push.

### Browser OTA completed, 2026-11-12

- The user explicitly approved local Vite startup, browser automation/testing and
  ST-LINK reset/programming as exceptions to AGENTS.md execution restrictions.
  The existing Web app was served at `http://127.0.0.1:5173/`.
- The user connected and uploaded `NatiLab-1.1.0-ota.2-F8BB9BAC.ota.bin` through
  the browser. The assistant observed the live BLE upload advancing through 75%.
- After the upload, the user reported this response from the controller:

  ```text
  OK VERSION 1.1.0-ota.2 IMAGE=F8BB9BAC SIZE=351952 OTA=2
  ```

  Version, image CRC and padded length exactly match the prepared package. This
  completes the browser upload -> bootloader installation -> running-application
  VERSION check, with the final response supplied by the user. The earlier
  Windows/Python discovery failure did not prevent this browser update.
- Fixed the OTA card clipping inherited from `.command-center-card`: the OTA
  view now overrides `max-height: 42vh` and `overflow: hidden`. The CSS update
  loaded while the upload continued. An isolated browser layout check using the
  upload view's copied markup verified CSS viewports 1280x720, 768x600 and 390x640,
  including RTL: controls/progress stay reachable by scrolling, without horizontal
  overflow. The isolated layout fixture did not connect to hardware.
- Remaining hardware coverage: deliberate upload cancellation and physical
  power interruption/recovery have not been completed. A successful full update
  does not establish those failure-path results. No file restoration is planned,
  in accordance with the user's full Flash-erasure authorization.
- This continuation did not rerun Node tests: automatic approval review rejected
  that command as outside its interpretation of the explicit override. Earlier
  successful host/Web test results are recorded above, not new results.


### Global BLE performance and browser validation, 2026-11-12

The user subsequently explicitly authorized build_ota.bat, firmware builds and all
required C/Python/Node tests. The known-working .2 firmware and Web baseline were
committed/pushed before performance changes (FW 0eb4bba, Web 085edfe).

The shared link policy in BLE_TRANSPORT.md now requests 15 ms / zero latency,
251-byte data length and 2M PHY for every connection. Flash Manager/RF arbitration,
one-at-a-time admission, durable status polling, CRC and commit rules are unchanged.

Final package: NatiLab-1.1.0-ota.3-8B6C6DD5.ota.bin, 352816 image bytes (352880
container bytes), SHA256 053c5c15fc22d9a797b9c0d67ae7ad31132cd567c467eed6013af37c7a4b6404.
The actual browser UI uploaded it in 96.15 s through 100%, including preparation,
then verified CRC and rebooted. After reconnect, Verify installed firmware showed:

```text
OK VERSION 1.1.0-ota.3 IMAGE=8B6C6DD5 SIZE=352816 OTA=2
```

The assistant directly observed the page's successful verification. The previous
active image was B19B537E / 352720 bytes, so this was a different-image installation.
An earlier same-image full upload took 95.61 s through 100%; both runs passed.
A separate production-client AbortSignal test cancelled after 32928 durable bytes
and confirmed the active VERSION stayed unchanged. Physical power interruption
remains untested; the 372 power-cut boundaries passed only in host simulation.
See BLE_TRANSPORT.md for timing comparisons, reconnect/PHY recovery and test details.

## User-selected firmware versions

Upgrades, downgrades, reinstalling the same image and custom builds are all
allowed. Version text is an identity label, not an installation permission or
ordering rule. Do not add a minimum-version requirement, monotonic counter or
comparison against the running firmware. Post-reboot verification compares the
running version, size and CRC to the file the user selected, including an older
file; it never requires a version newer than the previous installation.

The selected file must still be a valid OTA v2 .ota.bin for this board/application
layout. Container/protocol versions describe the binary format, not release age.
CRC, target, size, vector/tag, complete transfer and commit checks remain active.
An old raw .bin or incompatible OTA format is not a downgrade-policy rejection.
