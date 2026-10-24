"""Host tests only; no firmware, device, serial or network operations."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="cc", help="C compiler executable")
parser.add_argument("--zig", action="store_true", help="Use zig cc")
parser.add_argument("--sanitize", action="store_true", help="Address/undefined sanitizers")
args = parser.parse_args()
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="meos-fpga-memory-") as tmp:
    exe = Path(tmp) / "test_memory.exe"
    subprocess.run([args.cc, *(["cc"] if args.zig else []),
                    *(["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []),
                    "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-UNDEBUG", "-O1", str(root / "fpga_memory.c"),
                    str(root / "test_memory.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
