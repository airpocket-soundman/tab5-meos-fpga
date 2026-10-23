# SFTP v3 engine checks

Run on Windows with `python tests/sftp/run_server_test.py --cc PATH/TO/zig.exe`.
The runner builds an actual32-bit executable and asserts32-bit size_t. It
compiles the production engine against an in-memory VFS implementation, not a
mock SFTP parser. Filesystem calls assert that no internal/device path reaches
the backend.

Coverage includes fragmented and coalesced wire frames, partial output pulls,
allocation failure, exact64KiB frames and larger/0xfffffffc/0xffffffff length
rejection, malformed strings, normalized relative paths, traversal, alternate FAT separators/drive markers and embedded
NUL rejection, RO/RW policy,32KiB writes with short VFS I/O, stat/FSTAT position
restoration, directory listing, stale handles and four-handle exhaustion,
exclusive create and existing-file preservation, mount disappearance, close
errors and disconnect cleanup. Encrypted Paramiko/SSH interoperability is
covered separately by `tests/ssh/run_protocol_test.py`.

The engine keeps one response pending. Its caller must retain unconsumed input,
drain output and then resume feeding. All mutable session state lives in one
approximately70KiB PSRAM allocation; READ data is written directly into the
bounded response buffer. Each session belongs to one task. Destroy it on every
transport exit, including incomplete requests and protocol failure.

Only virtual `/` and mounted `/sd`, `/usb`, `/usb1`, `/usb2`, `/usb3` trees are
visible. Read-only is the creation default selected by the SSH integration.
Writable sessions allow file creation/write/truncation/deletion and directory
creation/removal. SETSTAT/FSETSTAT/RENAME and extensions are explicitly
unsupported. Exclusive create is passed to the atomic VFS/FAT new-file flag;
it is never emulated by checking existence and then opening a file.

Packets are limited to64KiB (excluding the length prefix); READ data is limited
to4KiB per response and clients can request subsequent chunks. Four handles
are available per session. Offsets and resulting file positions are restricted
to signed32-bit VFS limits. File CLOSE propagates filesystem flush/close errors;
there is no advertised fsync extension. STAT exposes size and server-policy
mode bits only:0444/0666 for files,0555/0777 for directories. These are synthetic
access-policy bits, not persisted POSIX metadata. No UID/GID or timestamps are
fabricated. Directory enumeration is incremental and not a snapshot if local
writers change the directory concurrently.
