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
    generated = glue.replace("/* PRODUCTION_CODE */", "\n".join(
        [constants, crypto, transport, derive_pub, include]))
    c_file = directory / "server.c"
    exe = directory / "server.exe"
    c_file.write_text(generated, encoding="utf-8")
    subprocess.run([compiler, "cc", "-target", "x86-windows-gnu", "-O1", "-I", str(ROOT / "kernel/include"),
                    str(c_file), "-lws2_32", "-o", str(exe)], check=True)
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
        result += channel.recv(4096)
    return result


def positive(port):
    c = client(port)
    try:
        ch = c.invoke_shell(term="xterm", width=80, height=24)
        ch.settimeout(10)
        receive_until(ch, b"share one shell.")
        # Exceeds the advertised 255-byte window and tests repeated credit.
        text = b"BEGIN_FLOW_" + b"1234567890" * 200 + b"_END_FLOW\r"
        ch.sendall(text)
        got = receive_until(ch, b"_END_FLOW\r")
        assert text in got, (len(text), len(got))
        ch.resize_pty(width=100, height=30)
        ch.close()
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


def run_case(exe, fn, expected_auth):
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    p = subprocess.Popen([str(exe), str(port)], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    try:
        assert p.stdout.readline().strip() == "READY"
        fn(port)
        p.wait(timeout=10)
        diagnostic = p.stderr.read()
        assert p.returncode == (0 if expected_auth else 1), diagnostic
        print(f"PASS {fn.__name__}: {diagnostic.strip()}")
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
        for fn, authenticated in [(positive, True), (bad_password, False),
                                  (reject_exec, True), (bad_packet, False)]:
            run_case(exe, fn, authenticated)


if __name__ == "__main__":
    main()
