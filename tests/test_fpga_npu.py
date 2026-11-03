"""CPU NPU conformance and honest all-feature coverage reporting; no hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--cc", required=True, help="Zig executable")
args = p.parse_args()
if not __debug__:
    raise SystemExit("Run without Python -O")
metadata = json.loads((ROOT / "tests/fixtures/fpga_npu_source.json").read_text())
assert hashlib.sha256((ROOT / "drivers/fpga/fpga_npu_vectors.h").read_bytes()).hexdigest() == metadata["header_sha256"]


def compile_test(exe, sources, defines=()):
    subprocess.run([args.cc, "cc", "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(ROOT / "kernel/include"), *defines,
                    *[str(ROOT / path) for path in sources], "-o", str(exe)], check=True)


with tempfile.TemporaryDirectory(prefix="meos-npu-") as tmp:
    core = ["drivers/fpga/fpga_npu.c", "drivers/fpga/fpga_npu_selftest.c"]
    exe = Path(tmp) / "npu.exe"
    compile_test(exe, [*core, "tests/fpga_npu_fixture.c"])
    subprocess.run([str(exe)], check=True)
    cli = Path(tmp) / "diag.exe"
    compile_test(cli, [*core, "drivers/fpga/fpga_link.c", "drivers/fpga/fpga_gpio.c",
                      "drivers/fpga/fpga_mock.c", "drivers/fpga/fpga_selftest.c",
                      "drivers/fpga/fpga_gfx.c", "drivers/fpga/fpga_gfx_mock.c",
                      "user/fpga/fpga_app.c", "user/fpga/fpga_gfx_app.c",
                      "user/fpga/fpga_npu_app.c", "user/fpga/fpga_diag_app.c", "tests/fpga_gpio_fixture.c"],
                 ["-DM5OS_MODULE_FPGA=1", "-DM5OS_FPGA_DOCK_VERIFIED=1", "-DM5OS_MODULE_FPGA_GFX=1", "-DM5OS_MODULE_FPGA_NPU=1",
                  "-DM5OS_MODULE_FPGA_DIAG=1", "-DM5OS_BOARD_TAB5=1"])

    def invoke(command, code=0, prefix=()):
        run = subprocess.run([str(cli), *prefix, "fpga", *command], capture_output=True, text=True)
        assert run.returncode == code, (command, run.stdout, run.stderr, run.returncode)
        return json.loads(run.stdout)

    cpu = ["--cpu", "npu"]
    report = invoke([*cpu, "selftest"])
    assert report["result"] == "ok" and not report["physical_npu_tested"]
    assert [report[k] for k in ("requant_cases", "dot_cases", "gemm_cases", "conv_cases", "checks")] == [460, 87, 12, 8, 13]
    for operator, field, count in [("mac", "checks", 13), ("requant", "requant_cases", 460),
                                    ("dot", "dot_cases", 87), ("gemm", "gemm_cases", 12), ("conv", "conv_cases", 8)]:
        report = invoke([*cpu, "selftest", operator])
        assert report[field] == count
        assert sum(report[k] for k in ("requant_cases", "dot_cases", "gemm_cases", "conv_cases", "checks")) == count
    assert invoke([*cpu, "requant", "-3", "1", "1", "0", "-128", "127"])["value"] == -2
    assert invoke([*cpu, "dot", "-3", "0", "1", "1", "0", "-128", "127"])["value"] == -2
    assert invoke([*cpu, "dot", "0", "0", "1", "0", "0", "-128", "127", "2", "3"])["value"] == 6
    assert invoke([*cpu, "dot", "2147483647", "0", "1", "0", "0", "-128", "127", "1", "1"], 1)["result"] == "acc-overflow"
    blocked = invoke(["--dock", "dock-jumper-v0", "npu", "test"], 2)
    assert not blocked["gpio_activated"] and blocked["result"] == "blocked"
    features = invoke(["features"])
    assert features["scope"] == "host-implementation-inventory-not-device-probe"
    assert not features["full_hardware_coverage"]
    for backend, code, expected in [(["--mock"], 0, "ok-software"), (["--dock", "dock-jumper-v0"], 2, "partial")]:
        report = invoke([*backend, "suite"], code)
        assert report["result"] == expected and report["link_exchanges"] == 513
        assert report["gfx_pixels_compared"] == 1024 and report["npu_cases"] == 567
        assert report["npu_backend"] == "cpu-reference" and not report["full_hardware_coverage"]
        assert "physical-npu" in report["blocked"] and "ramdisk" in report["blocked"]
    report = invoke(["--dock", "dock-jumper-v0", "suite"], 1, ["--no-reply"])
    assert not report["npu_ran"] and report["link_result"] == "timeout"
    for command in [["requant", "2147483648", "1", "0", "0", "-128", "127"],
                    ["requant", "-2147483649", "1", "0", "0", "-128", "127"],
                    ["requant", "1", "1", "63", "0", "-128", "127"],
                    ["requant", "1", "1", "0", "128", "-128", "127"],
                    ["dot", "0", "0", "1", "0", "0", "-128", "127", "128", "1"],
                    ["selftest", "unknown"]]:
        run = subprocess.run([str(cli), "fpga", *cpu, *command], capture_output=True)
        assert run.returncode
    print("NPU/diagnostics CLI: per-operator tests, numeric bounds, blocked physical NPU without GPIO, LINK/GFX suite coverage/failure reporting passed")
