"""Generate pinned MicroPython headers without POSIX shell utilities/network.

Uses the upstream generator scripts unchanged. C preprocessing is performed by
the configured target compiler, not the host compiler. Run by CMake, not a user
configuration interface; settings records are generated from the target itself.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

VERSION = "v1.26.1"
COMMIT = "647c8b96cae7e202c7a020395b7cfe65e5b8ce04"


def write_changed(path, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def run_script(script, *args, output=None, env=None):
    result = subprocess.run([sys.executable, str(script), *map(str, args)],
                            stdout=subprocess.PIPE, check=True, env=env)
    if output:
        write_changed(output, result.stdout)


def gcc_response(arguments):
    # GCC response files accept quoted arguments with backslash escapes.
    return "\n".join('"' + x.replace("\\", "\\\\").replace('"', '\\"') + '"'
                     for x in arguments) + "\n"


def generate(settings, vendor, port, output, jobs=4):
    records = {}
    for line in settings.read_text(encoding="utf-8").splitlines():
        if line:
            key, value = line.split("=", 1)
            if value:
                records.setdefault(key, []).append(value)
    compiler, = records["compiler"]
    sources = records["source"]
    flags = []
    for value in records.get("flags", []):
        flags.extend(shlex.split(value))
    flags += ["-I" + x for x in records.get("include", [])]
    flags += ["-D" + x for x in records.get("define", [])]
    output.mkdir(parents=True, exist_ok=True)
    py = vendor / "py"
    # Do not query the containing MeOS repository as if it were MicroPython.
    env = dict(os.environ, MICROPY_GIT_TAG=VERSION, MICROPY_GIT_HASH=COMMIT)
    run_script(py / "makeversionhdr.py", "--repo-path", vendor,
               output / "mpversion.h", env=env)
    response = output / "micropython_cpp.rsp"
    write_changed(response, gcc_response(flags))

    def preprocess(source):
        return subprocess.check_output([compiler, "@" + str(response),
                                        "-E", "-DNO_QSTR", str(source)])

    # A source per command avoids the Windows command-line length limit.
    # Ordered map keeps concatenation deterministic despite parallel execution.
    with ThreadPoolExecutor(max_workers=max(1, min(jobs, 8))) as pool:
        data = b"\n".join(pool.map(preprocess, sources))
    preprocessed = output / "qstr.i.last"
    write_changed(preprocessed, data)
    collected = {}
    for mode in ("qstr", "module", "root_pointer", "compress"):
        directory = output / ("extract_" + mode)
        directory.mkdir(exist_ok=True)
        # Only remove this generator's individual extraction records, not any
        # directory tree. Removed/disabled sources must not leave stale entries.
        for old in directory.glob("*." + mode):
            old.unlink()
        run_script(py / "makeqstrdefs.py", "split", mode, preprocessed, directory, "_")
        filename = output / (mode + ".collected")
        run_script(py / "makeqstrdefs.py", "cat", mode, "_", directory, filename)
        collected[mode] = filename
    qstr_text = (py / "qstrdefs.h").read_text(encoding="utf-8")
    if (port / "qstrdefsport.h").exists():
        qstr_text += "\n" + (port / "qstrdefsport.h").read_text(encoding="utf-8")
    qstr_text += "\n" + collected["qstr"].read_text(encoding="utf-8")
    # Equivalent to upstream mkrules.cmake's sed protection around Q(...).
    qstr_text = re.sub(r"^Q\(.*\)$", lambda m: '"' + m[0] + '"', qstr_text, flags=re.M)
    quoted = output / "qstrdefs.quoted.h"
    write_changed(quoted, qstr_text)
    qstr_pp = subprocess.check_output([compiler, "@" + str(response), "-E", "-x", "c", str(quoted)])
    qstr_pp = qstr_pp.replace(b"\r\n", b"\n")
    qstr_pp = re.sub(rb'^"(Q\(.*\))"$', rb"\1", qstr_pp, flags=re.M)
    qstr_input = output / "qstrdefs.preprocessed.h"
    write_changed(qstr_input, qstr_pp)
    for script, source, target in (
        ("makeqstrdata.py", qstr_input, "qstrdefs.generated.h"),
        ("makemoduledefs.py", collected["module"], "moduledefs.h"),
        ("make_root_pointers.py", collected["root_pointer"], "root_pointers.h"),
        ("makecompresseddata.py", collected["compress"], "compressed.data.h"),
    ):
        run_script(py / script, source, output=output / target)
    print("Generated MicroPython v1.26.1 headers from", len(sources), "target-preprocessed sources")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, required=True)
    parser.add_argument("--vendor", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    generate(args.settings, args.vendor, args.port, args.output, args.jobs)


if __name__ == "__main__":
    main()
