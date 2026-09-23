"""Frame-time distribution and spike summary per mission phase from a game.log.

Reads the lines the native renderer writes with --edf_native_frame_times=true
(and, when present, --edf_native_gpu_timings=true):

  Native frame times: frames=N span_ms=S p50_ms= p90_ms= p99_ms= p99_9_ms=
      max_ms= mean_ms= spikes=K suppressed_spikes=Z hist=LOW:COUNT,...
      one window of present-to-present times; hist is its histogram (bucket
      lower bound in ms: 0.25 ms wide below 50 ms, 1 ms to 100, 10 ms to 1000,
      then one overflow bucket), which is what lets windows be merged
  Native frame spike: frame=F ms=M median_ms=D reason=R <counter>=<delta> ...
      top_phases=NAME:MS,...|off|none
      one frame over 25 ms or twice the rolling median, with what else
      happened in it (pipelines, shader_compiles, mesh_builds, buffers,
      buffer_kb, textures, texture_kb, declines, post_fallbacks, ...)
  Native GPU timing: pass=P frames=N total_ms= avg_ms= max_ms=
      GPU time per full-frame pass from timestamps

and cuts the log into the mission phases renderer-runtime-gate.py uses
(pre_entry, intro, loading, gameplay; see that tool for the markers; a log
without the mission-entry marker is "unphased"). A
frame-time window counts for a phase only when its whole span (stamp minus
span_ms) lies inside one of the phase's segments, else it counts as
"boundary"; a spike or GPU line counts where it is stamped. "all" covers every
line. Percentiles over a phase come from the merged histograms, nearest rank,
reported as the bucket's upper bound (capped at the phase's max), so they are
exact to the bucket width; the per-window percentiles in the log are exact.

Prints a table, or JSON with --json.
"""
import argparse
import importlib.util
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    'renderer_runtime_gate', Path(__file__).resolve().parent / 'renderer-runtime-gate.py')
gate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gate)

NUMBER = r'([\d.eE+-]+)'
FRAME_TIMES = re.compile(
    r'Native frame times: frames=(\d+) span_ms=' + NUMBER + r' p50_ms=' + NUMBER + r' p90_ms=' + NUMBER +
    r' p99_ms=' + NUMBER + r' p99_9_ms=' + NUMBER + r' max_ms=' + NUMBER + r' mean_ms=' + NUMBER +
    r' spikes=(\d+) suppressed_spikes=(\d+) hist=(\S+)')
SPIKE = re.compile(r'Native frame spike: (.*)$')
GPU = re.compile(r'Native GPU timing: pass=(\S+) frames=(\d+) total_ms=' + NUMBER + r' avg_ms=' + NUMBER +
                 r' max_ms=' + NUMBER)
GPU_WINDOW = re.compile(r'Native GPU timing window: frames=(\d+) skipped=(\d+) dropped_spans=(\d+) invalid_spans=(\d+)')
PHASES = ('pre_entry', *gate.GAME_PHASES, 'boundary', 'unphased', 'all')
# Spike fields that describe the frame rather than attribute it.
SPIKE_FRAME_FIELDS = ('frame', 'ms', 'median_ms', 'reason', 'top_phases')
PERCENTILES = (('p50', 0.50), ('p90', 0.90), ('p99', 0.99), ('p99_9', 0.999))


def bucket_upper(lower):
    """Upper bound of the histogram bucket starting at `lower` ms."""
    if lower < 50:
        return lower + 0.25
    if lower < 100:
        return lower + 1
    if lower < 1000:
        return lower + 10
    return math.inf


def parse_histogram(text):
    if text == 'none':
        return {}
    out = {}
    for item in text.split(','):
        lower, _, count = item.partition(':')
        out[round(float(lower), 2)] = int(count)
    return out


def histogram_percentiles(histogram, maximum):
    total = sum(histogram.values())
    out = {}
    for name, p in PERCENTILES:
        if not total:
            out[name] = None
            continue
        rank, seen = max(1, math.ceil(p * total)), 0
        for lower in sorted(histogram):
            seen += histogram[lower]
            if seen >= rank:
                out[name] = round(min(bucket_upper(lower), maximum), 3)
                break
    return out


