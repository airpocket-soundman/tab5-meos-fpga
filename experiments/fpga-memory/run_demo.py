"""Build and run a host-only file mock; no MeOS or device operations."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--zig", action="store_true", help="Use zig cc instead of a C compiler")
parser.add_argument("--mock", action="store_true", required=True)
parser.add_argument("--size", type=int, default=1024 * 1024 + 17)
parser.add_argument("--transfer-limit", type=int, default=4096)
args = parser.parse_args()
if not 0 <= args.size <= 64 * 1024 * 1024 or not 1 <= args.transfer_limit <= 65536:
    parser.error("size must be 0..64MiB and transfer-limit 1..65536")
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="meos-fpga-demo-") as tmp:
    exe = Path(tmp) / "demo_memory.exe"
    subprocess.run([args.cc, *(["cc"] if args.zig else []), "-std=c11", "-Wall",
                    "-Wextra", "-Werror", "-O2", str(root / "fpga_memory.c"),
                    str(root / "demo_memory.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe), "--mock", "--size", str(args.size),
                    "--transfer-limit", str(args.transfer_limit)], check=True)
