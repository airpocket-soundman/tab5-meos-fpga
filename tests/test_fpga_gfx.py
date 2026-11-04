"""GFX host/CLI + native GPIO model and independent carrier golden vectors."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True, help="Zig executable")
args = parser.parse_args()
if not __debug__:
    raise SystemExit("Run without Python -O")
with tempfile.TemporaryDirectory(prefix="meos-gfx-") as tmp:
    exe = Path(tmp) / "gfx.exe"
    sources = ["drivers/fpga/fpga_link.c", "drivers/fpga/fpga_gpio.c",
               "drivers/fpga/fpga_mock.c", "drivers/fpga/fpga_selftest.c",
               "drivers/fpga/fpga_gfx.c", "drivers/fpga/fpga_gfx_mock.c",
               "user/fpga/fpga_app.c", "user/fpga/fpga_gfx_app.c", "tests/fpga_gpio_fixture.c"]
    subprocess.run([args.cc, "cc", "-std=c11", "-O2", "-UNDEBUG",
                    "-DM5OS_MODULE_FPGA=1", "-DM5OS_FPGA_DOCK_VERIFIED=1", "-DM5OS_MODULE_FPGA_GFX=1", "-DM5OS_BOARD_TAB5=1",
                    "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "kernel/include"),
                    *[str(ROOT / s) for s in sources], "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    verify = Path(tmp) / "verify.exe"
    subprocess.run([args.cc, "cc", "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "kernel/include"),
                    *[str(ROOT / s) for s in ["drivers/fpga/fpga_link.c", "drivers/fpga/fpga_mock.c",
                        "drivers/fpga/fpga_gfx.c", "drivers/fpga/fpga_gfx_mock.c", "tests/fpga_gfx_verify_fixture.c"]],
                    "-o", str(verify)], check=True)
    subprocess.run([str(verify)], check=True)
    vectors = json.loads((ROOT / "tests/fixtures/fpga_dock_gfx_v0.json").read_text())["vectors"]
    responses = subprocess.check_output([str(exe), "--vectors"], text=True,
                                        input="".join(v["request"] + "\n" for v in vectors)).splitlines()
    assert responses == [v["response"] for v in vectors]
    print(f"Carrier oracle: {len(vectors)} stateful vectors match exactly")
    for backend in [["--mock"], ["--dock", "dock-jumper-v0"]]:
        for command in [["info"], ["color", "0xffff"], ["clear", "0xf800"], ["test"], ["verify"], ["demo"]]:
            run = subprocess.run([str(exe), "fpga", *backend, "gfx", *command],
                                 check=True, capture_output=True, text=True)
            report = json.loads(run.stdout)
            assert report["result"] == "ok"
            assert report["lcd_submitted"] == (command[0] == "demo")
            if command[0] in ("test", "demo"):
                assert report["pixels_compared"] == 1024 and report["transactions"] == 1040
            if command[0] == "verify":
                assert report["pixels_compared"] == 22528 and report["transactions"] == 22586
                assert report["scenes_passed"] == 22 and report["readback_pass"]
                assert report["elapsed_us"] >= 0
        # Each CLI process resets the model; read/draw before CLEAR must fail.
        for command in [["read", "0", "0"], ["pixel", "1", "2", "0x1234"],
                        ["rect", "1", "2", "3", "4", "0xf800"]]:
            run = subprocess.run([str(exe), "fpga", *backend, "gfx", *command], capture_output=True, text=True)
            assert run.returncode and json.loads(run.stdout)["result"] == "uninitialized"
    for fault, result in [("--bad-read", "data"), ("--unsupported", "unsupported"), ("--fail-lcd", "io"), ("--no-lcd", "io"),
                          ("--cancel-gfx", "cancelled"), ("--no-reply", "timeout"), ("--bad-crc", "crc")]:
        run = subprocess.run([str(exe), fault, "fpga", "--dock", "dock-jumper-v0", "gfx", "demo"],
                             capture_output=True, text=True)
        assert run.stdout, (fault, run.returncode, run.stderr)
        report = json.loads(run.stdout)
        assert run.returncode and report["result"] == result and not report["lcd_submitted"]
        if fault in ("--fail-lcd", "--no-lcd"):
            assert report["readback_pass"] and not report["reset_required"]
        else:
            assert not report["readback_pass"] and report["reset_required"]
    single = subprocess.run([str(exe), "--single-buffer", "fpga", "--dock", "dock-jumper-v0", "gfx", "demo"], check=True, capture_output=True, text=True)
    assert json.loads(single.stdout)["lcd_submitted"]
    assert subprocess.run([str(exe), "--fail-alloc", "fpga", "--mock", "gfx", "demo"],
                          capture_output=True).returncode
    for fault, result in [("--bad-read", "data"), ("--unsupported", "unsupported"),
                          ("--cancel-gfx", "cancelled"), ("--no-reply", "timeout"), ("--bad-crc", "crc")]:
        run = subprocess.run([str(exe), fault, "fpga", "--dock", "dock-jumper-v0", "gfx", "verify"],
                             capture_output=True, text=True)
        report = json.loads(run.stdout)
        assert run.returncode and report["result"] == result and not report["lcd_submitted"]
        assert not report["readback_pass"] and report["scenes_passed"] == 0
    assert subprocess.run([str(exe), "--fail-alloc", "fpga", "--mock", "gfx", "verify"],
                          capture_output=True).returncode
    for command in [["pixel", "32", "0", "1"], ["rect", "31", "0", "2", "1", "1"],
                    ["clear", "65536"], ["clear", "-1"], ["clear", "0x"],
                    ["read", "0"], ["info", "extra"], []]:
        assert subprocess.run([str(exe), "fpga", "--dock", "dock-jumper-v0", "gfx", *command],
                              capture_output=True).returncode
    print("GFX CLI: mock/GPIO, readback+LCD, invalid input, device/data/allocation/display failures passed")
