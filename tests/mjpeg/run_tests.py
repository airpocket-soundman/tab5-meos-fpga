#!/usr/bin/env python3
"""Host test of drivers/mjpeg against Pillow.

  python tests/mjpeg/run_tests.py [--cc CL_OR_GCC]

Builds tests/mjpeg/mjpeg_cli.c with MSVC (found through vswhere) or the given compiler, then decodes JPEGs made
by Pillow (several sizes, subsamplings, qualities, grey, restart intervals, no-DHT frames) at every reduction and
compares the RGB565 output with Pillow's decode (box-filtered for the reductions).  Pillow uses smoothed chroma
upsampling and the decoder replicates chroma, so the thresholds are PSNR values, not exact matches.
"""
import argparse
import io
import math
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def build(cc: str | None, workdir: Path) -> Path:
    exe = workdir / "mjpeg_cli.exe"
    src = [str(ROOT / "tests/mjpeg/mjpeg_cli.c"), str(ROOT / "drivers/mjpeg/mjpeg.c")]
    inc = str(ROOT / "drivers/mjpeg")
    if cc:
        cmd = [cc, "-O2", "-Wall", "-Wextra", "-I", inc, *src, "-o", str(exe)]
        subprocess.run(cmd, check=True)
        return exe
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    root = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                          capture_output=True, text=True, check=True).stdout.strip()
    vcvars = Path(root) / "VC/Auxiliary/Build/vcvars64.bat"
    bat = workdir / "build.bat"
    bat.write_text(f'@call "{vcvars}" >nul\r\n@cl /nologo /O2 /W3 /I "{inc}" ' + " ".join(f'"{s}"' for s in src) +
                   f' /Fe:"{exe}" /Fo:"{workdir}\\\\"\r\n')
    r = subprocess.run(["cmd", "/c", str(bat)], capture_output=True, text=True, errors="replace")
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("build failed")
    warnings = [l for l in r.stdout.splitlines() if "warning" in l.lower()]
    if warnings:
        print("\n".join(warnings))
    return exe


