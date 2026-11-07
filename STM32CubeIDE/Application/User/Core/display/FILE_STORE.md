# Sequential device file store, version 2

Implemented in `image_store.c` / `file_store.inc`, with the original format in
`image_store_legacy.inc`. Keeping the implementation in the existing translation
unit preserves the CubeIDE source registration. `image_service.c` exposes the
same d0/d1/d2/d3 UUIDs. `Display_ShowFile` renders the two supported image formats.
Web peers are `fileStoreClient.ts`, `fileStoreCodec.ts` and `DemoImageMemory.ts`.

## Scope and memory ownership

Only the OTA download interval `[0x08080000, 0x080FA000)` is used. Bootloader,
active firmware, DATA and NVM are untouched. **OTA can erase all these files.**
Storage never automatically erases unknown or interrupted firmware content.

| Address interval | Purpose |
| --- | --- |
| `08080000..08082000` | Catalog A, 8 KiB |
| `08082000..08084000` | Catalog B, 8 KiB |
| `08084000..08086000` | Recovery scratch page, 8 KiB |
| `08086000..080FA000` | File extents, 464 KiB |

There are at most 64 extent records, independent of the byte capacity. Extents
are contiguous and 16-byte aligned. Interior deleted/reserved space retains its
capacity; it is not split, merged or compacted. New files choose the smallest
deleted extent that fits, otherwise append. Explicit reserved extents can also
be filled. Reserved extents are excluded from automatic allocation.

A live/corrupt middle record cannot be overwritten: delete it explicitly first.
Its replacement must fit the retained capacity. The last record can grow/shrink
within the end of the store. Deleting the last record trims consecutive deleted
tail records. This is logical reclamation; the next writer prepares Flash before
using those addresses. Deletion does not securely erase old content.

Each new file receives a monotonically increasing uint32 ID (initially 6, to
distinguish migrated files from legacy slots); reuse of a deleted/reserved extent
gets a new ID. Replacement of a live last file retains its ID and increments its
revision. Format retains the next ID of a mounted v2 catalog. Names use at most
63 UTF-8 bytes, without ASCII control characters. Types: 1 RGB565, 2 RGB332;
type 3 stores executable timelines with their editable source document. Unused entries and deleted/reserved extents
are allocation states, not content types. Unknown future types remain listable
and byte-readable if their record format is compatible; image actions reject them.

## Images

Type 1 stores RGB565 high-byte first, type 2 stores `RRRGGGBB`. Layout 1 is the
existing round 240x240 aperture, concatenating `ImageStore_RowLeft` row spans;
payloads are 91,384 and 45,692 bytes respectively. Layout 0 is an uncompressed
row-major rectangle, width/height 1..240, with no row padding. Length must equal
pixel count times bytes per pixel. File format version is 1. CRC excludes the
final FF padding to the 16-byte programming boundary.

Rectangle playback centres the image, clearing the rest of the display white.
RGB332 is expanded into RGB565 one row at a time using a static 480-byte buffer
under the display mutex. No framebuffer or heap allocation is added.

With no reserved extents: five full round RGB565 images, or ten RGB332 images,
leave 18,176 bytes. Smaller rectangular images allow more files, up to the
64-record limit. Eight RGB332 images leave 109,568 bytes.

## Disk catalog and transaction ordering

All integers are little endian. A catalog is 6,672 bytes:

- Header: 512 bytes. First words: magic `0x32534653`, version 2, generation,
  extent count. Bytes 16..31 are an initially erased OTA invalidation quadword.
- At byte 32: `next_id`, absolute tail address, pending journal page address,
  scratch CRC, blank-region start/end within that page, formatting flag,
  reserved word. The remaining header padding is erased, including `+0x160`,
  which must never resemble the firmware boot tag pointer.
- At byte 512: 64 fixed 96-byte records (unused entries are zero).
- At byte 6656: commit quadword: `0x324D4F43`, generation, CRC of the preceding
  6656 bytes, and the complement of that CRC. It is programmed LAST.

Each 96-byte record contains, in order: six uint32 fields (`id`, absolute
`offset`, `length`, `capacity`, payload `crc`, `revision`); uint16 width/height;
u8 type/layout/state/version; and a zero-terminated 64-byte UTF-8 name.
States: 1 live, 2 writing, 3 deleted, 4 reserved, 5 corrupt. No live file is
published until all bytes and the CRC match. CRC is CRC-32/ISO-HDLC, reflected
polynomial `0xEDB88320`, initial/final xor `0xFFFFFFFF`.

Catalog updates erase/write the inactive copy, verify it, then select it.
Generation chooses the newest complete valid copy on boot. Extent bounds,
contiguity, IDs, state, size and catalog CRC are validated before mounting.
All-FF quadwords are skipped when writing metadata/page backups so their ECC
programming unit is not consumed unnecessarily.

