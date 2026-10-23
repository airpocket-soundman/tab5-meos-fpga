# SSH and SFTP interoperability tests

Run on Windows with Zig 0.13+ and Python with Paramiko available:

```powershell
python -m pip install paramiko
python tests/ssh/run_protocol_test.py --cc C:/path/to/zig.exe
```

Paramiko is a test-harness dependency. The portable `tools/tab5_storage.py`
helper itself uses Python's standard library and OpenSSH, not Paramiko.

The runner compiles a 32-bit Windows executable (`x86-windows-gnu`, assertions
explicitly enabled) with a boundary assertion that `sizeof(size_t) == 4`.
It extracts the actual production crypto, packet transport and SSH server from
`user/ssh/ssh_app.c` / `ssh_server.inc`, and links the production SFTP engine
and local storage self-test. WinSock substitutes for the device TCP transport;
`host_vfs.inc` maps logical test mounts into a fresh temporary directory.
An unexported `/outside/secret.txt` fixture detects namespace containment
failures. All files are temporary host files; no Tab5 or attached media is used.

## Cases

The suite includes the following 14 connection cases and packet-boundary checks:

- X25519 key exchange, Ed25519 host signature, AES128-CTR/HMAC-SHA256 and password authentication; incorrect passwords are rejected.
- PTY/shared-shell requests and a 20 KiB console stream crossing repeated 4096-byte receive-window credits.
- Unsupported `exec` rejection and malformed SSH packet rejection.
- Clear/encrypted packet lengths `0xfffffffc`, `0xffffffff` and oversized values rejected before payload/MAC reads; overflow-sized strings/cursor positions rejected.
- Standard Paramiko SFTP stat/lstat/fstat, root and 80-entry directory listing, read/write, random offsets, append, mkdir/remove/rmdir and 192 KiB binary upload/download. Default 32 KiB WRITE requests are retained; the client receive window is 32 KiB to exercise both SSH flow-control directions.
- The same file operations while VFS reads/writes return at most 37 bytes, exercising short I/O instead of assuming full completion.
- Exclusive creation with `CREATE_NEW` semantics, including an existing destination and a file injected between SFTP's metadata check and the atomic VFS open. Existing bytes remain unchanged even when TRUNC is also requested.
- Read-only permission bits and refusal of create/write/delete/mkdir/rmdir; storage-disabled subsystem refusal.
- Export containment, parent traversal, an unexported namespace, malformed NUL paths and backslash paths. The host bridge rejects Windows-specific path syntax as well.
- Byte-fragmented SFTP INIT/REALPATH, unknown-extension `OP_UNSUPPORTED`, invalid handles and refusal to switch an SFTP channel into a shell.
- An oversized SFTP frame while a file remains open: channel teardown must occur, not a read timeout. Every process checks that the final VFS open-file count is zero.
- EOF and CLOSE are distinct: after the client sends EOF alone, the SSH transport must remain available for the subsequent channel-close handshake.
- The actual portable helper's `SFTP` class runs over Paramiko channel file streams and the real encrypted MeOS server/engine. It checks 128 KiB binary upload/download, local `get_file` publication, names, mkdir/remove, default exclusive destination protection and explicit overwrite/truncation.

The portable-helper case tests its actual SFTP wire client, not its subprocess
launcher or an OpenSSH executable. No `MAX_REQUEST_SIZE` workaround is applied
in the Paramiko tests. Successful SFTP transfer does not constitute an actual
Linux FUSE/sshfs mount test.

## Scope and limitations

The glue uses **deterministic test randomness** and a test console; it is never
compiled into firmware. Tests validate production SSH/SFTP parsing, encryption,
flow control and operation results against host filesystem behavior. They do
not validate hardware entropy, the MeOS scheduler/TCP listener, C6/Wi-Fi/USB
networking, physical FAT media or hot removal. Actual sshfs mounting and real
OpenSSH helper-process execution require separate environment/device checks.

The server currently offers one authenticated connection and one shared-console
**or** SFTP channel. Storage defaults to read-only; policy changes require the
server to stop. SFTP file offsets are limited by signed 32-bit VFS seek, handles
are bounded, and metadata/rename operations such as SETSTAT and RENAME are
unsupported. A successful transfer test does not imply a complete POSIX mounted
filesystem or simultaneous shell and mount sessions.

The bounded independent audit covered packet and path bounds, OPEN flags and
exclusive creation, handle lifetime, VFS error propagation, read-only policy,
channel mode exclusivity and both SSH window counters. It identified premature
TCP closure on EOF; the server now drains queued SFTP requests/replies, sends
exit status followed by EOF/CLOSE, and waits for the peer CLOSE with a bounded
grace period. The dedicated handshake case pipelines INIT and 32 requests before
half-closing and checks all replies, exit status 0 and graceful channel closure.

Validation on 2026-10-23: all 14 encrypted connection cases and the 32-bit packet-boundary checks passed after the EOF handshake fix. Every connection ended with `VFS_OPEN 0`.
