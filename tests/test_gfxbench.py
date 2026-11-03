"""Native GPU LAB renderer and real MeOS program against host HAL/display stubs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True, help="Zig executable")
parser.add_argument("--reference", type=Path, help="Optional original tab5-fpga-gpu checkout, read only")
args = parser.parse_args()
if not __debug__:
    raise SystemExit("Run without Python -O")


def run(command, **kwargs):
    p = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if p.returncode:
        raise AssertionError((command, p.returncode, p.stdout[-3000:], p.stderr[-3000:]))
    return p.stdout


with tempfile.TemporaryDirectory(prefix="meos-gfxbench-") as tmp:
    tmp = Path(tmp)
    common = [args.cc, "c++", "-std=c++17", "-O3", "-UNDEBUG", "-fno-exceptions",
              "-fno-rtti", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror"]
    native = ROOT / "third_party/gpu_lab"
    renderer = tmp / "renderer.exe"
    run([*common, "-DGFXBENCH_NATIVE", "-I" + str(native / "main"), str(native / "main/lab.cpp"),
         str(ROOT / "tests/gfxbench_renderer_fixture.cpp"), "-o", str(renderer)])
    records = run([str(renderer)])
    assert len(records.splitlines()) == 60
    if args.reference:
        provenance = json.loads((native / "provenance.json").read_text())
        for name, digests in provenance["files"].items():
            source = subprocess.check_output(["git", "-C", str(args.reference), "show", f"{provenance['commit']}:{name}"])
            assert hashlib.sha256(source).hexdigest() == digests["source_sha256"]
            # Compile the checkout only after confirming its source matches the pinned commit.
            current = (args.reference / name).read_bytes().replace(b"\r\n", b"\n")
            assert current == source.replace(b"\r\n", b"\n")
        reference = tmp / "reference.exe"
        run([*common, "-I" + str(args.reference / "main"), str(args.reference / "main/lab.cpp"),
             str(ROOT / "tests/gfxbench_renderer_fixture.cpp"), "-o", str(reference)])
        assert run([str(reference)]) == records
        print("Renderer: 60 color/depth/work records exactly match the pinned ESP-IDF source")
    else:
        print("Renderer: 60 cases, guards, determinism, statistics and allocation failures passed")

    app = tmp / "gfxbench.exe"
    run([*common, "-DM5OS_BOARD_TAB5=1", "-I" + str(ROOT / "kernel/include"),
         str(native / "main/lab.cpp"), str(ROOT / "user/gfxbench/gfxbench_app.cpp"),
         str(ROOT / "tests/gfxbench_app_fixture.cpp"), "-o", str(app)])

    def invoke(options, prefix=(), expected=0):
        p = subprocess.run([str(app), *prefix, "gfxbench", *options], capture_output=True, text=True)
        assert p.returncode == expected, (options, prefix, p.returncode, p.stdout, p.stderr)
        return p.stdout

    invoke(["list"])
    for scene in ["wire", "polygon", "mandelbrot", "julia", "particles", "galaxy", "fire", "flow"]:
        report = json.loads(invoke([scene, "--frames", "2", "--headless"]))
        assert report["frames_rendered"] == 2 and report["backend"] == "CPU"
    invoke(["polygon", "integer", "--frames", "2"])
    invoke(["polygon", "--frames", "1"], ["--test-paused"])
    for raster, cases in [("float", 24), ("integer", 6)]:
        output = invoke(["suite", raster, "--headless"])
        rows = [line for line in output.splitlines() if line.startswith("GFXBENCH,")]
        assert len(rows) == cases and all(row.split(",")[8] == "120" for row in rows)
        report = json.loads(output.splitlines()[-1])
        assert report["benchmark_cases"] == cases and report["frames_rendered"] == cases * 144
    output = invoke(["polygon", "--bench", "--headless"])
    assert json.loads(output.splitlines()[-1])["benchmark_cases"] == 1
    output = invoke([], ["--test-controls"])
    assert json.loads(output.splitlines()[-1])["benchmark_cases"] == 1
    invoke(["polygon", "--bench"], ["--test-cancel"], 130)
    invoke(["polygon", "--frames", "2"], ["--test-lcd-fail"], 1)
    invoke(["polygon", "--frames", "1"], ["--test-busy"], 1)
    for fault in range(1, 7):
        invoke(["flow", "--frames", "1", "--headless"], [f"--test-fail-{fault}"], 1)
    for options in [["--fpga"], ["polygon", "heavy", "light"], ["fire", "integer"],
                    ["--headless"], ["--frames", "0"], ["--frames", "10001"],
                    ["--frames", "-1"], ["--frames", "1", "--bench"], ["suite", "heavy"]]:
        invoke(options, expected=1)
    print("Program: 8 scenes, float 24-case/integer 6-case suites, controls, cancellation, LCD/memory failures, cleanup passed")