def parse_spike(text):
    fields = {}
    for token in text.split():
        key, sep, value = token.partition('=')
        if not sep:
            continue
        if key in ('reason', 'top_phases'):
            fields[key] = value
            continue
        try:
            fields[key] = float(value) if '.' in value else int(value)
        except ValueError:
            fields[key] = value
    return fields


def classify(t, span, entry, windows):
    """The phase a line stamped at t covering `span` seconds belongs to."""
    if entry is None:
        return 'unphased'
    if t < entry:
        return 'pre_entry'
    for phase in gate.GAME_PHASES:
        if gate.in_windows(t, span, windows[phase]):
            return phase
    return 'boundary'


def new_phase():
    return dict(windows=0, frames=0, span_ms=0.0, spikes_counted=0, suppressed_spikes=0, max_ms=None,
                mean_weighted=0.0, histogram=Counter(), spikes=[], gpu=defaultdict(
                    lambda: dict(frames=0, total_ms=0.0, max_ms=0.0)),
                gpu_windows=dict(frames=0, skipped=0, dropped_spans=0, invalid_spans=0))


def collect(lines, start=0.0, end=1e9):
    """Per-phase accumulators for every report line in the log."""
    segs = gate.segments(lines)
    entry = segs[0][1] if segs else None
    # A mission without a pre-mission scene enters gameplay directly: no intro.
    windows = gate.phase_windows(segs, start, end, not gate.entry_is_gameplay(lines, segs))
    phases = defaultdict(new_phase)
    for t, line in gate.stamped(lines):
        m = FRAME_TIMES.search(line)
        if m:
            frames, span_ms = int(m.group(1)), float(m.group(2))
            maximum, mean = float(m.group(7)), float(m.group(8))
            for phase in (classify(t, span_ms / 1000.0, entry, windows), 'all'):
                row = phases[phase]
                row['windows'] += 1
                row['frames'] += frames
                row['span_ms'] += span_ms
                row['spikes_counted'] += int(m.group(9))
                row['suppressed_spikes'] += int(m.group(10))
                row['max_ms'] = maximum if row['max_ms'] is None else max(row['max_ms'], maximum)
                row['mean_weighted'] += mean * frames
                row['histogram'].update(parse_histogram(m.group(11)))
            continue
        m = SPIKE.search(line)
        if m:
            spike = parse_spike(m.group(1))
            spike['t_s'] = round(t, 3)
            for phase in (classify(t, 0, entry, windows), 'all'):
                phases[phase]['spikes'].append(spike)
            continue
        m = GPU.search(line)
        if m:
            for phase in (classify(t, 0, entry, windows), 'all'):
                row = phases[phase]['gpu'][m.group(1)]
                row['frames'] += int(m.group(2))
                row['total_ms'] += float(m.group(3))
                row['max_ms'] = max(row['max_ms'], float(m.group(5)))
            continue
        m = GPU_WINDOW.search(line)
        if m:
            for phase in (classify(t, 0, entry, windows), 'all'):
                row = phases[phase]['gpu_windows']
                for key, value in zip(('frames', 'skipped', 'dropped_spans', 'invalid_spans'), m.groups()):
                    row[key] += int(value)
    return entry, phases


def summarize_spikes(spikes, top):
    causes = Counter()
    leaders = Counter()
    reasons = Counter()
    for spike in spikes:
        reasons[spike.get('reason', '?')] += 1
        for key, value in spike.items():
            if key in SPIKE_FRAME_FIELDS or key == 't_s':
                continue
            if isinstance(value, (int, float)) and value > 0:
                causes[key] += 1
        phases = spike.get('top_phases', 'off')
        if phases not in ('off', 'none'):
            leaders[phases.split(',')[0].rpartition(':')[0]] += 1
    unexplained = sum(1 for s in spikes if not any(
        isinstance(v, (int, float)) and v > 0 for k, v in s.items() if k not in SPIKE_FRAME_FIELDS and k != 't_s'))
    worst = sorted(spikes, key=lambda s: s.get('ms', 0), reverse=True)[:top]
    return dict(count=len(spikes), reasons=dict(reasons), causes=dict(causes.most_common()),
                without_counter_cause=unexplained, top_phase_leaders=dict(leaders.most_common()), worst=worst)


