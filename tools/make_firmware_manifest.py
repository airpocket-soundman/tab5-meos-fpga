#!/usr/bin/env python3
"""Collect the published firmware sizes into docs/site/firmware and write manifest.json for the flasher page.

  python tools/make_firmware_manifest.py small=build-small medium=build-medium large=build-large [--out docs/site/firmware]
                                         [--commit SHA]

Each argument is PROFILE=BUILD_DIR, a finished ESP-IDF build directory. The flash offsets are read from the build's
flash_args, so they always match what `idf.py flash` would write.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import m5os_modules  # noqa: E402


def flash_files(build: Path):
    """[(offset, relative path)] from flash_args, e.g. 0x2000 bootloader/bootloader.bin."""
    text = (build / "flash_args").read_text(encoding="utf-8")
    files = [(int(a, 16), b) for a, b in re.findall(r"^(0x[0-9a-fA-F]+)\s+(\S+)\s*$", text, re.M)]
    if not files:
        raise SystemExit(f"{build}: no flash entries in flash_args")
    return files


def sha256(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_commit():
    try:
        return subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("builds", nargs="+", help="PROFILE=BUILD_DIR")
    ap.add_argument("--out", default=str(ROOT / "docs/site/firmware"))
    ap.add_argument("--commit", default="")
    args = ap.parse_args()

    modules = m5os_modules.load_registry()
    profiles = m5os_modules.load_profiles()
    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    manifest = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": args.commit or git_commit(),
        "chip": "esp32p4",
        "flash_size": "16MB",
        "profiles": [],
    }
    for item in args.builds:
        name, _, build_dir = item.partition("=")
        if name not in profiles:
            raise SystemExit(f"unknown profile '{name}' (known: {', '.join(profiles)})")
        build = Path(build_dir)
        entries = []
        dest = out / name
        dest.mkdir()
        for offset, rel in flash_files(build):
            src = build / rel
            if not src.is_file():
                raise SystemExit(f"{src} is missing")
            target = dest / src.name
            shutil.copyfile(src, target)
            entries.append({"path": f"{name}/{src.name}", "offset": offset, "size": target.stat().st_size,
                            "sha256": sha256(target)})
        manifest["profiles"].append({
            "name": name,
            "description": profiles[name]["description"],
            "modules": m5os_modules.profile_modules(modules, profiles, name),
            "bytes": sum(e["size"] for e in entries),
            "app_bytes": next(e["size"] for e in entries if e["path"].endswith("m5os.bin")),
            "files": entries,
        })
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    for p in manifest["profiles"]:
        print(f"{p['name']:7} app {p['app_bytes']:>9,} B  modules: {', '.join(p['modules']) or '(none)'}")
    print(f"wrote {out / 'manifest.json'}")


if __name__ == "__main__":
    main()