BEGIN first commits a writing reservation, invalidating any replaced file. A
shared erase page is backed up to scratch and a journal intent (page, CRC and
blank range) is committed before erasure. The target page is erased, neighbours
are restored and verified, then the journal is cleared in another catalog
generation. Only after all affected pages are prepared are upload chunks
accepted. Recovery replays this operation idempotently before allowing use.
Pages wholly inside the writing extent can be erased without a backup.

Interrupted uploads become deleted extents at recovery. A replaced file can be
lost on interruption; unrelated files are preserved. A Flash error locks writes
until reboot/recovery instead of continuing with uncertain RAM metadata.
Disconnect cleanup uses the same exclusive mailbox as normal writes and OTA.

FORMAT writes a durable formatting intent before clearing scratch/data; recovery
finishes a committed format intent. If no valid catalog exists and the interval
is not erased or a recognized legacy catalog, it is locked. In particular,
unrecognizable partial initial formatting is not silently reclaimed as files.

OTA acquires exclusive ownership and invalidates **both** complete catalog
copies before any download erasure. Thus erasing the first page cannot resurrect
the older catalog from the second page. An OTA claim remains exclusive for the
rest of that boot. Preserve the bootloader candidate checks and erased tag slot.

## BLE protocol

Service/characteristics retain UUID suffix `b950-4226-aa2b-4ede9fa42f59` and
prefixes `91bad4d0` (service), d1 (command), d2 (data), d3 (status).
Command maximum is now 20 bytes; data 228 and status 240 are unchanged.
All writes use responses. Acceptance means queued, not durably completed.
Poll the status for matching version/opcode/nonzero sequence and DONE/ERROR.
Only one command or upload lease is active. BLE callbacks copy bounded requests;
the display worker uses Flash Manager and retains its buffer until completion.

V2 command prefix: `op:u8, type:u8, sequence:u16, file_id:u32` (8 bytes).
Integers are LE; type is zero except BEGIN/PLAN. ID zero means automatic placement.

| Opcode | Operation | Bytes after prefix |
| --- | --- | --- |
| 20 | BEGIN | length:u32, crc:u32, width:u8, height:u8, layout:u8, format_version:u8 |
| 21 | COMMIT | none |
| 22 | SHOW | none |
| 23 | DELETE | none |
| 24 | READ | byte offset:u32 |
| 25 | LIST | extent index:u32, starting at 0 |
| 26 | INFO | none |
| 27 | ABORT | none |
| 28 | DATA status only | written through data characteristic |
| 29 | PLAN (read-only) | same as BEGIN |
| 2A | FORMAT | confirmation key `0x46534F4B`:u32 |
| 2B | NAME | sequential name byte offset:u32, 1..8 UTF-8 bytes, during upload |
| 2C | RESERVE | requested capacity:u32 |

Opcodes in the table are hexadecimal. BEGIN and PLAN return a descriptor with
the assigned/proposed ID. PLAN does not reserve anything; BEGIN revalidates the
placement. COMMIT/NAME must name the active upload. An old middle ID cannot
accidentally refer to a new file that reused its extent.

DATA: `sequence:u16, offset_div_16:u16, payload:1..224 bytes`. Offset must equal
the acknowledged upload progress. Chunks are multiples of 16 except the last.
The client falls back to 16-byte payloads on an explicit ATT size rejection.
Never blindly resend after an uncertain write; abort/reconnect and restart.

V2 status header (32 bytes):

| Offset | Field |
| --- | --- |
| 0 | version:u8 = 2 |
| 1 | state:u8: 1 BUSY, 2 DONE, 3 ERROR |
| 2, 3 | opcode:u8, error:u8 |
| 4 | sequence:u16 |
| 6 | mode:u8: 0 erased/unformatted, 1 v2 mounted, 2 legacy, 3 locked |
| 7 | extent count:u8 |
| 8 | file ID:u32 |
| 12 | progress/read offset/next list index:u32 |
| 16 | catalog generation:u32 |
| 20 | payload CRC:u32 when relevant |
| 24, 28 | free tail bytes:u32, deleted-extent bytes:u32 |

INFO adds four u32 values: data capacity, maximum records, type mask (14), layout
mask (3). LIST adds one 96-byte record, or no payload and next index FFFFFFFF at
end. BEGIN/PLAN/RESERVE add a descriptor. READ adds up to 208 payload bytes.
Downloads check generation, offsets, length and CRC before applying editor pixels.

