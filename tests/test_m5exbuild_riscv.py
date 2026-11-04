"""Real ESP32-P4 toolchain relocation equivalence; no target execution."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("m5exbuild", ROOT / "tools/m5exbuild.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

SOURCE = r'''
#include "m5ex_sdk.h"
M5EX_APP("reloccheck", M5_PERM_CONSOLE, 4096);
static char storage[192];
static const char text[] = "external application";
static void sub(const m5_api_t *api) { storage[0] = (char)api->task_id(); }
static void (*volatile function_pointer)(const m5_api_t *) = sub;
static char *volatile bss_pointer = storage;
static const char *volatile text_pointer = text;
void app_main(const m5_api_t *api, void *arg) {
    (void)arg; function_pointer(api);
    api->write(1, text_pointer, 20);
    api->write(1, bss_pointer, 1);
}
'''

def image(elf):
    result = {}
    for section in elf.iter_sections():
        if section['sh_flags'] & 2 and section['sh_type'] == 'SHT_PROGBITS':
            result[section.name] = section.data()
    return result

def reject(fn, text):
    try:
        fn()
    except SystemExit as error:
        assert text in str(error), str(error)
    else:
        raise AssertionError('invalid input was accepted')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='meos-riscv-m5ex-') as temporary:
        folder = Path(temporary)
        hello, hello_info = builder.build_app(str(ROOT / 'apps/m5ex/m5hello.c'), 'riscv', args.cc)
        assert struct.unpack_from('<I', hello, 8)[0] == 1
        assert hello[:4] == b'M5EX' and zlib.crc32(hello[64:]) == struct.unpack_from('<I', hello, 44)[0]
        source = folder / 'reloccheck.c'
        source.write_text(SOURCE)
        elf_path = builder.build(str(source), str(folder), 'riscv', args.cc)
        blob, info = builder.pack(elf_path, 'riscv')
        assert info['relocs'] == 3 and info['mem'] > info['image']
        reject(lambda: builder.pack(elf_path, 'xtensa'), 'architecture')
        # Link the exact same compiled objects at a real internal-RAM-like
        # address. The runtime relocation result must match the static linker,
        # including every instruction/rodata/data byte, not just pointer values.
        base = 0x4ff00000
        script = folder / 'relocated.ld'
        script.write_text((ROOT / 'apps/sdk/m5ex_riscv.ld').read_text().replace('. = 0;', '. = 0x%x;' % base))
        static_elf = folder / 'relocated.elf'
        subprocess.run([args.cc, '-mno-relax', '-march=rv32imafc_zicsr_zifencei', '-mabi=ilp32f',
                        '-nostdlib', '-nostartfiles', '-Wl,-T,' + str(script), '-Wl,--gc-sections',
                        '-o', str(static_elf), str(folder / 'reloccheck.c.o'), str(folder / 'm5ex_rt.c.o'), '-lgcc'], check=True)
        relocated = bytearray(blob[64:64 + info['image']])
        offsets = struct.unpack_from('<%dI' % info['relocs'], blob, 64 + info['image'])
        for offset in offsets:
            value = struct.unpack_from('<I', relocated, offset)[0]
            assert value < info['mem']
            struct.pack_into('<I', relocated, offset, value + base)
        with open(static_elf, 'rb') as handle:
            elf = ELFFile(handle)
            for section in elf.iter_sections():
                if section['sh_flags'] & 2 and section['sh_type'] == 'SHT_PROGBITS':
                    offset = section['sh_addr'] - base
                    assert relocated[offset:offset + section['sh_size']] == section.data(), section.name
        # Non-PIC absolute instruction fixups are outside M5EX's word-only ABI.
        bad_source = folder / 'absolute.c'
        bad_source.write_text(SOURCE.replace('(void)arg; function_pointer(api);',
            '(void)arg; __asm__ volatile ("lui a5, %%hi(storage)" ::: "a5"); function_pointer(api);'))
        bad_elf = builder.build(str(bad_source), str(folder), 'riscv', args.cc)
        reject(lambda: builder.pack(bad_elf, 'riscv'), 'unsupported RISC-V relocation')
        # Existing callers still select Xtensa and its original compiler flags.
        captured = []
        original_run = builder.run
        builder.run = captured.append
        try:
            builder.build(str(source), str(folder))
        finally:
            builder.run = original_run
        assert all(command[0] == 'xtensa-esp32s3-elf-gcc' for command in captured)
        assert '-mtext-section-literals' in captured[0] and '-fpie' not in captured[0]
    result = {'result': 'pass', 'architecture': 'riscv32-ilp32f', 'hello': hello_info,
              'pointer_image': info, 'static_link_base': base, 'relocations': 3,
              'all_allocated_bytes_match_static_link': True, 'absolute_instruction_rejected': True,
              'xtensa_default_preserved': True, 'hardware_execution': False}
    print(json.dumps(result))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
