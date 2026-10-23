# SSH server interoperability test

Run on Windows with Zig 0.13+ and Python with Paramiko installed:

```powershell
python -m pip install paramiko
python tests/ssh/run_protocol_test.py --cc C:/path/to/zig.exe
```

The runner compiles a 32-bit Windows executable (`x86-windows-gnu`) with a
compile-time assertion that `sizeof(size_t) == 4`, matching the target's overflow
behavior. It extracts the production crypto, packet transport and server from
`user/ssh/ssh_app.c` / `ssh_server.inc`, compiles them against a loopback WinSock
transport and an echo console, and connects with Paramiko. It checks:

- X25519 key exchange, Ed25519 host signature, AES128-CTR / HMAC-SHA256.
- Password authentication and rejection of an incorrect password.
- PTY / shell channel requests and input/output across repeated 255-byte windows.
- Explicit refusal of unsupported `exec` requests.
- Rejection of a malformed transport packet.
- Rejection of `0xfffffffc`, `0xffffffff`, and oversized clear/encrypted packet
  lengths before attempting a payload or MAC read, and overflow-sized SSH
  strings / cursor positions in the shared parser.

The glue uses **deterministic test randomness**. It is never compiled into the
firmware. The test does not validate the ADC entropy source, MeOS scheduler,
MeOS TCP listener, USB Ethernet, or shared-console behavior on the device.
Those require hardware tests, including reconnect, stop/restart, and an outbound
SSH client used while an inbound server session is active.
