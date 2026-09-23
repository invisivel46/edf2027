"""Input-to-photon latency per stage from --edf_native_input_latency_trace runs.

Reads either source the trace writes (src/input_latency.cpp, input_latency_logic.h):

  game.log lines, one per input kind every 5 s:
    Input latency: kind=mouse n=N input_tick=P50/P90/P99/MAX tick_record=... record_submit=...
        submit_acquire=... acquire_present=... present_photon=... total=... photon_estimated=E
        ui_block_pct=X ui_block_delay_ms=Y low_latency=0|1 vsync=0|1 expired=A discarded=B
        overflow=C span_ms=S in_flight=F
    Windows are merged by sample count: a stage's p50/p90/p99 over a run is the n-weighted
    mean of the window percentiles (an approximation, marked "~"), its max the largest max.
  the per-event CSV (--edf_native_input_latency_csv=PATH):
    kind,input_ns,tick,tick_ns,record_ns,sequence,submit_ns,acquire_ns,present_id,present_ns,
    photon_ns,photon_estimated,low_latency,vsync
    Percentiles are exact (nearest rank, the rule input_latency_logic.h uses).

Rows are grouped by input kind and the low_latency/vsync settings the samples ran under, so
one run that toggled edf_low_latency reports both halves. With two inputs, --compare prints
the second minus the first for every stage of the rows they share (before -> after).

Stages: input_tick (window event to the simulation step's pad poll), tick_record (to the
render that shows that step), record_submit (to the image's publication), submit_acquire
(to the presenter copying it or a newer image), acquire_present (to Present returning),
present_photon (to the vblank; DXGI statistics or the frame-latency waitable's next return,
counted in photon_estimated), total.
"""
import argparse
import csv
import importlib.util
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    'renderer_runtime_gate', Path(__file__).resolve().parent / 'renderer-runtime-gate.py')
gate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gate)

STAGES = ('input_tick', 'tick_record', 'record_submit', 'submit_acquire', 'acquire_present', 'present_photon',
          'total')
NUMBER = r'(-?[\d.]+)'
QUAD = NUMBER + '/' + NUMBER + '/' + NUMBER + '/' + NUMBER
LINE = re.compile(
    r'Input latency: kind=(\w+) n=(\d+) ' + ' '.join(f'{stage}={QUAD}' for stage in STAGES) +
    r' photon_estimated=(\d+) ui_block_pct=' + NUMBER + r' ui_block_delay_ms=' + NUMBER +
    r' low_latency=([01]) vsync=([01]) expired=(\d+) discarded=(\d+) overflow=(\d+)')
CSV_FIELDS = ('kind', 'input_ns', 'tick', 'tick_ns', 'record_ns', 'sequence', 'submit_ns', 'acquire_ns',
              'present_id', 'present_ns', 'photon_ns', 'photon_estimated', 'low_latency', 'vsync')


def nearest_rank(sorted_values, p):
    """Value at rank ceil(p * n), 1-based, as input_latency_logic.h Summarize does."""
    if not sorted_values:
        return 0.0
    k = max(1, math.ceil(p * len(sorted_values)))
    return sorted_values[min(k, len(sorted_values)) - 1]


def stage_ms(row):
    t = [row['input_ns'], row['tick_ns'], row['record_ns'], row['submit_ns'], row['acquire_ns'], row['present_ns'],
         row['photon_ns']]
    steps = [(t[i + 1] - t[i]) / 1e6 for i in range(6)]
    return steps + [(t[6] - t[0]) / 1e6]


def key_of(kind, low_latency, vsync):
    return f'{kind} low_latency={low_latency} vsync={vsync}'


