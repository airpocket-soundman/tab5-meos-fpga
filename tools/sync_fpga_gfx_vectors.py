"""Freeze independent carrier oracle vectors and source hashes; never edits carrier."""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import random
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--carrier", type=Path, required=True)
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
carrier = args.carrier.resolve()
sys.dont_write_bytecode = True
sys.path.insert(0, str(carrier / "fpga"))
oracle = importlib.import_module("reference.gfx_bench")
wire = importlib.import_module("reference.link_protocol")
model = oracle.GfxBench()
vectors = []


def emit(op, a=0, b=0, bad_crc=False):
    seq = len(vectors) & 255
    q = bytearray(wire.seal(bytes([0xa5, op, seq, 2, a, b])))
    if bad_crc:
        q[7] ^= 1
    r = model.exchange(q)
    vectors.append({"request": q.hex(), "response": r.hex()})


emit(1)
emit(0x10)
for op in (0x13, 0x14, 0x16):
    emit(op, 1, 1)  # uninitialized
    emit(op, 255, 255)  # range before uninitialized
emit(0x11, 0x34, 0x12)
emit(0x12)
emit(0x15, 31, 31)
emit(0x16, 2, 1)  # out of bounds
emit(0x16, 0, 1)
emit(0x16, 1, 1)
emit(0x14, 31, 31)
emit(0x11, 0xcd, 0xab, True)  # rejected mutation
emit(0x13, 0, 0)
emit(0x14, 0, 0)
rng = random.Random(20261028)
for _ in range(400):
    op = rng.choice([1, 2, 3, *range(0x10, 0x18)])
    a, b = rng.randrange(256), rng.randrange(256)
    if op in (0x13, 0x14, 0x15, 0x16) and rng.random() < .8:
        a, b = rng.randrange(33), rng.randrange(33)
    emit(op, a, b, rng.random() < .06)
for y in range(32):
    for x in range(32):
        emit(0x14, x, y)

paths = ["fpga/link/DOCK_GFX_V0.md", "fpga/host/link_protocol.h",
         "fpga/reference/gfx_bench.py", "fpga/reference/link_protocol.py",
         "fpga/gfx/rtl/gfx_bench.v"]
document = {
    "carrier_base_commit": subprocess.check_output(
        ["git", "-C", str(carrier), "rev-parse", "HEAD"], text=True).strip(),
    "contract": "uncommitted codex/dock-jumper-gfx working tree; source hashes identify content",
    "source_sha256": {p: hashlib.sha256((carrier / p).read_bytes()).hexdigest() for p in paths},
    "seed": 20261028, "vectors": vectors,
}
target = Path(__file__).resolve().parents[1] / "tests/fixtures/fpga_dock_gfx_v0.json"
content = json.dumps(document, indent=2) + "\n"
if args.check:
    if target.read_text() != content:
        raise SystemExit("Carrier GFX vectors/source hashes changed; review before syncing")
else:
    target.write_text(content)
print(f"{len(vectors)} carrier GFX vectors {'verified' if args.check else 'written'}")
