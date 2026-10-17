#!/usr/bin/env python3
"""Build M5EX applications (see kernel/include/m5os/m5ex.h for the format).

  tools/m5exbuild.py apps/m5ex/m5hello.c -o /tmp/m5hello.m5ex
  tools/m5exbuild.py --embed apps/m5ex/images.c apps/m5ex/*.c      # also emits a C table for the firmware

Needs the ESP-IDF environment (. tools/env.sh): xtensa-esp32s3-elf-gcc and pyelftools.
"""
import argparse, os, struct, subprocess, sys, tempfile, zlib
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC = "xtensa-esp32s3-elf-gcc"
INC = [os.path.join(ROOT, "kernel", "include")]
SDK = os.path.join(ROOT, "apps", "sdk")
CFLAGS = ["-Os", "-g0", "-ffreestanding", "-fno-jump-tables", "-fno-tree-switch-conversion",
          "-mtext-section-literals", "-ffunction-sections", "-fdata-sections", "-fno-common",
          "-fno-stack-protector", "-fno-tree-loop-distribute-patterns", "-Wall", "-Werror"]
R_XTENSA_32 = 1
MAGIC, VERSION, HEADER_SIZE = b"M5EX", 1, 64
HEADER_FMT = "<4sHHIIIIIIIIII16s"
MANIFEST_FMT = "<16sIII"            # name, permissions, stack_size, api_version


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("command failed: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))


def build(source, workdir):
    objs = []
    for src in (source, os.path.join(SDK, "m5ex_rt.c")):
        obj = os.path.join(workdir, os.path.basename(src) + ".o")
        run([CC] + CFLAGS + ["-I" + i for i in INC] + ["-I" + SDK, "-c", src, "-o", obj])
        objs.append(obj)
    elf = os.path.join(workdir, "app.elf")
    # Unresolved symbols are link errors on purpose: an app may not reference kernel symbols.
    run([CC, "-nostdlib", "-nostartfiles", "-Wl,-T," + os.path.join(SDK, "m5ex.ld"),
         "-Wl,--emit-relocs", "-Wl,--gc-sections", "-o", elf] + objs + ["-lgcc"])
    return elf


def pack(elf_path):
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        sym = {s.name: s["st_value"] for s in symtab.iter_symbols()}
        for need in ("app_main", "m5ex_manifest", "__m5ex_code_end", "__m5ex_image_end", "__m5ex_mem_end"):
            if need not in sym:
                sys.exit("missing symbol: " + need)
        code_size, image_size, mem_size = sym["__m5ex_code_end"], sym["__m5ex_image_end"], sym["__m5ex_mem_end"]

        image = bytearray(image_size)
        for sec in elf.iter_sections():
            if sec["sh_flags"] & 2 and sec["sh_type"] == "SHT_PROGBITS" and sec["sh_size"]:   # SHF_ALLOC
                data = sec.data()
                image[sec["sh_addr"]:sec["sh_addr"] + len(data)] = data

        relocs = []
        for sec in elf.iter_sections():
            if not isinstance(sec, RelocationSection):
                continue
            target = elf.get_section(sec["sh_info"])
            if not (target["sh_flags"] & 2):
                continue
            for r in sec.iter_relocations():
                if r["r_info_type"] != R_XTENSA_32:
                    continue
                off = r["r_offset"]
                if off % 4 or off + 4 > image_size:
                    sys.exit("bad relocation offset 0x%x" % off)
                value = struct.unpack_from("<I", image, off)[0]
                if value >= mem_size:
                    sys.exit("relocation at 0x%x points outside the module (0x%x)" % (off, value))
                relocs.append(off)
        relocs = sorted(set(relocs))

        name, perms, stack, api = struct.unpack_from(MANIFEST_FMT, image, sym["m5ex_manifest"])
        reloc_bytes = b"".join(struct.pack("<I", o) for o in relocs)
        crc = zlib.crc32(bytes(image) + reloc_bytes) & 0xFFFFFFFF
        header = struct.pack(HEADER_FMT, MAGIC, VERSION, HEADER_SIZE, 0, api, perms, image_size, mem_size,
                             code_size, sym["app_main"], len(relocs), stack, crc, name)
        info = dict(name=name.split(b"\0")[0].decode(), code=code_size, image=image_size, mem=mem_size,
                    relocs=len(relocs), perms=perms, stack=stack)
        return header + bytes(image) + reloc_bytes, info


def build_app(source):
    with tempfile.TemporaryDirectory() as d:
        return pack(build(source, d))


def c_array(name, data):
    rows = [", ".join("0x%02x" % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return "static const uint8_t img_%s[] = {\n    %s\n};\n" % (name, ",\n    ".join(rows))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="+")
    ap.add_argument("-o", help="output .m5ex (single source)")
    ap.add_argument("--embed", help="write a C file embedding all images")
    a = ap.parse_args()

    images = []
    for src in a.sources:
        blob, info = build_app(src)
        print("%-10s file %5d B  image %4d (code %4d)  mem %5d  relocs %3d  perms 0x%02x  stack %d"
              % (info["name"], len(blob), info["image"], info["code"], info["mem"], info["relocs"], info["perms"], info["stack"]))
        images.append((info["name"], blob))
        if a.o and len(a.sources) == 1:
            open(a.o, "wb").write(blob)
    if a.embed:
        out = ["/* GENERATED by tools/m5exbuild.py --embed.  Do not edit; rebuild with tools/build_m5ex.sh. */",
               '#include <m5os/m5ex.h>', ""]
        for n, b in images:
            out.append(c_array(n, b))
        out.append("static const m5ex_image_t s_images[] = {")
        for n, b in images:
            out.append('    { "%s", img_%s, %d },' % (n, n, len(b)))
        out += ["};", "", "const m5ex_image_t *m5ex_embedded(int *count)",
                "{", "    *count = (int)(sizeof s_images / sizeof s_images[0]);", "    return s_images;", "}", ""]
        open(a.embed, "w").write("\n".join(out))


if __name__ == "__main__":
    main()
