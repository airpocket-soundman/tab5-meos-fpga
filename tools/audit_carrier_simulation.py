"""Reproduce a frozen carrier channel estimate and exercise a combined corner.
This is an audit harness, not an independent full-wave electromagnetic solver.
The snapshot is an extracted, immutable Git revision of the carrier repository.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.snapshot.resolve()
    sim = root / 'kicad/sim'
    sys.path.insert(0, str(sim))
    import tmds_channel as tc
    import tmds_sparam as sp
    tc.load()
    saved = json.loads((sim / 'tmds_sparam.json').read_text(encoding='utf-8'))
    out = {'revision_declared': args.revision, 'hashes': {}, 'reproduction': {}, 'paths': {}, 'joint_corner': {}}
    for name in ('kicad/tab5-fpga-carrier.kicad_pcb', 'kicad/sim/tmds_roles.json',
                 'kicad/sim/tmds_geom.json', 'kicad/sim/tmds_channel.py', 'kicad/sim/tmds_sparam.py'):
        out['hashes'][name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
    for tag, p, n in tc.PAIRS:
        s11, s21 = sp.abcd_to_s(sp.channel(tag, p, n))
        calc = sp.metrics(s11, s21)
        out['reproduction'][tag] = {'max_abs_error_db': max(abs(v-saved[tag][k]) for k, v in calc.items())}
        for suffix in ('', '_C'):
            for net in (p+suffix, n+suffix):
                ordered, loose = tc.order_segments(tc.segs(None, net))
                out['paths'][net] = {'ordered_segments': len(ordered), 'loose_segments': len(loose),
                                     'start': ordered[0][:2], 'end': ordered[-1][2:4]}
        a, b = sp.abcd_to_s(sp.channel(tag, p, n, core_z=85, esd_cd=.5e-12))
        out['joint_corner'][tag] = sp.metrics(a, b)
    out['geometry_role_coverage'] = {
        net: {'pcb_track_mm': tc.length(v['segs']),
              'role_main_plus_branch_mm': tc.length(tc.segs(None, net))+tc.length(tc.branch_segs(net)),
              'difference_mm': tc.length(v['segs'])-tc.length(tc.segs(None, net))-tc.length(tc.branch_segs(net))}
        for net, v in tc.GEOM_DATA.items()
    }
    args.output.write_text(json.dumps(out, indent=2)+'\n', encoding='utf-8')
    print('Maximum saved-result difference:', max(r['max_abs_error_db'] for r in out['reproduction'].values()), 'dB')
    print('Joint corner worst reflection:', max(r['s11_max_1500'] for r in out['joint_corner'].values()), 'dB')


if __name__ == '__main__':
    main()
