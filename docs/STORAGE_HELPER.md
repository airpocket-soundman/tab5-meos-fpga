# Linux storage helper

`tools/tab5_storage.py` is a single Python 3 standard-library script. It connects to the Tab5 SSH server and speaks SFTP v3 over `ssh -s HOST sftp`. No Python packages are required. OpenSSH retains its normal known-host verification, SSH configuration, key authentication and password prompt via the local terminal. The helper does not disable host-key checks, store passwords, run sudo, or install dependencies.

The Linux machine must be able to reach the Tab5 SSH server's IP and port. Exporting storage through an existing SSH connection initiated by Tab5, or through an automatic reverse tunnel, is not implemented. The current MeOS server supports one SSH connection: a foreground mount occupies it, so another remote SSH Shell or transfer cannot run concurrently. The device's local console remains usable.

## Commands

```sh
python3 tools/tab5_storage.py doctor
python3 tools/tab5_storage.py connect TAB5_IP /sd "$HOME/tab5-sd" --user meos
python3 tools/tab5_storage.py ls TAB5_IP /sd --user meos
python3 tools/tab5_storage.py get TAB5_IP /sd/example.txt ./example.txt --user meos
python3 tools/tab5_storage.py put TAB5_IP ./example.txt /usb/example.txt --user meos
python3 tools/tab5_storage.py mkdir TAB5_IP /sd/new-directory --user meos
python3 tools/tab5_storage.py rm TAB5_IP /sd/unneeded.txt --user meos
```

Use `--port 2222` for a nondefault port. `user@HOST` is also accepted; do not combine it with `--user`. Host/user options cannot begin with `-`, and arguments are passed as a subprocess argument list. Transfer paths travel in the SFTP binary protocol, never in a remote Shell command. The exposed storage roots are `/sd`, `/usb`, `/usb1`, `/usb2`, `/usb3`; unavailable devices return the server's error. Parent-directory components and paths outside these roots are rejected.

`put` uses SFTP's exclusive-create flag by default and refuses an existing destination. `--overwrite` explicitly requests truncation; the MeOS FAT backend supports this explicit replacement. Failed uploads can leave the newly created remote file containing a partial transfer; inspect it before explicitly removing/retrying. No remote cleanup/delete is automatic. Rename, resume, truncate-by-offset, metadata preservation and recursive copy are not provided.

`get` downloads to a newly created temporary file in the destination directory and publishes it using an exclusive hard link. An existing file or symlink is never overwritten. Partial-download and publication failures remove only the temporary pathname created by that call. The destination filesystem must support hard links; a filesystem error is reported otherwise. No fallback silently replaces a destination.

## Mount

Install/configure OpenSSH, SSHFS and FUSE through your normal Linux administration process. `doctor` reports Python, executable paths, fusermount permission/setuid bits and `/dev/fuse` read/write access. These observations do not prove that a container's security policy or the kernel permits a mount. Missing mount dependencies do not prevent normal `ls/get/put/mkdir/rm` transfers.

```sh
mkdir -p "$HOME/tab5-sd"
python3 tools/tab5_storage.py mount TAB5_IP /sd "$HOME/tab5-sd" --user meos
# Explicit write-enabled mount:
python3 tools/tab5_storage.py mount TAB5_IP /sd "$HOME/tab5-sd" --user meos --rw
# From another terminal when needed:
fusermount3 -u "$HOME/tab5-sd"
```

The mountpoint must already exist and be empty. SSHFS runs in the foreground with `ro` by default; `--rw` is required for a write-enabled mount. The helper sets `max_write=32768` to stay within MeOS's 64 KiB SFTP request-frame limit. MeOS may return shorter READ data (currently at most 4 KiB), which the transfer client accepts. Mount roots must be exactly one of the five storage roots, rather than an arbitrary subdirectory.

Write-enabled mounting does not add missing VFS operations. Editors and file managers that depend on rename, SETSTAT/FSETSTAT (including truncate-by-offset), or other unsupported requests can fail; simple explicit file transfer is the predictable alternative. A successful SSHFS process start alone does not verify compatibility with every filesystem operation.

## Distribution and verification

Copy the script alone to the Linux machine and run it with `python3`. An optional standard-library zipapp can contain the same file as `__main__.py`; it adds no runtime packages.

Run `python3 tests/storage/test_client.py`. The tests exercise real subprocess pipes with a mock SSH/SFTP peer, fragmented frames, directory iteration, binary downloads, multi-chunk uploads, exclusive-create refusal, unsupported truncation, response validation, local destination preservation and partial cleanup, command argument isolation, and read-only foreground mount construction. They do not mount a real FUSE filesystem or establish real SSH authentication. MeOS device interoperability and Linux SSHFS mounting remain separate integration checks.

## Automatic selection and MeOS policy

`connect` checks for an existing SSHFS/FUSE environment and tries the foreground mount. If dependencies are absent or the mount fails, it opens an interactive SFTP transfer prompt instead. The prompt supports `ls`, `get`, `put`, `mkdir`, `rm`, and `quit`, confined to the selected storage root. Read-only is the default; `--rw` enables prompt writes or a writable mount. It never installs packages or elevates privileges.

On the Tab5 console, configure authentication using the existing SSH server commands. Storage defaults to read-only. To permit writes, stop the server, run `sshd storage rw`, then start it again. `sshd storage ro` restores read-only and `sshd storage off` disables the subsystem. Policy changes are refused while the server task is active. Four handles and signed 32-bit file offsets are supported. Only mounted SD/USB roots are exported; ordinary system files and device nodes are excluded.

The production engine and encrypted SSH transport are covered by 32-bit host tests, including the actual portable helper client over an encrypted Paramiko channel. The hardware command `sshd storage-test /sd` exercises the same engine against a fresh exclusively created SD file. It does not test network transport or Linux FUSE. Actual Linux SSHFS mounting remains unverified.


## Device network checks, 2026-10-23

Over the connected Tab5's Wi-Fi, Paramiko and the actual portable helper's SFTP
protocol class passed two 102400-byte SD upload/download roundtrips. Exclusive
create refused an existing file and its contents remained unchanged; delete,
reconnect, read-only root listing and write denial also passed. The serial
`sshd fingerprint` output (SHA256 hexadecimal) was compared against the network
host key before password authentication. This local CLI is available while the
server is running; compare fingerprints in the same encoding.

The actual `tools/tab5_storage.py` process then passed with Windows OpenSSH 9:
a 20480-byte SD put/get matched exactly, and ls/rm succeeded. The test used a
configuration-only launcher, isolated known_hosts whose key was verified against
the serial fingerprint, and askpass; user SSH configuration was not modified.
This verifies the actual helper/OpenSSH process path on Windows, not interactive
password entry on a Linux terminal.

The SSH server needed orderly EOF/close handling: drain queued replies, send
exit-status/EOF/CLOSE and wait for the peer CLOSE rather than immediately dropping
TCP. The fix passed 14 encrypted host cases, including 33 pipelined responses;
final firmware was 1,410,224 bytes.

Actual Linux OpenSSH process/terminal prompting and Linux SSHFS/FUSE mounting
remain unverified. All real network storage transfers above used SD; USB media
network transfers remain untested. The single-inbound-connection and
no-reverse-export limits still apply.
