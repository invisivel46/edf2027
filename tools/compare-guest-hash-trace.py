#!/usr/bin/env python3
"""Compare two guest state hash traces (--edf_guest_hash_trace, src/guest_state_hash.h).

  python tools/compare-guest-hash-trace.py baseline.csv candidate.csv [--key seq|sim_ticks]
      [--chunks N]

Lines are matched by dispatch sequence number (default) or by simulation tick. Reports how many
matched lines agree, the first line whose whole-memory hash differs and which heaps differ there,
and, with --chunks N, the chunk ranges that differ between the two runs' per-chunk dumps at
dispatch N (<trace>.chunks-N.csv, written with --edf_guest_hash_dump_at=N). Exit status 0 when every
common line agrees, 1 otherwise.
"""
import argparse
import csv
import sys

HEAPS = ['v00', 'v40', 'v80', 'v90', 'vA0', 'vC0', 'vE0']


def load(path, key):
    rows = {}
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            rows[int(row[key])] = row
    return rows


def chunks(path):
    out = {}
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            out[int(row['address'], 16)] = (int(row['size']), row['heap'], row['hash'])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('baseline')
    ap.add_argument('candidate')
    ap.add_argument('--key', default='seq', choices=['seq', 'sim_ticks'])
    ap.add_argument('--chunks', type=int, help='compare the per-chunk dumps written at this dispatch')
    ap.add_argument('--show', type=int, default=10, help='differing lines/chunks to list')
    a = ap.parse_args()
    base, cand = load(a.baseline, a.key), load(a.candidate, a.key)
    common = sorted(set(base) & set(cand))
    diffs = [k for k in common if base[k]['hash'] != cand[k]['hash']]
    print(f'{len(base)} baseline lines, {len(cand)} candidate lines, {len(common)} common by {a.key}; '
          f'{len(common) - len(diffs)} agree, {len(diffs)} differ')
    for k in diffs[:a.show]:
        heaps = [h for h in HEAPS if base[k][h] != cand[k][h]]
        print(f'  {a.key}={k} sim_ticks={base[k]["sim_ticks"]}/{cand[k]["sim_ticks"]} '
              f'steps={base[k]["steps"]}/{cand[k]["steps"]} bytes={base[k]["bytes"]}/{cand[k]["bytes"]} '
              f'heaps differing: {" ".join(heaps)}')
    if diffs:
        first = diffs[0]
        agree_before = [k for k in common if k < first]
        print(f'first difference at {a.key}={first}; {len(agree_before)} common lines agree before it')
    if a.chunks is not None:
        cb = chunks(f'{a.baseline}.chunks-{a.chunks}.csv')
        cc = chunks(f'{a.candidate}.chunks-{a.chunks}.csv')
        only_b = sorted(set(cb) - set(cc))
        only_c = sorted(set(cc) - set(cb))
        differ = [x for x in sorted(set(cb) & set(cc)) if cb[x] != cc[x]]
        print(f'chunks at {a.chunks}: {len(cb)}/{len(cc)}, {len(differ)} differ, '
              f'{len(only_b)} only in baseline, {len(only_c)} only in candidate')
        for x in differ[:a.show]:
            print(f'  {x:08X}+{cb[x][0]:X} {cb[x][1]}')
        for x in only_b[:a.show]:
            print(f'  baseline only {x:08X}+{cb[x][0]:X} {cb[x][1]}')
        for x in only_c[:a.show]:
            print(f'  candidate only {x:08X}+{cc[x][0]:X} {cc[x][1]}')
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
