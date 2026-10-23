"""Windows host interoperability test of production MeOS SSH server code.

Requires Zig cc (or clang with WinSock) and Paramiko; never touches a device.
The Windows transport substitutes deterministic TEST randomness and an echoing
console. ADC entropy, scheduler, USB and the MeOS TCP implementation require
separate hardware validation.
"""
import argparse
import pathlib
import socket
import subprocess
import tempfile
import time
import io
import struct
import stat
import importlib.util

import paramiko

ROOT = pathlib.Path(__file__).resolve().parents[2]


def build(compiler, directory):
    source = (ROOT / "user/ssh/ssh_app.c").read_text(encoding="utf-8")
    start = source.index("#define SSH_MAX_PKT")
    end = source.index("/* =========================================================================", start)
    constants = source[start:end]
    start = source.index("typedef struct {", end)
    end = source.index("/* HMAC-SHA256 DRBG", start)
    crypto = source[start:end]
    start = source.index("typedef struct {", end)
    end = source.index("static bool ssh_handshake", start)
    transport = source[start:end]
    start = source.index("static void ed25519_seed_to_pub")
    end = source.index("\n}", start) + 2
    derive_pub = source[start:end]
    include = '#include "' + (ROOT / "user/ssh/ssh_server.inc").as_posix() + '"\n'
    glue = (ROOT / "tests/ssh/host_transport.c").read_text(encoding="utf-8")
    glue = glue.replace("/* HOST_VFS */", '#include "' + (ROOT / "tests/ssh/host_vfs.inc").as_posix() + '"\n')
    generated = glue.replace("/* PRODUCTION_CODE */", "\n".join(
        [constants, crypto, transport, derive_pub, include]))
    c_file = directory / "server.c"
    exe = directory / "server.exe"
    c_file.write_text(generated, encoding="utf-8")
    sftp_source = ROOT / "user/ssh/sftp_server.c"
    extra = ["-DM5OS_TEST_SFTP=1", str(sftp_source), str(ROOT / "user/ssh/sftp_selftest.c")]
    subprocess.run([compiler, "cc", "-target", "x86-windows-gnu", "-O1", "-UNDEBUG", "-I", str(ROOT / "kernel/include"),
                    str(c_file), *extra, "-lws2_32", "-o", str(exe)], check=True)
    return exe


def client(port, password="testpass123"):
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        c.connect("127.0.0.1", port, username="tester", password=password,
                  look_for_keys=False, allow_agent=False, timeout=10,
                  banner_timeout=10, auth_timeout=15)
    except Exception:
        c.close()
        raise
    return c


def receive_until(channel, marker):
    result = b""
    deadline = time.monotonic() + 10
    while marker not in result:
        if time.monotonic() > deadline:
            raise AssertionError(f"Timeout receiving {marker!r}: {result!r}")
        data = channel.recv(4096)
        if not data:
            raise EOFError(f"Channel closed before {marker!r}")
        result += data
    return result


def positive(port):
    c = client(port)
    try:
        ch = c.invoke_shell(term="xterm", width=80, height=24)
        ch.settimeout(10)
        receive_until(ch, b"share one shell.")
        # Exceeds the advertised 4096-byte window and tests repeated credit.
        text = b"BEGIN_FLOW_" + b"1234567890" * 2000 + b"_END_FLOW\r"
        ch.sendall(text)
        got = receive_until(ch, b"_END_FLOW\r")
        assert text in got, (len(text), len(got))
        ch.resize_pty(width=100, height=30)
        ch.close()
    finally:
        c.close()


