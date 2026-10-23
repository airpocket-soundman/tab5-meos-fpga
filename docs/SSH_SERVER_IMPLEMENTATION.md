# SSH server implementation

The initial server attaches one authenticated SSH connection to the existing
MeOS console. Local input and SSH input operate the same shell and share its
working directory and foreground application. It is not an independent PTY.

## Commands

```text
sshd start USER PASSWORD [PORT]
sshd status
sshd stop
```

The server is stopped at boot. Start requires a username of 1–32 bytes and a
password of 8–64 bytes; the default port is 22. The password is retained only as
a SHA-256 digest in RAM and cleared when the server task exits. The console line
used to start it can be visible locally and in shell history; use a dedicated
development password. Stop closes the listening/connected socket. Start again
after the task has completed stopping.

## Transport and resources

- SSH-2 with `curve25519-sha256` and the `@libssh.org` alias.
- `ssh-ed25519` host key, `aes128-ctr`, `hmac-sha2-256`, no compression.
- Password authentication, up to eight authentication requests per connection.
- One session channel; `pty-req`, `shell`, and `window-change` accepted. Console
  dimensions remain the LCD dimensions. `exec`, SFTP, forwarding, and independent
  terminal sessions are not implemented and are refused.
- TCP socket 4 is reserved for the server. The SSH client uses sockets 0–1,
  allowing client and server activity at the same time. Web retains socket 2 and
  Tailscale retains socket 3.
- The server task uses a 16 KiB stack and a session allocation in PSRAM when
  available. The existing TCP socket receive buffer is allocated separately.
- A 255-byte channel input window bounds pending input. Window credit is returned
  only after bytes enter the console input queue; data is retained when that queue
  is full. Output reads the existing 2 KiB console log. If a reader falls behind,
  the console log can overwrite older output. This initial version is unsuitable
  for lossless bulk console capture or large file transfer.
- Sessions are limited to one hour; SSH rekeying is not implemented. Incoming
  rekey requests terminate the connection, after which a new session can connect.

The host seed is stored separately from the client's identity at
`/home/ssh_host_ed25519`. Persistence follows the filesystem mounted at `/home`;
a volatile root does not preserve this key across restart. An existing malformed
key is rejected rather than replaced automatically. The first connection should
verify and retain the host key on the development PC.

## Entropy

On Tab5, the SSH random generator acquires a one-time 256-bit startup seed from
the ESP32-P4 hardware RNG with the ADC noise source enabled, then uses
HMAC-SHA256 DRBG with state updates after each request. ADC register settings and
analog calibration fields are restored after seed collection. The seed operation
uses ROM register access under MeOS interrupt protection, avoiding the FreeRTOS
locks used by IDF's application `regi2c_ctrl` wrapper. No other ADC user may run
concurrently with this initialization. The current Tab5 firmware has no ADC API
consumer. Failure to acquire the seed prevents server startup.

The register sequence in `ssh_entropy_tab5.inc` is adapted from ESP-IDF 5.4.4
`components/bootloader_support/src/bootloader_random_esp32p4.c` under Apache-2.0.

## Verification

The Windows test in `tests/ssh` exercises the production cryptographic and SSH
server code against Paramiko. Host tests cover positive authentication, host
signature validation, channel flow control, incorrect passwords, unsupported exec,
and malformed packets. Hardware verification must separately establish Ethernet
or Wi-Fi connectivity, ADC initialization, reconnect, stop/restart, console
commands, and simultaneous outbound SSH. Do not infer those results from the
host interoperability tests.

Hardware verification on 2026-10-23 passed Ethernet login, shared-shell echo commands,
disconnect/reconnect, stop/restart, and an outbound SSH command to a test server
while the inbound session stayed open. The inbound shell remained usable after
the outbound command finished. Wi-Fi transport remains under separate investigation.
