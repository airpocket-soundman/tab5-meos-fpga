"""Record comparable ESP-IDF image/static-RAM measurements. Run with IDF Python.

Runtime heap values are optional, truncated KiB readings from an idle-shell
`mem` capture. They are never added to static RAM: the heap arena is already
included in the ELF image's static RAM reservation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

FEATURES = ('M5OS_USB_BACKEND', 'M5OS_BLUETOOTH', 'M5OS_MICROPYTHON', 'M5OS_SL')
CONFIG_KEYS = ('CONFIG_IDF_TARGET', 'CONFIG_COMPILER_OPTIMIZATION_SIZE',
               'CONFIG_COMPILER_OPTIMIZATION_DEBUG', 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ',
               'CONFIG_CACHE_L2_CACHE_SIZE', 'CONFIG_SPIRAM_SPEED_200M')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def heap_readings(text):
    result = {}
    positions = {}
    for name in ('internal', 'psram'):
        pattern = (r'\b' + name + r'\s+total\s+(\d+)\s+KB\s+used\s+(\d+)\s+KB'
                   r'\s+free\s+(\d+)\s+KB\s+largest\s+(\d+)\s+KB'
                   r'\s+peak used\s+(\d+)\s+KB\s+fails\s+(\d+)')
        matches = list(re.finditer(pattern, text))
        if matches:
            positions[name] = (matches[-1].start(), matches[-1].end())
            result[name] = dict(zip(('total', 'used', 'free', 'largest', 'peak', 'fails'),
                                    map(int, matches[-1].groups())))
    if set(result) != {'internal', 'psram'}:
        raise ValueError('runtime capture must contain both complete mem heap lines')
    if positions['psram'][0] < positions['internal'][1] or text[positions['internal'][1]:positions['psram'][0]].strip():
        raise ValueError('legacy heap lines must be an adjacent, complete internal/PSRAM pair')
    return result

def exact_heap_readings(text):
    """Read only complete, schema-checked mem --json records, not other logs."""
    records = []
    for line in text.splitlines():
        if not line.startswith('{'):
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if type(value.get('schema')) is not int or value.get('schema') != 1 or value.get('units') != 'bytes':
            continue
        for name in ('internal', 'psram'):
            fields = value.get(name, {})
            if not isinstance(fields, dict):
                raise ValueError('invalid exact heap object')
            for key in ('total', 'used', 'free', 'largest', 'peak', 'alloc_count', 'fail_count'):
                if type(fields.get(key)) is not int or fields[key] < 0:
                    raise ValueError('invalid exact heap counter')
            if fields['used'] + fields['free'] != fields['total'] or fields['largest'] > fields['free']:
                raise ValueError('inconsistent exact heap counters')
        records.append({name: value[name] for name in ('internal', 'psram')})
    return records[-1] if records else None

def snapshot(args):
    from elftools.elf.elffile import ELFFile
    build = args.build_dir.resolve()
    description = json.loads((build / 'project_description.json').read_text(encoding='utf-8'))
    elf, binary, linkmap = (build / ('m5os.' + suffix) for suffix in ('elf', 'bin', 'map'))
    config = Path(description['config_file'])
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    config_text = config.read_text(encoding='utf-8')
    version_file = Path(description['idf_path']) / 'version.txt'
    compiler_match = re.search(r'^CMAKE_C_COMPILER:[^=]+=(.*)$', cache, re.M)
    compiler = description.get('c_compiler') or (compiler_match[1] if compiler_match else None)
    compiler_version = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0] if compiler else None
    size = json.loads(subprocess.check_output(
        [sys.executable, '-m', 'esp_idf_size', '--format', 'json', str(linkmap)]))
    static = sum(size.get(key, 0) for key in ('used_dram', 'used_iram', 'used_diram'))
    with elf.open('rb') as stream:
        parsed = ELFFile(stream)
        symbols = parsed.get_section_by_name('.symtab')
        arena_symbols = symbols.get_symbol_by_name('s_internal_arena') if symbols else None
        if not arena_symbols or len(arena_symbols) != 1:
            raise ValueError('cannot identify exactly one MeOS internal heap arena')
        arena = int(arena_symbols[0]['st_size'])
    if static < arena:
        raise ValueError('static RAM smaller than heap arena; incompatible size tool/layout')
    flags = {}
    for key in FEATURES:
        match = re.search(r'^' + key + r':[^=]+=(.*)$', cache, re.M)
        flags[key] = match[1] if match else None
    selected = {}
    for key in CONFIG_KEYS:
        match = re.search(r'^' + key + r'=(.*)$', config_text, re.M)
        selected[key] = match[1] if match else None
    result = {
        'schema': 1, 'label': args.label, 'revision_declared': args.revision,
        'artifact_project_version': description.get('project_version'),
        'target': description.get('target'),
        'idf_version': description.get('git_revision') or (version_file.read_text().strip() if version_file.exists() else None),
        'compiler_version': compiler_version,
        'sdkconfig_sha256': digest(config), 'selected_sdkconfig': selected,
        'cmake_features': flags, 'elf_sha256': digest(elf), 'bin_sha256': digest(binary),
        'flash_bin_bytes': binary.stat().st_size,
        'static_hp_sram_including_arena_bytes': static,
        'internal_heap_arena_bytes': arena,
        'static_hp_sram_excluding_arena_bytes': static - arena,
        'linker_hp_sram_capacity_bytes': sum(size.get(key, 0) for key in
                                           ('dram_total', 'iram_total', 'diram_total')),
        'size_tool_summary': size,
        'notes': ['Declared revision is a label, not proof of build/source identity.',
                  'BIN size includes image overhead; IDF total_size is a different metric.',
                  'Static RAM includes the entire internal heap arena, used or free.',
                  'PSRAM allocations and task stacks need workload-specific runtime measurements.'],
    }
    if args.runtime_log:
        if not args.workload:
            raise ValueError('--workload is required with --runtime-log')
        result['runtime_workload'] = args.workload
        result['runtime_capture_sha256'] = digest(args.runtime_log)
        runtime = args.runtime_log.read_text(encoding='utf-8')
        exact = exact_heap_readings(runtime)
        if exact is not None:
            result['runtime_heap_bytes'] = exact
        else:
            result['runtime_heap_kib_truncated'] = heap_readings(runtime)
        result['notes'].append('Runtime image/build association is supplied by the operator, not proven by a mem capture.')
    return result

def compare(before, after):
    keys = ('flash_bin_bytes', 'static_hp_sram_including_arena_bytes',
            'internal_heap_arena_bytes', 'static_hp_sram_excluding_arena_bytes')
    deltas = {key: after[key] - before[key] for key in keys}
    flags = {key: [before.get('cmake_features', {}).get(key), after.get('cmake_features', {}).get(key)]
             for key in FEATURES if before.get('cmake_features', {}).get(key) != after.get('cmake_features', {}).get(key)}
    same = all(before.get(k) is not None and before.get(k) == after.get(k)
               for k in ('sdkconfig_sha256', 'target', 'idf_version', 'compiler_version'))
    return {'before': before['label'], 'after': after['label'], 'delta_bytes': deltas,
            'same_recorded_sdk_configuration': same, 'changed_cmake_features': flags,
            'notes': ['Differences between multi-feature checkpoints are cumulative, not per-feature costs.',
                      'Runtime counters are workload-dependent and intentionally not subtracted automatically.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    snap = commands.add_parser('snapshot')
    snap.add_argument('--build-dir', type=Path, required=True)
    snap.add_argument('--label', required=True)
    snap.add_argument('--revision', required=True)
    snap.add_argument('--runtime-log', type=Path)
    snap.add_argument('--workload')
    diff = commands.add_parser('compare')
    diff.add_argument('before', type=Path)
    diff.add_argument('after', type=Path)
    diff.add_argument('--max-flash-growth', type=int)
    diff.add_argument('--max-static-ram-growth', type=int)
    for command in (snap, diff):
        command.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.command == 'snapshot':
        result = snapshot(args)
    else:
        result = compare(json.loads(args.before.read_text()), json.loads(args.after.read_text()))
    text = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='utf-8')
    else:
        print(text, end='')
    if args.command == 'compare':
        for limit, key in ((args.max_flash_growth, 'flash_bin_bytes'),
                           (args.max_static_ram_growth, 'static_hp_sram_including_arena_bytes')):
            if limit is not None and result['delta_bytes'][key] > limit:
                return 2
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, ImportError, subprocess.CalledProcessError) as error:
        print('resource-report: ' + str(error), file=sys.stderr)
        sys.exit(1)