def report(lines, start=0.0, end=1e9, top=5):
    entry, phases = collect(lines, start, end)
    out = dict(mission_entry_s=None if entry is None else round(entry, 1), phases={})
    for phase in PHASES:
        if phase not in phases:
            continue
        row = phases[phase]
        frames = row['frames']
        summary = dict(windows=row['windows'], frames=frames, span_s=round(row['span_ms'] / 1000.0, 1),
                       mean_ms=round(row['mean_weighted'] / frames, 3) if frames else None,
                       max_ms=row['max_ms'],
                       spikes_in_windows=row['spikes_counted'], suppressed_spikes=row['suppressed_spikes'])
        summary.update(histogram_percentiles(row['histogram'], row['max_ms'] or 0.0))
        summary['spikes'] = summarize_spikes(row['spikes'], top)
        summary['gpu'] = {name: dict(frames=g['frames'], avg_ms=round(g['total_ms'] / g['frames'], 3) if g['frames'] else None,
                                     max_ms=g['max_ms']) for name, g in row['gpu'].items()}
        if row['gpu_windows']['frames'] or row['gpu_windows']['skipped']:
            summary['gpu_windows'] = row['gpu_windows']
        out['phases'][phase] = summary
    return out


def fmt(value, width=8):
    return f'{"-":>{width}}' if value is None else f'{value:>{width}.3f}'


def print_text(result, file=None):
    file = file or sys.stdout
    entry = result['mission_entry_s']
    print(f'mission entry: {"not found" if entry is None else f"{entry} s"}', file=file)
    print(f'{"phase":<10} {"frames":>7} {"p50":>8} {"p90":>8} {"p99":>8} {"p99.9":>8} {"max":>8} {"mean":>8} {"spikes":>6}',
          file=file)
    for phase, row in result['phases'].items():
        print(f'{phase:<10} {row["frames"]:>7} {fmt(row["p50"])} {fmt(row["p90"])} {fmt(row["p99"])} '
              f'{fmt(row["p99_9"])} {fmt(row["max_ms"])} {fmt(row["mean_ms"])} {row["spikes"]["count"]:>6}', file=file)
    for phase, row in result['phases'].items():
        spikes = row['spikes']
        if phase == 'all' or not (spikes['count'] or row['gpu']):
            continue
        print(f'\n[{phase}]', file=file)
        if spikes['count']:
            causes = ', '.join(f'{k} {v}' for k, v in spikes['causes'].items()) or 'none'
            print(f'  spikes: {spikes["count"]} ({", ".join(f"{k} {v}" for k, v in spikes["reasons"].items())}); '
                  f'frames with a counter moving: {causes}; with none: {spikes["without_counter_cause"]}', file=file)
            if spikes['top_phase_leaders']:
                print('  largest hook phase: ' + ', '.join(f'{k} {v}' for k, v in spikes['top_phase_leaders'].items()),
                      file=file)
            for spike in spikes['worst']:
                moved = ' '.join(f'{k}={v}' for k, v in spike.items()
                                 if k not in SPIKE_FRAME_FIELDS and k != 't_s' and isinstance(v, (int, float)) and v > 0)
                fields = [f't={spike["t_s"]}s', f'frame={spike.get("frame")}', f'ms={spike.get("ms")}',
                          f'median={spike.get("median_ms")}', f'reason={spike.get("reason")}', moved,
                          f'top_phases={spike.get("top_phases")}']
                print('    ' + ' '.join(f for f in fields if f), file=file)
        if row['gpu']:
            print('  gpu: ' + ', '.join(f'{name} avg {g["avg_ms"]} max {g["max_ms"]}' for name, g in row['gpu'].items()),
                  file=file)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('log')
    ap.add_argument('--start', type=float, default=0.0,
                    help='seconds after entry (intro) and after the gameplay marker where phases start')
    ap.add_argument('--end', type=float, default=1e9, help='seconds after the gameplay marker where gameplay ends')
    ap.add_argument('--top', type=int, default=5, help='worst spikes listed per phase')
    ap.add_argument('--json', action='store_true', help='print JSON instead of a table')
    args = ap.parse_args(argv)
    lines = gate.read_log_lines(args.log)
    result = report(lines, args.start, args.end, args.top)
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_text(result)
    return 0 if result['phases'] else 1


if __name__ == '__main__':
    sys.exit(main())
