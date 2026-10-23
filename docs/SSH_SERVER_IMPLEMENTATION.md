# SSH server implementation

The initial server attaches one authenticated SSH connection to the existing
MeOS console. Local input and SSH input operate the same shell and share its
working directory and foreground application. It is not an independent PTY.

## Commands

```text
sshd start USER PASSWORD [PORT]
sshd status
sshd fingerprint
sshd storage ro|rw|off
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
- One session channel, either the shared shell or the SFTP subsystem. Shell
  dimensions remain the LCD dimensions. `exec`, forwarding, and independent
  terminal sessions are not implemented and are refused. See the
  [storage helper](STORAGE_HELPER.md) for SFTP scope and policy.
- TCP socket 4 is reserved for the server. The SSH client uses sockets 0–1,
  allowing client and server activity at the same time. Web retains socket 2 and
  Tailscale retains socket 3.
- The server task uses a 16 KiB stack and a session allocation in PSRAM when
  available. The existing TCP socket receive buffer is allocated separately.
- A 4096-byte channel input window bounds pending input. Window credit is returned
  only after the console or SFTP engine consumes input; queued data is retained
  under backpressure. Shell output reads the existing 2 KiB console log, which
  can overwrite older output if a reader falls behind. SFTP uses a separate
  framed byte stream and does not pass file contents through the console log.
- Sessions are limited to one hour; SSH rekeying is not implemented. Incoming
  rekey requests terminate the connection, after which a new session can connect.
- When input ends, queued SFTP requests and replies are drained before the server
  sends exit status, EOF and CLOSE. It waits for the peer's CLOSE, with a bounded
  two-second close/drain deadline, rather than immediately dropping TCP.

The host seed is stored separately from the client's identity at
`/home/ssh_host_ed25519`. Persistence follows the filesystem mounted at `/home`;
a volatile root does not preserve this key across restart. An existing malformed
key is rejected rather than replaced automatically. The first connection should
verify and retain the host key on the development PC.

While the server is running, `sshd fingerprint` prints the SHA-256 digest of its
public SSH key blob as 64 hexadecimal characters. Read it locally or over the
physical serial connection and compare it with the network peer's public key
before sending credentials. This is explicitly hex, unlike OpenSSH's usual
base64 `SHA256:` display. It never prints the private seed or password.

## Entropy

On Tab5, the SSH random generator acquires a one-time 256-bit startup seed from
the ESP32-P4 hardware RNG with the ADC noise source enabled, then uses
HMAC-SHA256 DRBG with state updates after each request. ADC register settings and
analog calibration fields are restored after seed collection. The seed operation
uses ROM register access under MeOS interrupt protection, avoiding the FreeRTOS
locks used by IDF's application `regi2c_ctrl` wrapper. This single-core startup
sequence holds IRQ protection through save, seed acquisition and restore so an
ADC task cannot be scheduled inside it. Shell and MicroPython ADC users now
exist; the register restoration remains required. Failure to acquire the seed
prevents server startup.

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
the outbound command finished. Later Wi-Fi SFTP checks passed 100 KiB SD
upload/download, exclusive-create preservation, deletion, read-only write denial
and reconnection, with the host public key checked against the physical serial
fingerprint before authentication. Linux FUSE mounting is still unverified.