def sftp_transfer(port, payload_repeats=768):
    c = client(port)
    try:
        with paramiko.SFTPClient.from_transport(c.get_transport(), window_size=32768,
                                                max_packet_size=4096) as fs:
            fs.get_channel().settimeout(15)
            assert fs.normalize(".") == "/"
            assert {"sd", "usb", "usb1", "usb2", "usb3"} <= set(fs.listdir("/"))
            assert "outside" not in fs.listdir("/")
            assert stat.S_ISDIR(fs.stat("/sd").st_mode)
            assert fs.stat("/sd/base.txt").st_size == len(b"MeOS SFTP fixture\n")
            assert fs.lstat("/sd/base.txt").st_size == fs.stat("/sd/base.txt").st_size
            expected = {f"entry{i:03}.txt" for i in range(80)} | {"base.txt"}
            assert set(fs.listdir("/sd")) == expected
            with fs.open("/sd/base.txt", "rb") as f:
                assert f.read() == b"MeOS SFTP fixture\n"
                assert f.stat().st_size == len(b"MeOS SFTP fixture\n")
            payload = bytes(range(256)) * payload_repeats  # Standard Paramiko 32KiB WRITE requests.
            fs.putfo(io.BytesIO(payload), "/sd/upload.bin", file_size=len(payload), confirm=True)
            got = io.BytesIO()
            assert fs.getfo("/sd/upload.bin", got) == len(payload)
            assert got.getvalue() == payload
            with fs.open("/sd/upload.bin", "r+b") as f:
                f.seek(12345)
                assert f.read(7777) == payload[12345:12345 + 7777]
                f.seek(11)
                f.write(b"PATCH")
            payload = payload[:11] + b"PATCH" + payload[16:]
            with fs.open("/sd/upload.bin", "ab") as f:
                f.write(b"TAIL")
            with fs.open("/sd/upload.bin", "rb") as f:
                assert f.read() == payload + b"TAIL"
            fs.mkdir("/sd/newdir")
            assert stat.S_ISDIR(fs.stat("/sd/newdir").st_mode)
            fs.rmdir("/sd/newdir")
            fs.remove("/sd/upload.bin")
    finally:
        c.close()


def sftp_short_io(port):
    sftp_transfer(port, 300)  # Host VFS deliberately returns at most37 bytes per call.


def sftp_exclusive(port):
    c = client(port)
    try:
        with c.open_sftp() as fs:
            for name in ("/sd/base.txt", "/sd/race.bin"):
                try:
                    with fs.open(name, "wxb") as f:
                        f.write(b"MUST_NOT_REPLACE")
                except IOError:
                    pass
                else:
                    raise AssertionError("Exclusive creation replaced an existing/racing file")
            with fs.open("/sd/base.txt", "rb") as f:
                assert f.read() == b"MeOS SFTP fixture\n"
            with fs.open("/sd/race.bin", "rb") as f:
                assert f.read() == b"RACE_ORIGINAL"
            with fs.open("/sd/exclusive.bin", "wxb") as f:
                f.write(b"NEW_FILE")
            with fs.open("/sd/exclusive.bin", "rb") as f:
                assert f.read() == b"NEW_FILE"
    finally:
        c.close()