Errors: 80 invalid; 81 busy; 82 Flash/SPI failure; 83 disabled; 84 OTA/unknown/
fault lock; 85 upload offset/state; 86 incomplete/CRC; 87 missing; 88 disconnect;
89 unsupported format/not initialized; 8A no fitting extent or tail; 8B middle
replacement requires explicit deletion/rebuild; 8C record/ID limit. Immediate
rejection returns the corresponding ATT error. A completed write followed by a
lost response may still have committed: reconnect and refresh before retrying.

## Migration and Web behaviour

New Web probes INFO. Firmware that rejects the probe and reports v1 status uses
the old five-slot client path. New firmware with a recognized legacy catalog
reports mode 2 and still accepts the old v1 commands. New-format storage rejects
v1 commands with 89; old Web clients cannot overwrite new files.

In Display: download an image backup, explicitly create the new file store,
then restore the backup. Backup JSON is `servo-device-image-backup`, version 1,
with entries of `{metadata, pixels}`. Restore adds new files sequentially, never
silently formats or replaces existing files. An interrupted restore can leave a
successfully restored prefix; refresh the file list before repeating it.

The UI shows allocation capacity separately from payload length, tail space
separately from holes, image dimensions, encoding, state and file ID. Unknown
types cannot be edited as images. File metadata is persisted with the project;
the 240x240 editor uses centred rectangular bounds for smaller files. Code uses
`showStoredImage(fileId)`; legacy IDs remain 1..5 on legacy storage. Motor timelines now share the catalog and use device-side playback; see the
timeline format and command sections below.

## Verification

- `tools/check_file_store.py --cc <arm-none-eabi-gcc>`: real C under ARM emulation,
  strict Flash/ECC-unit writes, capacity, 64 records, allocation, UTF-8 names,
  ownership, disconnect, CRC, reboot, OTA and 27 power-cut checkpoints around
  both shared boundaries of a multi-page extent. `--output` writes a JSON result.
- `tools/check_image_store.py`: legacy format regression.
- `tools/check_display_animation.py`: real RGB332 conversion for all 256 colours,
  RGB565 byte order, rectangle bounds/placement and existing display/TOF tests.
- `tools/check_boot_image_guard.py` and `tools/validate_firmware.py`: boot/OTA guards
  and linked image limits.
- Web: `node --test tests/fileStore.test.mjs` plus TypeScript `--noEmit` checking.

These tests do not establish physical brownout/ECC behaviour, RF timing or panel
output. Physical controller flashing and destructive on-device formatting are
not part of these software tests.


## Timeline files (type 3)

Type 3 uses version 1, width/height/layout zero. Storage management, exact byte
length, CRC32, name, allocation, DELETE/FORMAT and READ use the same v2 protocol
as images. INFO type mask is 14 (bits 1, 2, 3). SHOW rejects timelines.

The little-endian payload is a 32-byte header, zero to 4096 16-byte events, then
UTF-8 editor JSON. Header: magic `TML1` at 0, u16 version 1 at 4, u16 header size
32 at 6; u32 event count at 8, duration milliseconds at 12, repeat boundary at
16 (0 disables), JSON byte count at 20, zero flags at 24 and zero reserved at 28.
Each event: u32 dispatch milliseconds at 0, u32 duration milliseconds at 4,
u16 pulse microseconds at 8, u8 channel at 10, u8 kind at 11 (0 direct, 1 linear),
u32 zero reserved at 12. Pulse range 500..2500, channels 0..15. Direct duration
must be zero; linear duration must be positive. All event endpoints fit within
the declared duration. Durations support the full unsigned 32-bit millisecond
range. 4096 is an explicit implementation event limit, not a fixed file size.

Events sort by dispatch, then kind (direct first), then channel. Duplicate
same-kind/channel/dispatch is invalid. Same-channel linear intervals cannot
overlap; direct events cannot lie strictly inside a linear interval. A direct
and a linear at the same start are legal. Empty and guide-only documents are
valid. Repeat clips at its boundary. Events exactly at the boundary execute before
time-zero events restart; later events are not executed. Repeat may be after the
last event/guide, holding until that boundary, including in empty documents. JSON preserves editable commands, guides, repeat, identity and name;
firmware checks UTF-8 but treats JSON as opaque. The editor validates its schema
and its correspondence to the binary plan. Firmware executes the binary plan.

Both COMMIT and PLAY validate the plan. PLAY additionally verifies full payload
CRC and acquires an exclusive flash lease. All storage requests (including
read/list), recovery and OTA are blocked while the lease is held. STOP,
disconnect, natural completion or execution fault releases it. Stop playback
before managing files. No flash I/O or allocation occurs in the motion loop.
See `servo/timeline_player.inc` and root `SERVO_SCHEDULING.md` for timing limits.
