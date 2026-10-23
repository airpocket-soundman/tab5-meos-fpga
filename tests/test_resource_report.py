import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('resource_report', ROOT / 'tools/resource_report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)

def reading(label, flash, static, arena=196608):
    return {'label': label, 'flash_bin_bytes': flash,
            'static_hp_sram_including_arena_bytes': static,
            'internal_heap_arena_bytes': arena,
            'static_hp_sram_excluding_arena_bytes': static - arena,
            'sdkconfig_sha256': 'same', 'target': 'esp32p4',
            'idf_version': 'v5.4.4', 'compiler_version': 'same', 'cmake_features': {}}

class ResourceReportTest(unittest.TestCase):
    def test_exact_record_rejects_corruption(self):
        fields={'total':192,'used':109,'free':83,'largest':78,'peak':125,'alloc_count':5,'fail_count':0}
        record={'schema':1,'units':'bytes','internal':fields,'psram':dict(fields)}
        self.assertEqual(report.exact_heap_readings('unrelated log\n'+json.dumps(record))['internal']['used'],109)
        record['internal']['free']=82
        with self.assertRaises(ValueError):report.exact_heap_readings(json.dumps(record))
        self.assertIsNone(report.exact_heap_readings('{truncated\n'))

    def test_heap_rounding_and_latest_capture(self):
        line = ('internal total 192 KB used 109 KB free 82 KB largest 78 KB peak used 125 KB fails 0\n'
                'psram total 28672 KB used 9031 KB free 19640 KB largest 19623 KB peak used 9114 KB fails 0\n')
        values = report.heap_readings(line)
        self.assertEqual(values['internal']['used'], 109)
        self.assertEqual(values['psram']['free'], 19640)
        with self.assertRaises(ValueError):
            report.heap_readings(line.splitlines()[0])
        with self.assertRaises(ValueError):
            report.heap_readings(line + line.splitlines()[0])

    def test_static_arena_not_double_counted(self):
        before, after = reading('before', 1000000, 440000), reading('after', 1100000, 460000)
        result = report.compare(before, after)
        self.assertEqual(result['delta_bytes']['static_hp_sram_including_arena_bytes'], 20000)
        self.assertEqual(result['delta_bytes']['static_hp_sram_excluding_arena_bytes'], 20000)
        self.assertEqual(result['delta_bytes']['internal_heap_arena_bytes'], 0)
        self.assertTrue(result['same_recorded_sdk_configuration'])

    def test_configuration_and_feature_differences(self):
        before, after = reading('before', 1, 300000), reading('after', 2, 300100)
        after['compiler_version'] = 'different'
        after['cmake_features']['M5OS_MICROPYTHON'] = 'ON'
        result = report.compare(before, after)
        self.assertFalse(result['same_recorded_sdk_configuration'])
        self.assertEqual(result['changed_cmake_features']['M5OS_MICROPYTHON'], [None, 'ON'])

    def test_budget_boundary_and_json_output(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            for name, value in [('before', reading('before', 100, 300000)),
                                ('after', reading('after', 200, 300010))]:
                (folder / (name + '.json')).write_text(json.dumps(value))
            command = [sys.executable, str(ROOT / 'tools/resource_report.py'), 'compare',
                       str(folder / 'before.json'), str(folder / 'after.json'),
                       '--output', str(folder / 'result.json')]
            self.assertEqual(subprocess.run(command + ['--max-flash-growth', '100']).returncode, 0)
            self.assertEqual(subprocess.run(command + ['--max-flash-growth', '99']).returncode, 2)
            self.assertEqual(subprocess.run(command + ['--max-static-ram-growth', '9']).returncode, 2)
            self.assertEqual(json.loads((folder / 'result.json').read_text())['delta_bytes']['flash_bin_bytes'], 100)

if __name__ == '__main__':
    unittest.main()