def portable_helper(port):
    spec = importlib.util.spec_from_file_location("tab5_storage_interop", ROOT / "tools/tab5_storage.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    c, ch = sftp_channel(port)
    incoming, outgoing = ch.makefile("rb"), ch.makefile("wb")
    try:
        fs = helper.SFTP(incoming, outgoing)
        assert "base.txt" in set(fs.names("/sd"))
        payload = bytes(range(256)) * 512  #128KiB over actual encrypted MeOS SSH.
        assert fs.upload(io.BytesIO(payload), "/sd/helper.bin") == len(payload)
        got = io.BytesIO()
        assert fs.download("/sd/helper.bin", got) == len(payload)
        assert got.getvalue() == payload
        try:
            fs.upload(io.BytesIO(b"MUST_NOT_REPLACE"), "/sd/helper.bin")
        except helper.StatusError:
            pass
        else:
            raise AssertionError("Portable helper default upload overwrote an existing file")
        check = io.BytesIO()
        fs.download("/sd/helper.bin", check)
        assert check.getvalue() == payload
        with tempfile.TemporaryDirectory(prefix="meos-helper-get-") as local:
            destination = pathlib.Path(local) / "download.bin"
            assert helper.get_file(fs, "/sd/helper.bin", destination) == len(payload)
            assert destination.read_bytes() == payload
        assert fs.upload(io.BytesIO(b"REPLACE"), "/sd/helper.bin", overwrite=True) == 7
        check = io.BytesIO()
        fs.download("/sd/helper.bin", check)
        assert check.getvalue() == b"REPLACE"
        fs.mkdir("/sd/helperdir")
        assert "helperdir" in set(fs.names("/sd"))
        fs.remove("/sd/helper.bin")
        assert "helper.bin" not in set(fs.names("/sd"))
    finally:
        outgoing.close()
        incoming.close()
        ch.close()
        c.close()


def sftp_readonly(port):
    c = client(port)
    try:
        with c.open_sftp() as fs:
            fs.get_channel().settimeout(10)
            assert fs.stat("/sd/base.txt").st_mode & 0o222 == 0
            with fs.open("/sd/base.txt", "rb") as f:
                assert f.read() == b"MeOS SFTP fixture\n"
            operations = [lambda: fs.open("/sd/new.bin", "wb"),
                          lambda: fs.open("/sd/base.txt", "r+b"),
                          lambda: fs.remove("/sd/base.txt"),
                          lambda: fs.mkdir("/sd/forbidden"),
                          lambda: fs.rmdir("/sd")]
            for op in operations:
                try:
                    result = op()
                    if hasattr(result, "close"):
                        result.close()
                except IOError:
                    pass
                else:
                    raise AssertionError("Read-only export accepted a modifying operation")
    finally:
        c.close()


def sftp_containment(port):
    c = client(port)
    try:
        with c.open_sftp() as fs:
            fs.get_channel().settimeout(10)
            for path in ["/outside/secret.txt", "/sd/../outside/secret.txt",
                         "/sd/../../outside/secret.txt", "../outside/secret.txt",
                         "/sdx/base.txt", "/dev/uart", "/sd\\..\\outside\\secret.txt",
                         "/sd/base.txt\0evil"]:
                try:
                    with fs.open(path, "rb") as f:
                        f.read(16)
                except IOError:
                    pass
                else:
                    raise AssertionError(f"Unexported or malformed path accepted: {path!r}")
            with fs.open("/sd/../usb/other.txt", "rb") as f:
                assert f.read() == b"USB fixture\n"
    finally:
        c.close()


def sftp_channel(port):
    c = client(port)
    ch = c.get_transport().open_session(timeout=10)
    ch.settimeout(10)
    ch.invoke_subsystem("sftp")
    return c, ch


def recv_exact(ch, n):
    out = b""
    while len(out) < n:
        data = ch.recv(n - len(out))
        if not data:
            raise EOFError("SFTP channel closed")
        out += data
    return out


def sftp_packet(ch, kind, payload=b"", fragmented=False):
    data = struct.pack(">IB", len(payload) + 1, kind) + payload
    if fragmented:
        for byte in data:
            ch.sendall(bytes([byte]))
    else:
        ch.sendall(data)
    length = struct.unpack(">I", recv_exact(ch, 4))[0]
    assert 1 <= length <= 65536, length
    response = recv_exact(ch, length)
    return response[0], response[1:]


def sftp_string(data):
    return struct.pack(">I", len(data)) + data


def sftp_raw(port):
    c, ch = sftp_channel(port)
    try:
        kind, body = sftp_packet(ch, 1, struct.pack(">I", 3), fragmented=True)
        assert kind == 2 and struct.unpack(">I", body[:4])[0] == 3
        kind, body = sftp_packet(ch, 200, struct.pack(">I", 1) + sftp_string(b"unknown@test"))
        assert kind == 101 and struct.unpack(">II", body[:8]) == (1, 8)
        # Header and strings arrive across many independent SSH CHANNEL_DATA packets.
        kind, body = sftp_packet(ch, 16, struct.pack(">I", 2) + sftp_string(b"/sd/./base.txt"), True)
        assert kind == 104 and struct.unpack(">I", body[:4])[0] == 2
        kind, body = sftp_packet(ch, 4, struct.pack(">I", 3) + sftp_string(b"not-a-handle"))
        assert kind == 101 and struct.unpack(">I", body[:4])[0] == 3
        assert struct.unpack(">I", body[4:8])[0] != 0
        # Deliberately leave an actual file open; session teardown must close it.
        request = struct.pack(">I", 4) + sftp_string(b"/sd/base.txt") + struct.pack(">II", 1, 0)
        kind, body = sftp_packet(ch, 3, request)
        assert kind == 102
        # A session already assigned to SFTP must not become a shared shell.
        try:
            ch.invoke_shell()
        except paramiko.SSHException:
            pass
        else:
            raise AssertionError("SFTP channel accepted a shell mode switch")
        ch.close()
    finally:
        c.close()


def sftp_eof_handshake(port):
    c, ch = sftp_channel(port)
    try:
        # Pipeline requests then half-close before reading any responses. The
        # server must drain both queued requests and replies before channel EOF.
        wire = struct.pack(">IBI", 5, 1, 3)
        for request_id in range(1, 33):
            payload = struct.pack(">I", request_id) + sftp_string(b"/sd")
            wire += struct.pack(">IB", len(payload) + 1, 16) + payload
        ch.sendall(wire)
        ch.shutdown_write()  # EOF is distinct from CHANNEL_CLOSE.
        for index in range(33):
            size = struct.unpack(">I", recv_exact(ch, 4))[0]
            response = recv_exact(ch, size)
            assert response[0] == (2 if index == 0 else 104)
            if index:
                assert struct.unpack(">I", response[1:5])[0] == index
        # The subsystem may finish at EOF. It must deliver a successful process
        # status and gracefully finish channel CLOSE before tearing down TCP.
        assert ch.recv_exit_status() == 0
        deadline = time.monotonic() + 2
        while not ch.closed and time.monotonic() < deadline:
            time.sleep(0.005)
        assert ch.closed, "Server did not complete channel close"
        ch.close()  # Idempotent, without Paramiko EOFError from a TCP race.
    finally:
        c.close()


def sftp_malformed(port):
    c, ch = sftp_channel(port)
    try:
        assert sftp_packet(ch, 1, struct.pack(">I", 3))[0] == 2
        request = struct.pack(">I", 1) + sftp_string(b"/sd/base.txt") + struct.pack(">II", 1, 0)
        assert sftp_packet(ch, 3, request)[0] == 102
        # App-frame length exceeds the engine cap; no unbounded allocation/read.
        ch.sendall(struct.pack(">I", 0xffffffff))
        try:
            assert ch.recv(1) == b""
        except socket.timeout as exc:
            raise AssertionError("Oversized SFTP frame did not close the channel") from exc
        except (EOFError, OSError):
            pass
    finally:
        c.close()


def sftp_disabled(port):
    c = client(port)
    try:
        try:
            c.open_sftp()
        except paramiko.SSHException:
            return
        raise AssertionError("Disabled SFTP subsystem accepted")
    finally:
        c.close()


def bad_password(port):
    try:
        c = client(port, "wrong-password")
    except paramiko.AuthenticationException:
        return
    c.close()
    raise AssertionError("Incorrect password was accepted")


def reject_exec(port):
    c = client(port)
    try:
        try:
            c.exec_command("echo MUST_NOT_RUN", timeout=5)
        except paramiko.SSHException:
            return
        raise AssertionError("Unsupported exec request was accepted")
    finally:
        c.close()


def bad_packet(port):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.recv(4096)
        s.sendall(b"SSH-2.0-protocol-test\r\n" + b"\0\0\0\0")
        while s.recv(4096):
            pass


def run_case(exe, fn, expected_auth, policy="rw", io_limit=0):
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    root = exe.parent / (fn.__name__ + "-vfs")
    root.mkdir()
    for mount in ("sd", "usb", "usb1", "usb2", "usb3", "outside"):
        (root / mount).mkdir()
    (root / "sd/base.txt").write_bytes(b"MeOS SFTP fixture\n")
    (root / "usb/other.txt").write_bytes(b"USB fixture\n")
    (root / "outside/secret.txt").write_bytes(b"MUST_NOT_EXPORT")
    for i in range(80):
        (root / "sd" / f"entry{i:03}.txt").write_bytes(bytes([i]))
    p = subprocess.Popen([str(exe), str(port), str(root), policy, str(io_limit)], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    try:
        assert p.stdout.readline().strip() == "READY"
        fn(port)
        p.wait(timeout=10)
        diagnostic = p.stderr.read()
        assert p.returncode == (0 if expected_auth else 1), diagnostic
        assert "VFS_OPEN 0" in diagnostic, diagnostic
        print(f"PASS {fn.__name__}: {diagnostic.strip()}")
    except BaseException as exc:
        if p.poll() is None:
            p.kill()
            p.wait()
        exc.add_note("Server diagnostics: " + p.stderr.read())
        raise
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True, help="Path to zig executable")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="meos-ssh-") as path:
        exe = build(args.cc, pathlib.Path(path))
        subprocess.run([str(exe), "--bounds"], check=True)
        cases = [(positive, True, "rw", 0), (bad_password, False, "rw", 0),
                 (reject_exec, True, "rw", 0), (bad_packet, False, "rw", 0),
                 (sftp_transfer, True, "rw", 0), (sftp_short_io, True, "rw", 37),
                 (sftp_exclusive, True, "rw", 0), (portable_helper, True, "rw", 0),
                 (sftp_readonly, True, "ro", 0),
                 (sftp_containment, True, "rw", 0), (sftp_raw, True, "rw", 0),
                 (sftp_malformed, True, "rw", 0), (sftp_eof_handshake, True, "rw", 0),
                 (sftp_disabled, True, "off", 0)]
        for fn, authenticated, policy, limit in cases:
            run_case(exe, fn, authenticated, policy, limit)


if __name__ == "__main__":
    main()
