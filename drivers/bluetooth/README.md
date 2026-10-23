# Optional BLE HID host

The Tab5 backend uses the ESP32-C6 BLE controller through ESP-Hosted HCI over the
existing SDIO transport. ESP32-C6 does not support Classic Bluetooth HID.
The default firmware build leaves this backend disabled.

## Implementation

- Apache NimBLE 1.8.0 supplies the real GAP, ATT/GATT, L2CAP and SMP host stack.
  Secure Connections is required; Legacy pairing and debug keys are disabled.
- MeOS NPL replaces OS integration: event queues, timers, recursive owned mutexes,
  semaphores, clock and critical sections. No FreeRTOS APIs are used.
- The host runs in a separate MeOS task with a 12 KiB stack. Existing WiFi/SDIO
  service receives HCI traffic independently, including synchronous command ACKs.
- Explicit passive scan and peer selection; no advertisement triggers a connection.
- Pairing requires the selected policy. MITM authentication is required by default;
  Just Works is allowed only by an explicit connection flag. All input requires
  encryption and a full 128-bit key. Numeric comparison and passkey entry/display
  require explicit replies, with no automatic confirmation. Display passkeys use
  controller LE Rand with bounded rejection sampling.
- HID service, boot characteristics and CCC discovery; Protocol Mode is set to
  boot mode and notifications are enabled with peer-confirmed CCC writes.
- Boot Keyboard and three-byte Boot Mouse notifications are accepted only from
  the selected, encrypted connection and discovered/subscribed value handles.
  Disconnect, failed subscription or lost security releases held keys/buttons.
- Bond records use upstream RAM storage and can be cleared. They do not persist
  across a P4 reboot. No pairing keys, HCI payloads or input reports are logged.
- Source-local allocation wrappers use MeOS heaps, track realloc sizes and wipe
  allocations before freeing; IDF's allocator is not used by the host stack.

Report-only/NKRO HID devices, wheel/extra-button mouse reports, multiple simultaneous
input peers and persistent bonds are not implemented. These require HID Report Map
support and additional lifecycle/storage work. Host-level tests do not establish
that pairing or input works on physical Bluetooth devices.

## Build and integration

Include `cmake/nimble_meos.cmake` after the main component is registered and call
`m5os_add_nimble(${COMPONENT_LIB})`. It adds sources only with
`-DM5OS_BLUETOOTH=ON`, on the Tab5 board. All upstream sources remain unmodified.

The public optional-backend API is `host.h`. `bt_host_enable()` claims the generic
Hosted HCI transport and creates the host task once. Subsequent enable after a
transport loss reuses the task and requests stack resynchronization. The parent
shell can expose scan, discovered devices, selected-address connect, status,
pairing reply, disconnect and volatile-bond deletion through the typed APIs.
Address bytes use controller/HCI little-endian order; public/random address type
must be retained from discovery. Pairing numbers are UI output, not general logs.

The generic `m5os/hosted_hci.h` transport owns bus serialization. MCU firmware
96bea3a uses asymmetric SDIO HCI framing: host-to-C6 puts the H4 type in envelope
byte 11 and excludes it from the payload; C6-to-host leaves byte 11 zero and
copies the complete H4 frame into the payload. RX must extract `payload[0]` and
pass `payload + 1` with length minus one to the backend. Treating header byte 11
as the RX type drops command-complete events and causes HCI initialization timeout.
Checksums cover the complete envelope and transmitted payload. RX/lost callbacks
run under the WiFi owner lock: they copy/enqueue and
never transmit, block or manipulate transport ownership. NimBLE TX runs from its
own task and synchronously copies through the transport API.

NPL callout objects must have host-lifetime storage. The 32-slot callout limit
fails closed; blocking calls reject interrupt-masked context. This port assumes
the existing single-core MeOS scheduler. Stop the host cleanly rather than killing
its task while it owns a mutex. Startup/task allocation failure requires a reboot
before attempting to construct the host again.

`hci_probe.c` remains a separate small controller/scan diagnostic. It and NimBLE
must never own the same controller simultaneously. `hid_boot.c` and
`hogp_session.c` implement the reusable authorization and report-decoding layer.

## Source and validation

`third_party/nimble/UPSTREAM.json` records the Apache `nimble_1_8_0_tag` archive hash
and every selected upstream file hash. Apache LICENSE/NOTICE and TinyCrypt's own
LICENSE/AUTHORS are retained. Source archive SHA256:
`e12534499f30739a582020f17bd594186e7dc6b7f2d9a03d6b37c34c907590ad`.

- `run_bluetooth_test.py`: HCI framing/credits/errors, bounded scanning, malformed
  advertisements, boot-report decoding, authorization and stale-write tokens.
- `run_npl_test.py`: real upstream NPL headers, event ownership, callout cancellation
  and reinitialization, tick wraparound, capacity failure, recursive mutexes,
  semaphores and rejection of unauthorized release/interrupt-context blocking.
