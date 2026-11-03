#!/usr/bin/env python3
"""List the MeOS feature modules and resolve a selection, without running a build.

The registry is cmake/modules.cmake (m5os_module(...) calls); this reads it, so there is one source of truth.

  python tools/m5os_modules.py list [--board tab5]
  python tools/m5os_modules.py resolve mp_camera,uvc          # what a build of exactly these would contain
  python tools/m5os_modules.py resolve all
  python tools/m5os_modules.py cmake mp_camera                # the idf.py argument: -DM5OS_MODULES=...
  python tools/m5os_modules.py profiles                       # the published firmware sizes and what each contains
  python tools/m5os_modules.py json                           # machine readable (modules and profiles), for the flasher page / CI

resolve follows the same rules as m5os_resolve_modules(): the listed modules plus what they need; for DEPENDS_ANY
the first available alternative is taken when none was chosen.
"""
import argparse
import json
import re
import sys
from pathlib import Path

REGISTRY = Path(__file__).resolve().parents[1] / "cmake" / "modules.cmake"
ALL_BOARDS = ["tab5", "cores3", "cardputer"]


def load_registry():
    text = REGISTRY.read_text(encoding="utf-8")
    modules = {}
    for call in re.finditer(r"^m5os_module\((.*?)\)\s*$", text, re.S | re.M):
        body = call.group(1)
        tokens = re.findall(r'"[^"]*"|\S+', body)
        name, rest = tokens[0], tokens[1:]
        keys = {"DESCRIPTION", "DEFAULT", "OPTION", "BOARDS", "DEPENDS", "DEPENDS_ANY"}
        fields, current = {}, None
        for tok in rest:
            if tok in keys:
                current = tok
                fields[current] = []
            elif current:
                fields[current].append(tok.strip('"'))
        modules[name] = {
            "name": name,
            "description": " ".join(fields.get("DESCRIPTION", [])),
            "boards": fields.get("BOARDS") or ALL_BOARDS,
            "default": (fields.get("DEFAULT") or ["ON"])[0].upper() in ("ON", "TRUE", "1"),
            "option": (fields.get("OPTION") or [""])[0],
            "depends": fields.get("DEPENDS", []),
            "depends_any": fields.get("DEPENDS_ANY", []),
        }
    return modules


def load_profiles():
    text = REGISTRY.read_text(encoding="utf-8")
    profiles = {}
    for call in re.finditer(r"^m5os_profile\((.*?)\)\s*$", text, re.S | re.M):
        tokens = re.findall(r'"[^"]*"|\S+', call.group(1))
        name, rest = tokens[0], tokens[1:]
        fields, current = {}, None
        for tok in rest:
            if tok in ("DESCRIPTION", "MODULES"):
                current = tok
                fields[current] = []
            elif current:
                fields[current].append(tok.strip('"'))
        profiles[name] = {"name": name, "description": " ".join(fields.get("DESCRIPTION", [])),
                          "modules": fields.get("MODULES", [])}
    return profiles


def profile_modules(modules, profiles, name, board="tab5"):
    """The modules a profile builds, dependencies included, in dependency order."""
    if name not in profiles:
        raise SystemExit(f"unknown profile '{name}' (known: {', '.join(profiles)})")
    requested = profiles[name]["modules"]
    on, _ = resolve(modules, requested, board) if requested else ([], {})
    return dependency_order(modules, on)


def resolve(modules, requested, board):
    available = [m for m, d in modules.items() if board in d["boards"]]
    if requested == ["all"]:
        requested = list(available)
    on, why = [], {}
    for m in requested:
        if m not in modules:
            raise SystemExit(f"unknown module '{m}' (known: {', '.join(modules)})")
        if m not in available:
            raise SystemExit(f"module '{m}' is not available for board '{board}'")
        on.append(m)
        why[m] = "selected"
    changed = True
    while changed:
        changed = False
        for m in list(on):
            for d in modules[m]["depends"]:
                if d not in on:
                    if d not in available:
                        raise SystemExit(f"'{m}' needs '{d}', which is not available for board '{board}'")
                    on.append(d)
                    why[d] = f"needed by {m}"
                    changed = True
            alts = modules[m]["depends_any"]
            if alts and not any(a in on for a in alts):
                pick = next((a for a in alts if a in available), None)
                if pick is None:
                    raise SystemExit(f"'{m}' needs one of: {', '.join(alts)}")
                on.append(pick)
                why[pick] = f"needed by {m}"
                changed = True
    return on, why


def dependency_order(modules, names):
    order, seen = [], set()

    def visit(m):
        if m in seen:
            return
        seen.add(m)
        for d in modules[m]["depends"]:
            visit(d)
        for a in modules[m]["depends_any"]:
            if a in names:
                visit(a)
        order.append(m)

    for m in names:
        visit(m)
    return order


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["list", "resolve", "cmake", "json", "profiles"])
    ap.add_argument("modules", nargs="?", default="")
    ap.add_argument("--board", default="tab5", choices=ALL_BOARDS)
    args = ap.parse_args()
    modules = load_registry()

    profiles = load_profiles()
    if args.command == "json":
        json.dump({"modules": list(modules.values()),
                   "profiles": [dict(p, resolved=profile_modules(modules, profiles, n, args.board))
                                for n, p in profiles.items()]}, sys.stdout, indent=2)
        print()
        return
    if args.command == "profiles":
        for n, p in profiles.items():
            print(f"{n:7} {p['description']}")
            print(f"{'':7} modules: {', '.join(profile_modules(modules, profiles, n, args.board)) or '(none)'}")
        return
    if args.command == "list":
        for m, d in modules.items():
            avail = "" if args.board in d["boards"] else "  [not for this board]"
            needs = ", ".join(d["depends"] + (["one of " + "/".join(d["depends_any"])] if d["depends_any"] else []))
            print(f"{m:12} {'on ' if d['default'] else 'off'} by default  {d['description']}{avail}")
            if needs:
                print(f"{'':12} needs: {needs}")
        return
    requested = [x for x in args.modules.replace(",", ";").split(";") if x]
    if not requested:
        raise SystemExit("name at least one module (or 'all')")
    on, why = resolve(modules, requested, args.board)
    if args.command == "cmake":
        print(f"-DM5OS_MODULES={','.join(on)}")
        return
    for m in dependency_order(modules, on):
        print(f"[on ] {m}  ({why[m]})")
    for m in modules:
        if m not in on and args.board in modules[m]["boards"]:
            print(f"[off] {m}")


if __name__ == "__main__":
    main()
