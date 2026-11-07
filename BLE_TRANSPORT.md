# BLE transmit ownership

The BLE TX owner is `STM32_WPAN/App/ble_tx.c`. Producers do not send notifications.
`bleTxTask` (High2, static 4096-byte stack) is separate from command parsing. The
serialized HCI dispatcher (High3) has a static 2048-byte stack. `defaultTask` has
6144 bytes after reclaiming its previously unused stack allocation. Measure
runtime high-water marks; successful linking alone does not establish stack safety.

## Queue and scheduling contract

- RX and ordered replies each use five statically allocated inline 255-byte items.
  No transport item allocates or frees heap memory. RX validates length before
  narrowing it and copies with zero queue wait in the BLE callback.
- RX reserves one reply slot before returning ATT success. A full RX queue or
  unavailable reply capacity returns ATT Insufficient Resources (0x11). The credit
  transfers to the TX queue on enqueue. Stale RX items release their credit without
  executing their command. The TX owner never enqueues into its own queue.
- Ordered command/OTA replies have priority over telemetry. Multiline NUS replies
  retain their existing one-line-per-notification framing and END markers.
- Servo, IMU, TOF and ADC use one latest-value slot each, never a growing history.
  Maximum aggregate send rates are 20, 20, 10 and 5 Hz respectively. Round-robin
  service and link selection prevents one subscriber from monopolizing telemetry.
- The eight configured links are retained. Items carry connection, session and
  CCCD generation. Sends target the intended link, never a broadcast. CCCD 0x03
  prefers notification. 0x01/0x02 changes atomically replace the previous mode.
- Each link starts at ATT MTU23 and tracks the ST MTU response event. Frames that
  cannot fit MTU-3 remain latest-only until MTU permits them. They are never split
  into incompatible sensor packets. A too-long text response becomes
  `- MTU TOO SMALL`; HELP and diagnostics use lines that fit MTU23.

## State machine and ST semantics

Only one application notification/indication is outstanding globally. The owner
arms its expected attribute and mode before calling the stack, including when a
completion callback arrives before the call returns. A successful API return is
stack acceptance, not completion and not proof of remote application receipt.

`BLE_STATUS_INSUFFICIENT_RESOURCES` (0x64) waits for
`ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE`, with a sequence counter to prevent a lost
wake. Its Connection_Handle is explicitly unused by ST and is not filtered.
There is no sleep-and-retry loop. Successful notification acceptance waits for
`ACI_GATT_NOTIFICATION_COMPLETE_VSEVT_CODE`. That event carries only the
characteristic VALUE handle, not a connection or success status. Indications wait
for `ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE` matching the connection. OTA consumes
only confirmation of its own active indication, not a NUS indication confirmation.
All notifying characteristics enable the notification-completion event mask.

A two-second transport timeout fails the link rather than retransmitting an
accepted packet or completing a partial reply. Failed disconnect requests are
retried on a one-second recovery deadline; data is never retried in that state.
A late completion while draining discards the remaining reply. The dispatcher
clears orphaned work after matching disconnect and drain of its asynchronous event
queue, before starting any new accepted transmission.

ST's public headers do not supply a transaction/session ID for notification
completion. The reconnect drain barrier relies on stack bearer teardown plus FIFO
HCI dispatch: completions already queued before teardown are consumed before
reusing the owner. A fabricated completion generated after completed teardown and
queue drain could not be distinguished from a new same-attribute completion.
This is an explicit platform assumption, not a claim that queue-empty alone proves
arbitrary future callbacks impossible. Host tests exercise queued late events;
real reconnect/soak tests validate the bundled ST binary under load.

## Bounded NUS/servo ATT permission responses

Deferred permission recovery covers NUS TX reads, NUS RX command writes, and the
NUS direct-servo characteristic's reads/writes only. Other services' ATT handlers,
including image_service.c allow_read/write_resp paths, are unchanged and do not
use this recovery. Their permission-failure behavior was not tested by the motor
workload or these fault-injection cases. Central notification ownership does not
imply that every service's ATT permission response is covered.

NUS reserves one static response slot per connected unenhanced ATT bearer before
admitting RX or applying a direct-servo write. The reservation copies write bytes
and carries connection session plus request serial. A collision is rejected before
side effects; the affected session is closed rather than overwriting its response.
The HCI callback commits only a permission decision. The owner submits it before
new notifications, including while notification transmission waits for pool space.
An already accepted notification keeps its completion identity and is never replayed.

Only explicit BLE_STATUS_INSUFFICIENT_RESOURCES (0x64), which means the controller
has not accepted the permission command, is retried. Pool/completion events wake
it immediately. ST guarantees the pool event for notification-update APIs, not
for permit_read/permit_write, so permission responses alone also use a 50ms probe.
The absolute two-second permission deadline is not extended by attempts. Success
clears the slot. Other/ambiguous statuses or expiry close only the matching session;
failed termination is retried at one-second intervals without replaying data.
Disconnect discards the saved decision, and a reused handle receives a new session.
Control selection is round-robin across all eight links. If the next sequential
peer request arrives before the prior controller call returns, the prior response
bytes already live in the owner stack copy. The new request uses a new serial;
a successful old return cannot erase it, and a contradictory failure closes the
session without replay. Completion bookkeeping runs before control selection.

Retries send only the copied ATT decision, never enqueue/parse a command again or
apply a servo write again. An admitted command may already have executed before
an ACK fails; reconnecting clients must not assume a lost ACK means no side effect.
This closes a definite forgotten-response error path. It is not proof that an ATT
permission failure caused the earlier hardware timeout; the diagnostic soak after
removing per-notification dispatcher logging passed without exercising that error.

## Reads and diagnostics

Servo READ constructs a fresh 16xuint16 little-endian snapshot and uses
GATT_CHAR_UPDATE_LOCAL_ONLY before permit_read. It never requests notification.
NUS TX READ now schedules its required permission response. ADC/IMU local reset
snapshots also use local-only updates. NUS/servo READs and their ATT replies do not
enter the notification FIFO. The controller can still reject a permission command
for temporary resource pressure; the scoped recovery above handles that failure.

`BLE_STATS` is read-only, returns lines from `OK BLE_STATS` through
`OK BLE_STATS END`, and does not reset counters. Fields: s (state), rsv (pending
RX reply reservations), rx/tx (queue depth/high-water), rxrej/txrej, sent (accepted),
done (ST completion), pool, timeout, err (transport/local/ATT errors), stale,
merge (coalesced frames), heap/min (FreeRTOS bytes), stack/hci (unused stack bytes),
lag (maximum observed completion latency milliseconds, saturated at9999), cp
(pending/closing NUS/servo ATT responses), cr (their retry attempts), and cf
(their fatal sessions).
Displayed monotonic counts saturate at9999999 (meaning at least that many); internal
counters remain uint32. This preserves the complete <=254-byte reply including END
and <=20-byte lines for MTU23. No diagnostic command resets counters.
The diagnostic command itself normally owns one reservation while its snapshot is
captured. Counters survive reconnection and reset only with firmware restart.

`tools/check_ble_transport.py` compiles the real owner with strict host mocks.
`tools/check_ble_transport_hardware.py` checks actual callback receipt, non-notifying
reads, traffic load, subscription switching, reconnect and heap/stack behavior.
Neither host tests nor ST completion alone prove remote receipt or physical motion.