- `run_host_app_test.py`: actual application callbacks with mocked NimBLE procedures;
  explicit peer selection, no automatic pairing consent, authentication/key-size
  failure, HID/CCC discovery sequencing, input cutoff and transport-loss recovery.
- `compile_nimble_host.py`: RISC-V compilation of the real host/SMP/TinyCrypt and
  adapters, followed by whole-archive relocatable linking. It rejects unresolved
  NimBLE/NPL symbols and direct references to libc allocation functions.

Physical pairing, HID reconnect and keyboard/mouse input still need hardware
validation. The backend remains experimental until those tests pass.

### Hardware validation, 2026-10-23

With the official C6 MCU firmware, `bt on` reached `state=ready error=0` and
`bt scan 3000` discovered two nearby BLE devices. Neither advertised the HID
service (`HID=0`), so neither was selected or paired. The transport counters were
20 transmitted and 22 received HCI packets, with zero TX failures and zero RX
rejections. This verifies controller initialization and BLE discovery only.
After Bluetooth startup, an HTTP request over WiFi also returned HTTP 200 with
577 response-body bytes. This confirms basic WiFi traffic remained usable in
that run; simultaneous sustained HID/WiFi load was not tested.

The reported memory snapshot had approximately 70K free internal RAM, a 66K
largest internal block, 19640K free PSRAM and zero allocator failures. These are
measurements of that firmware/run, not a reserved-memory guarantee. Paired HID
keyboard/mouse input, pairing UI and reconnect remain unverified on hardware.

## Shell and input adapter

`user/bluetooth/bt_app.c` exposes `bt_app_command(argc, argv, output)` for the shell.
It uses the caller's output stream, including SSH/redirection; it does not print
asynchronous pairing prompts or passkeys to a global log. `bt status` explicitly
shows a pending comparison/display number or asks for passkey entry.

Commands: `bt on`, `bt status`, `bt scan [MS]`, `bt devices`,
`bt connect MAC public|random [justworks]`, `bt pair accept [SIX_DIGIT_PASSKEY] challenge=N`,
`bt pair reject`, `bt disconnect`, `bt forget`, and
`bt pointer gui|console|off`. Scan defaults to 10 seconds, at most 60 seconds.
Scan and device listing never select a peer or initiate pairing automatically.
Passkey-input pairing requires six explicit digits; comparison/display confirmation
uses `bt pair accept challenge=N` without supplying a new number. Use the exact
`challenge=N` shown with the pairing prompt. Connection, action, comparison number
and challenge generation are captured when the reply is queued and rechecked
before it can reach SMP. A replacement prompt invalidates earlier consent even
if its displayed number happens to be identical. Public/random address type
comes from discovery. `justworks` is an explicit weaker-authentication policy choice.

Keyboard usage changes maintain modifier/Caps Lock state, then use the same
`hid_decode()` to `ime_feed()` path as USB keyboards. Key repeat and keyboard LED
output are not implemented by this adapter.

Mouse routing defaults to console mode. GUI mode is explicit because the current
GUI API cannot prove that its window is the visible display owner while another
full-screen application runs. GUI mode clamps relative motion to logical screen
coordinates and maps left-button press/drag/release to `gui_inject_touch()`.
It does not add a hover-pointer renderer. A button already held when routing or
the front window changes does not generate a new click in the next window.
Console wheel deltas scroll LCD history only while the console is active; native
three-byte Boot Mouse reports have no wheel, so actual wheel input awaits Report
Map support. Right/middle buttons do not become touch clicks.

`run_cli_test.py` tests both enabled and disabled builds, strict MAC/timeout/passkey
parsing, explicit Just Works consent, no automatic connection, real keyboard/IME
decoding, GUI routing/clipping and button transitions, and console-only wheel routing.

The single-peer host config reserves four outgoing and four incoming ACL buffers,
four non-discardable HCI events and four discardable advertising events; the 12
MSYS mbufs remain unchanged. Advertising reports may be dropped under scan load
without consuming the non-discardable command/security event pool. A UUID-specific
bounded hex formatter replaces that upstream module's `sprintf` calls, avoiding
newlib stdio and its FreeRTOS lock path. The upstream source itself is unchanged.

## Primary references

- [Espressif Hosted Bluetooth design, firmware commit 96bea3a](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/docs/bluetooth_design.md)
- [Hosted SDIO HCI framing](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/host/drivers/transport/sdio/sdio_drv.c)
- [C6 SDIO TX copies the H4-prefixed payload unchanged](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/slave/main/sdio_slave_api.c)
- [C6 VHCI receive/send processing](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/slave/main/slave_bt.c)
- [C6 initializes Bluetooth at startup without a host RPC](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/slave/main/app_main.c)
- [Hosted envelope layout](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/common/esp_hosted_header.h)
- [Hosted capability definitions](https://github.com/espressif/esp-hosted-mcu/blob/96bea3a/common/transport/esp_hosted_transport_init.h)
- [Apache NimBLE pinned source](https://github.com/apache/mynewt-nimble/tree/nimble_1_8_0_tag)