def make_image(w: int, h: int, seed: int) -> Image.Image:
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = 128 + 100 * np.sin(xx / 17.0 + seed) * np.cos(yy / 23.0)
    g = 128 + 90 * np.sin((xx + yy) / 29.0)
    b = 128 + 100 * np.cos(xx / 13.0 - yy / 31.0)
    img = np.stack([r, g, b], axis=-1)
    cy, cx = h * 0.4, w * 0.6
    mask = (xx - cx) ** 2 + (yy - cy) ** 2 < (min(w, h) * 0.25) ** 2
    img[mask] = (230, 60, 40)
    img[: h // 5, : w // 3] = (20, 20, 20)
    img += rng.normal(0, 3, img.shape)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def strip_dht(jpeg: bytes) -> bytes:
    out = bytearray(jpeg[:2])
    i = 2
    while i < len(jpeg):
        marker = jpeg[i + 1]
        length = struct.unpack(">H", jpeg[i + 2:i + 4])[0]
        if marker == 0xDA:
            out += jpeg[i:]
            break
        if marker != 0xC4:
            out += jpeg[i:i + 2 + length]
        i += 2 + length
    return bytes(out)


def rgb565_to_rgb(raw: bytes, w: int, h: int) -> np.ndarray:
    px = np.frombuffer(raw, dtype="<u2").reshape(h, w).astype(np.int32)
    r = (px >> 11) << 3
    g = ((px >> 5) & 63) << 2
    b = (px & 31) << 3
    return np.stack([r | (r >> 5), g | (g >> 6), b | (b >> 5)], axis=-1).astype(np.float32)


def psnr(a: np.ndarray, b: np.ndarray) -> float:
    mse = float(np.mean((a - b) ** 2))
    return 99.0 if mse == 0 else 10 * math.log10(255.0 ** 2 / mse)


def reduce_box(arr: np.ndarray, n: int) -> np.ndarray:
    """Box-average by `n` (edge blocks use what exists), like a DCT-domain reduction."""
    h, w = arr.shape[:2]
    oh, ow = -(-h // n), -(-w // n)
    pad = np.pad(arr, ((0, oh * n - h), (0, ow * n - w), (0, 0)), mode="edge")
    return pad.reshape(oh, n, ow, n, -1).mean(axis=(1, 3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc")
    args = ap.parse_args()
    failures = 0
    checks = 0
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        exe = build(args.cc, work)

        def decode(jpeg: bytes, shift: int):
            f = work / "in.jpg"
            f.write_bytes(jpeg)
            r = subprocess.run([str(exe), str(f), str(shift), str(work / "out.raw")], capture_output=True, text=True)
            if r.returncode:
                return None
            w, h = map(int, r.stdout.split())
            return w, h, (work / "out.raw").read_bytes()

        cases = []
        for (w, h) in ((64, 48), (100, 75), (353, 289), (640, 480), (17, 9)):
            for sub, label in ((0, "4:4:4"), (1, "4:2:2"), (2, "4:2:0")):
                for q in (50, 90):
                    cases.append((w, h, sub, label, q, "RGB", False))
            cases.append((w, h, 2, "4:2:0", 75, "RGB", True))   # no DHT, like many cameras
            cases.append((w, h, 0, "grey", 80, "L", False))
            cases.append((w, h, 2, "4:2:0", 80, "RGB", "rst"))   # restart intervals (every 3 MCU rows' worth)

        for w, h, sub, label, q, mode, nodht in cases:
            img = make_image(w, h, w + h)
            if mode == "L":
                img = img.convert("L")
            buf = io.BytesIO()
            kw = dict(quality=q)
            if mode == "RGB":
                kw["subsampling"] = sub
            if nodht == "rst":
                kw["restart_marker_blocks"] = 3
            img.save(buf, "JPEG", **kw)
            jpeg = buf.getvalue()
            if nodht is True:
                jpeg = strip_dht(jpeg)
            ref = np.asarray(Image.open(io.BytesIO(buf.getvalue())).convert("RGB"), dtype=np.float32)
            for shift in (0, 1, 2, 3):
                n = 8 >> shift
                got = decode(jpeg, shift)
                tag = f"{w}x{h} {label} q{q}{' no-DHT' if nodht is True else ''}{' restart' if nodht == 'rst' else ''} shift{shift}"
                checks += 1
                if got is None:
                    print("FAIL decode failed:", tag)
                    failures += 1
                    continue
                gw, gh, raw = got
                ew, eh = (w * n + 7) // 8, (h * n + 7) // 8
                if (gw, gh) != (ew, eh):
                    print(f"FAIL size {gw}x{gh}, expected {ew}x{eh}: {tag}")
                    failures += 1
                    continue
                out = rgb565_to_rgb(raw, gw, gh)
                want = reduce_box(ref, 8 // n) if shift else ref
                want = want[:gh, :gw]
                score = psnr(out, want)
                # Chroma is replicated here but smoothed by Pillow/libjpeg, and a 1/8 reduction gives each chroma
                # sample of 4:2:0 data 16 source pixels; so RGB thresholds are lower for those, and a luma-only
                # check (which does not depend on chroma handling) guards the entropy decoder and the IDCT.
                subsampled = label != "4:4:4" and mode == "RGB"
                if shift == 0:
                    limit = 32 if subsampled else 36
                    if subsampled and min(w, h) < 32:
                        limit = 28
                else:
                    limit = 20 if (label == "4:2:0" and shift == 3) else 24
                    if min(w, h) < 32:
                        limit = 18
                luma = np.array([0.299, 0.587, 0.114], dtype=np.float32)
                luma_score = psnr((out @ luma)[..., None], (want @ luma)[..., None])
                luma_limit = 34 if min(w, h) >= 32 else 26
                ok = score >= limit and luma_score >= luma_limit
                if not ok:
                    failures += 1
                print(f"{'ok  ' if ok else 'FAIL'} rgb {score:5.1f} dB (>= {limit}) luma {luma_score:5.1f} dB "
                      f"(>= {luma_limit}) {tag} -> {gw}x{gh}")

        # malformed input must be rejected, not crash
        base = io.BytesIO()
        make_image(64, 48, 1).save(base, "JPEG", quality=80)
        data = base.getvalue()
        for name, blob in (("truncated", data[: len(data) // 2]), ("garbage", b"\xff\xd8" + bytes(range(256)) * 4),
                           ("empty", b""), ("SOI only", b"\xff\xd8")):
            checks += 1
            got = decode(blob, 0)
            print(("ok   " if got is None else "FAIL ") + f"rejected: {name}")
            if got is not None:
                failures += 1
        prog = io.BytesIO()
        make_image(64, 48, 1).save(prog, "JPEG", quality=80, progressive=True)
        checks += 1
        got = decode(prog.getvalue(), 0)
        print(("ok   " if got is None else "FAIL ") + "rejected: progressive")
        failures += got is not None
    print(f"\n{checks - failures}/{checks} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
