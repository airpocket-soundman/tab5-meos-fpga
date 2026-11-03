"""Compile real GPIO transport/CLI against a pin-level Dock model (no hardware)."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True, help="Zig executable")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="meos-dock-") as tmp:
        exe = Path(tmp) / "gpio.exe"
        sources = ["drivers/fpga/fpga_link.c", "drivers/fpga/fpga_gpio.c",
                   "drivers/fpga/fpga_mock.c", "drivers/fpga/fpga_selftest.c",
                   "user/fpga/fpga_app.c", "tests/fpga_gpio_fixture.c"]
        subprocess.run([args.cc, "cc", "-std=c11", "-O2", "-UNDEBUG",
                        "-DM5OS_MODULE_FPGA=1", "-DM5OS_FPGA_DOCK_VERIFIED=1", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "kernel/include"),
                        *[str(ROOT / s) for s in sources], "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        for command, count in [(["id"], 1), (["echo", "85", "170"], 1),
                               (["invert", "0", "255"], 1), (["test"], 513)]:
            report = json.loads(subprocess.check_output(
                [str(exe), "fpga", "--dock", "dock-jumper-v0", *command], text=True))
            assert report["result"] == "ok" and report["transactions"] == count
            assert report["reset_required"] is False
            assert report["sent"] == 8 and report["received"] == 8
            assert len(bytes.fromhex(report["request_hex"])) == len(bytes.fromhex(report["response_hex"])) == 8
        for command in [["--dock"], ["--dock", "carrier", "id"],
                        ["--dock", "dock-jumper-v0", "echo", "256", "0"]]:
            assert subprocess.run([str(exe), "fpga", *command], capture_output=True).returncode
        print("Dock CLI: ID/echo/invert/513-exchange test and invalid arguments passed")

        gated = Path(tmp) / "gated.exe"
        subprocess.run([args.cc, "cc", "-std=c11", "-O2", "-UNDEBUG",
                        "-DM5OS_MODULE_FPGA=1", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "kernel/include"),
                        *[str(ROOT / s) for s in sources], "-o", str(gated)], check=True)
        subprocess.run([str(gated)], check=True)