def from_csv(path):
    """Exact per-stage percentiles from the per-event CSV."""
    groups = defaultdict(lambda: {'values': [[] for _ in STAGES], 'estimated': 0})
    with open(path, newline='', encoding='utf-8') as handle:
        reader = csv.DictReader(handle)
        missing = [field for field in CSV_FIELDS if field not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(f'{path}: not an input latency CSV (missing {", ".join(missing)})')
        for raw in reader:
            row = {name: int(raw[name]) for name in CSV_FIELDS if name != 'kind'}
            group = groups[key_of(raw['kind'], row['low_latency'], row['vsync'])]
            for i, value in enumerate(stage_ms(row)):
                group['values'][i].append(value)
            group['estimated'] += row['photon_estimated']
    result = {}
    for key, group in sorted(groups.items()):
        stages = {}
        for name, values in zip(STAGES, group['values']):
            values.sort()
            stages[name] = {'p50': nearest_rank(values, 0.50), 'p90': nearest_rank(values, 0.90),
                            'p99': nearest_rank(values, 0.99), 'max': values[-1] if values else 0.0,
                            'mean': sum(values) / len(values) if values else 0.0}
        result[key] = {'n': len(group['values'][0]), 'exact': True, 'stages': stages,
                       'photon_estimated': group['estimated']}
    return result


def from_log(lines):
    """Merged per-stage summaries from the 5 s "Input latency:" windows of a game.log."""
    groups = defaultdict(lambda: {'n': 0, 'windows': 0, 'sums': {s: [0.0, 0.0, 0.0] for s in STAGES},
                                  'max': {s: 0.0 for s in STAGES}, 'estimated': 0, 'ui_pct': 0.0,
                                  'ui_delay': 0.0, 'expired': 0, 'discarded': 0, 'overflow': 0})
    for line in lines:
        match = LINE.search(line)
        if not match:
            continue
        values = match.groups()
        kind, n = values[0], int(values[1])
        quads = [tuple(float(v) for v in values[2 + 4 * i:6 + 4 * i]) for i in range(len(STAGES))]
        rest = values[2 + 4 * len(STAGES):]
        estimated, ui_pct, ui_delay = int(rest[0]), float(rest[1]), float(rest[2])
        low_latency, vsync = int(rest[3]), int(rest[4])
        expired, discarded, overflow = int(rest[5]), int(rest[6]), int(rest[7])
        group = groups[key_of(kind, low_latency, vsync)]
        group['windows'] += 1
        group['ui_pct'] += ui_pct
        group['ui_delay'] += ui_delay
        group['expired'] += expired
        group['discarded'] += discarded
        group['overflow'] += overflow
        if n == 0:
            continue
        group['n'] += n
        group['estimated'] += estimated
        for stage, (p50, p90, p99, maximum) in zip(STAGES, quads):
            sums = group['sums'][stage]
            sums[0] += p50 * n
            sums[1] += p90 * n
            sums[2] += p99 * n
            group['max'][stage] = max(group['max'][stage], maximum)
    result = {}
    for key, group in sorted(groups.items()):
        n = group['n']
        stages = {stage: {'p50': group['sums'][stage][0] / n if n else 0.0,
                          'p90': group['sums'][stage][1] / n if n else 0.0,
                          'p99': group['sums'][stage][2] / n if n else 0.0,
                          'max': group['max'][stage]} for stage in STAGES}
        windows = group['windows']
        result[key] = {'n': n, 'exact': False, 'stages': stages, 'photon_estimated': group['estimated'],
                       'windows': windows, 'ui_block_pct': group['ui_pct'] / windows if windows else 0.0,
                       'ui_block_delay_ms': group['ui_delay'] / windows if windows else 0.0,
                       'expired': group['expired'], 'discarded': group['discarded'], 'overflow': group['overflow']}
    return result


def load(path):
    path = Path(path)
    if path.suffix.lower() == '.csv':
        return from_csv(path)
    return from_log(gate.read_log_lines(path))


def compare(before, after):
    """after - before, per shared row and stage."""
    out = {}
    for key in sorted(set(before) & set(after)):
        out[key] = {stage: {p: after[key]['stages'][stage][p] - before[key]['stages'][stage][p]
                            for p in ('p50', 'p90', 'p99', 'max')} for stage in STAGES}
    return out


def print_report(name, result, file=None):
    print(f'== {name}', file=file)
    if not result:
        print('  no "Input latency:" samples (was --edf_native_input_latency_trace on?)', file=file)
        return
    for key, row in result.items():
        mark = '' if row['exact'] else '~'
        extra = ''
        if not row['exact']:
            extra = (f' windows={row["windows"]} ui_block={row["ui_block_pct"]:.1f}% '
                     f'ui_block_delay={row["ui_block_delay_ms"]:.2f} ms expired={row["expired"]} '
                     f'discarded={row["discarded"]} overflow={row["overflow"]}')
        print(f'  {key}: n={row["n"]} photon_estimated={row["photon_estimated"]}{extra}', file=file)
        print(f'    {"stage":<16}{"p50":>10}{"p90":>10}{"p99":>10}{"max":>10}  (ms)', file=file)
        for stage in STAGES:
            s = row['stages'][stage]
            print(f'    {stage:<16}' + ''.join(f'{mark + format(s[p], ".2f"):>10}' for p in ('p50', 'p90', 'p99'))
                  + f'{s["max"]:>10.2f}', file=file)


def print_compare(delta, file=None):
    print('== after - before (ms; negative is faster)', file=file)
    if not delta:
        print('  no row in common (same input kind, low_latency and vsync)', file=file)
    for key, stages in delta.items():
        print(f'  {key}', file=file)
        for stage in STAGES:
            d = stages[stage]
            print(f'    {stage:<16}' + ''.join(f'{d[p]:>+10.2f}' for p in ('p50', 'p90', 'p99', 'max')), file=file)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('inputs', nargs='+', help='game.log (rotated parts are read too) or trace CSV files')
    ap.add_argument('--compare', action='store_true', help='with two inputs: print the second minus the first')
    ap.add_argument('--json', action='store_true', help='print JSON instead of tables')
    args = ap.parse_args(argv)
    if args.compare and len(args.inputs) != 2:
        ap.error('--compare needs exactly two inputs (before, after)')
    results = {str(path): load(path) for path in args.inputs}
    delta = compare(*results.values()) if args.compare else None
    if args.json:
        print(json.dumps({'runs': results, 'compare': delta}, indent=2))
    else:
        for name, result in results.items():
            print_report(name, result)
        if delta is not None:
            print_compare(delta)
    return 0 if all(results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
