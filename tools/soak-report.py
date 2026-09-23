"""Soak report: memory, renderer cache sizes and frame-time percentiles over time.

Cuts a long run's game.log into windows (5 minutes by default) from the
gameplay marker (renderer-runtime-gate.py's markers; the first stamp when the
log has none) and reports per window:

  memory     "Native memory: private_mb= working_set_mb= peak_working_set_mb=
             pagefile_mb= handles= sim_ticks=" (--edf_native_memory_log=true,
             beside the FPS line every 5 s): first, last and max per window
  caches     the sizes the renderer's own stats lines already print, last value
             per window: scene backend pipelines, sampler tables and retiring
             objects; immediate mesh cache bytes and entries; resident guest
             textures; render registry entries and records; full-frame model
             entries; full-frame static world groups
  frames     "Native frame times" windows (--edf_native_frame_times=true) merged
             into histogram percentiles p50/p90/p99/p99.9, max, and spike counts
  fps        the 5 s "FPS:" lines, median and min

and over the whole soak: private and working-set growth (least-squares slope in
MB per minute, and last window mean minus first window mean), each cache's
first/last/max, the late-stutter ratio (last window p99 over first window p99),
and every loading screen after the origin (a mission end, death or restart
leaves gameplay, which invalidates later windows as a soak of gameplay).

Gates, each optional: --max-growth-mb-per-min (private bytes slope),
--max-growth-mb (private growth first to last window), --max-p99-ratio,
--max-cache-growth NAME=N (last minus first, repeatable), --require-gameplay
(fail when a loading screen follows the origin). Prints a table, or JSON with
--json. Exits 0 on pass, 1 on a failed gate, 2 when the log has no data.
"""
import argparse
import importlib.util
import json
import math
import re
import statistics
import sys
from collections import Counter
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, TOOLS / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gate = _load('renderer_runtime_gate', 'renderer-runtime-gate.py')
frames = _load('frame_time_report', 'frame-time-report.py')

NUMBER = r'([\d.]+)'
MEMORY = re.compile(r'Native memory: private_mb=' + NUMBER + r' working_set_mb=' + NUMBER +
                    r' peak_working_set_mb=' + NUMBER + r' pagefile_mb=' + NUMBER + r' handles=(\d+)'
                    r' sim_ticks=(\d+)')
MEMORY_FIELDS = ('private_mb', 'working_set_mb', 'peak_working_set_mb', 'pagefile_mb', 'handles')
# (gauge name, line prefix, value pattern). Each is the size of something the
# renderer keeps, as its periodic stats line prints it.
GAUGES = (
    ('backend.pipelines', 'Native scene backend spend:', re.compile(r'\bpipelines=(\d+) \(')),
    ('backend.sampler_tables', 'Native scene backend spend:', re.compile(r'\bsampler_tables=(\d+) \(')),
    ('backend.retiring', 'Native scene backend spend:', re.compile(r'\bretiring=(\d+)')),
    ('immediate_mesh.bytes', 'Native immediate mesh cache:', re.compile(r'\bbytes=(\d+)')),
    ('immediate_mesh.entries', 'Native immediate mesh cache:', re.compile(r'\bentries=(\d+)')),
    ('textures.resident', 'Native texture bridge:', re.compile(r'\bresident=(\d+)')),
    ('registry.entries', 'Native render registry:', re.compile(r'\bentries=(\d+)')),
    ('registry.records', 'Native render registry:', re.compile(r'\brecords=(\d+)')),
    ('models.entries', 'Native full frame models:', re.compile(r'\bentries=(\d+)')),
    ('static_world.groups', 'Native full frame static world:', re.compile(r'\bgroups=(\d+)')),
)
GAUGE_NAMES = tuple(name for name, _, _ in GAUGES)


def slope_per_minute(points):
    """Least-squares slope of (seconds, value) points, per minute; None below two points."""
    if len(points) < 2:
        return None
    mean_t = statistics.fmean(t for t, _ in points)
    mean_v = statistics.fmean(v for _, v in points)
    var = sum((t - mean_t) ** 2 for t, _ in points)
    if not var:
        return None
    return sum((t - mean_t) * (v - mean_v) for t, v in points) / var * 60.0


def new_window(index, origin, width):
    return dict(index=index, from_s=round(origin + index * width, 1), to_s=round(origin + (index + 1) * width, 1),
                fps=[], frames=0, histogram=Counter(), max_ms=None, spikes=0,
                memory={k: [] for k in MEMORY_FIELDS}, gauges={})


def collect(lines, window_s=300.0, origin_mode='gameplay'):
    marks = gate.markers(lines)
    stamps = list(gate.stamped(lines))
    if not stamps:
        return None
    if origin_mode == 'gameplay' and marks['gameplay'] is not None:
        origin, origin_name = marks['gameplay'], 'gameplay'
    elif origin_mode in ('gameplay', 'entry') and marks['entry'] is not None:
        origin, origin_name = marks['entry'], 'entry'
    else:
        origin, origin_name = 0.0, 'first stamp'
    windows = {}
    memory_points = {k: [] for k in MEMORY_FIELDS}
    gauge_points = {name: [] for name in GAUGE_NAMES}

    def window_at(t):
        index = int(math.floor((t - origin) / window_s))
        if index not in windows:
            windows[index] = new_window(index, origin, window_s)
        return windows[index]

    for t, line in stamps:
        if t < origin:
            continue
        m = gate.FPS.search(line)
        if m:
            window_at(t)['fps'].append(float(m.group(1)))
            continue
        m = frames.FRAME_TIMES.search(line)
        if m:
            w = window_at(t)
            w['frames'] += int(m.group(1))
            w['histogram'].update(frames.parse_histogram(m.group(11)))
            maximum = float(m.group(7))
            w['max_ms'] = maximum if w['max_ms'] is None else max(w['max_ms'], maximum)
            w['spikes'] += int(m.group(9))
            continue
        m = MEMORY.search(line)
        if m:
            w = window_at(t)
            for key, value in zip(MEMORY_FIELDS, m.groups()):
                w['memory'][key].append(float(value))
                memory_points[key].append((t - origin, float(value)))
            continue
        for name, prefix, pattern in GAUGES:
            if prefix in line:
                g = pattern.search(line)
                if g:
                    value = int(g.group(1))
                    window_at(t)['gauges'][name] = value
                    gauge_points[name].append((t - origin, value))
    loading_after = [[round(a, 1), round(b, 1)] for kind, a, b in gate.segments(lines)
                     if kind == 'loading' and a >= origin]
    return dict(markers_s={k: v if v is None or k == 'loading_screens' else round(v, 1) for k, v in marks.items()},
                origin=origin_name, origin_s=round(origin, 1), window_s=window_s,
                windows=[windows[i] for i in sorted(windows)], memory_points=memory_points,
                gauge_points=gauge_points, loading_after_origin=loading_after, last_s=stamps[-1][0])


def summarize_window(w, last_s):
    # The log ended before half of this window: too short to compare with the others.
    partial = last_s - w['from_s'] < 0.5 * (w['to_s'] - w['from_s'])
    row = dict(index=w['index'], from_s=w['from_s'], to_s=w['to_s'], partial=partial,
               fps_median=statistics.median(w['fps']) if w['fps'] else None,
               fps_min=min(w['fps']) if w['fps'] else None, frames=w['frames'], max_ms=w['max_ms'],
               spikes=w['spikes'])
    row.update(frames.histogram_percentiles(w['histogram'], w['max_ms'] or 0.0))
    row['memory'] = {k: dict(first=v[0], last=v[-1], max=max(v)) for k, v in w['memory'].items() if v}
    row['gauges'] = dict(w['gauges'])
    return row


def report(lines, window_s=300.0, origin_mode='gameplay'):
    data = collect(lines, window_s, origin_mode)
    if data is None:
        return None
    rows = [summarize_window(w, data['last_s']) for w in data['windows']]
    # Growth and late stutter compare whole windows; a short last window is shown but left out.
    whole = [i for i, r in enumerate(rows) if not r['partial']]
    whole = whole if len(whole) >= 2 else list(range(len(rows)))
    memory = {}
    for key, points in data['memory_points'].items():
        if not points:
            continue
        # Window means, so one noisy sample at either end does not decide the growth.
        per_window = [statistics.fmean(data['windows'][i]['memory'][key]) for i in whole
                      if data['windows'][i]['memory'][key]] or [points[0][1]]
        memory[key] = dict(first=points[0][1], last=points[-1][1], max=max(v for _, v in points),
                           first_window_mean=per_window[0], last_window_mean=per_window[-1],
                           growth=per_window[-1] - per_window[0], slope_per_min=slope_per_minute(points))
    gauges = {}
    for name, points in data['gauge_points'].items():
        if points:
            gauges[name] = dict(first=points[0][1], last=points[-1][1], max=max(v for _, v in points),
                                growth=points[-1][1] - points[0][1], slope_per_min=slope_per_minute(points))
    timed = [rows[i] for i in whole if rows[i]['p99'] is not None]
    stutter = None
    if len(timed) >= 2:
        first, last = timed[0]['p99'], timed[-1]['p99']
        stutter = dict(first_window=timed[0]['index'], last_window=timed[-1]['index'], first_p99_ms=first,
                       last_p99_ms=last, ratio=last / first if first else None,
                       worst_window=max(timed, key=lambda r: r['p99'])['index'])
    return dict(markers_s=data['markers_s'], origin=data['origin'], origin_s=data['origin_s'],
                window_s=window_s, span_s=round(rows[-1]['to_s'] - data['origin_s'], 1) if rows else 0.0,
                windows=rows, memory=memory, caches=gauges, late_stutter=stutter,
                loading_after_origin=data['loading_after_origin'])


def cache_limit(text):
    name, sep, value = text.partition('=')
    try:
        if not sep or name not in GAUGE_NAMES:
            raise ValueError
        return name, float(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f'expected NAME=N with NAME one of {", ".join(GAUGE_NAMES)}, got {text!r}')


def failures(result, args):
    out = []
    private = result['memory'].get('private_mb')
    if args.max_growth_mb_per_min is not None:
        slope = private['slope_per_min'] if private else None
        if slope is None:
            out.append('no private-bytes trend: the log needs "Native memory" lines (--edf_native_memory_log=true)')
        elif slope > args.max_growth_mb_per_min:
            out.append(f'private bytes grow {slope:.2f} MB/min, above {args.max_growth_mb_per_min:g}')
    if args.max_growth_mb is not None:
        if not private:
            out.append('no private-bytes data: the log needs "Native memory" lines (--edf_native_memory_log=true)')
        elif private['growth'] > args.max_growth_mb:
            out.append(f"private bytes grew {private['growth']:.1f} MB from the first to the last window, "
                       f'above {args.max_growth_mb:g}')
    if args.max_p99_ratio is not None:
        stutter = result['late_stutter']
        if stutter is None or stutter['ratio'] is None:
            out.append('no frame-time windows to compare: the log needs "Native frame times" lines '
                       '(--edf_native_frame_times=true) in two windows')
        elif stutter['ratio'] > args.max_p99_ratio:
            out.append(f"last window p99 {stutter['last_p99_ms']:.2f} ms is {stutter['ratio']:.2f}x the first "
                       f"window's {stutter['first_p99_ms']:.2f} ms, above {args.max_p99_ratio:g}x")
    for name, limit in args.max_cache_growth:
        cache = result['caches'].get(name)
        if cache is None:
            out.append(f'{name}: no stats lines in the log')
        elif cache['growth'] > limit:
            out.append(f"{name} grew by {cache['growth']} ({cache['first']} to {cache['last']}), above {limit:g}")
    if args.require_gameplay and result['loading_after_origin']:
        spans = ', '.join(f'{a}-{b} s' for a, b in result['loading_after_origin'])
        out.append(f'left gameplay for a loading screen after the origin ({spans})')
    return out


def fmt(value, width=8, digits=2):
    return f'{"-":>{width}}' if value is None else f'{value:>{width}.{digits}f}'


def print_text(result, file=None):
    file = file or sys.stdout
    print(f"origin: {result['origin']} at {result['origin_s']} s; windows of {result['window_s']:g} s; "
          f"span {result['span_s']} s", file=file)
    print(f'{"window":>6} {"from_s":>8} {"fps":>6} {"p50":>7} {"p90":>7} {"p99":>7} {"p99.9":>7} {"max":>8} '
          f'{"spikes":>6} {"priv_mb":>8} {"ws_mb":>8}', file=file)
    for r in result['windows']:
        private = r['memory'].get('private_mb', {}).get('last')
        working = r['memory'].get('working_set_mb', {}).get('last')
        print(f"{r['index']:>6} {r['from_s']:>8.1f} {fmt(r['fps_median'], 6, 1)} {fmt(r['p50'], 7)} "
              f"{fmt(r['p90'], 7)} {fmt(r['p99'], 7)} {fmt(r['p99_9'], 7)} {fmt(r['max_ms'])} {r['spikes']:>6} "
              f'{fmt(private, 8, 1)} {fmt(working, 8, 1)}', file=file)
    for key, m in result['memory'].items():
        slope = m['slope_per_min']
        print(f"memory {key}: {m['first']:g} -> {m['last']:g} (max {m['max']:g}, window growth "
              f"{m['growth']:+.1f}, slope {fmt(slope, 0, 3).strip()}/min)", file=file)
    for name, c in result['caches'].items():
        print(f"cache {name}: {c['first']} -> {c['last']} (max {c['max']}, growth {c['growth']:+d})", file=file)
    if result['late_stutter']:
        s = result['late_stutter']
        print(f"late stutter: p99 {s['first_p99_ms']} ms -> {s['last_p99_ms']} ms "
              f"(ratio {fmt(s['ratio'], 0, 2).strip()}, worst window {s['worst_window']})", file=file)
    if result['loading_after_origin']:
        print('loading screens after the origin: ' +
              ', '.join(f'{a}-{b} s' for a, b in result['loading_after_origin']), file=file)
    for failure in result.get('failures', []):
        print(f'FAIL: {failure}', file=file)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('log')
    ap.add_argument('--window', type=float, default=300.0, help='window length in seconds (default 300)')
    ap.add_argument('--origin', choices=('gameplay', 'entry', 'start'), default='gameplay',
                    help='where windows start: the gameplay marker (falls back to entry, then the first '
                         'stamp), entry, or the first stamp')
    ap.add_argument('--max-growth-mb-per-min', type=float, default=None)
    ap.add_argument('--max-growth-mb', type=float, default=None)
    ap.add_argument('--max-p99-ratio', type=float, default=None)
    ap.add_argument('--max-cache-growth', type=cache_limit, action='append', default=[], metavar='NAME=N')
    ap.add_argument('--require-gameplay', action='store_true',
                    help='fail when a loading screen follows the origin (the soak left gameplay)')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args(argv)
    result = report(gate.read_log_lines(args.log), args.window, args.origin)
    if result is None or not result['windows']:
        print('soak-report: no stamped lines after the origin', file=sys.stderr)
        return 2
    result['failures'] = failures(result, args)
    result['passed'] = not result['failures']
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_text(result)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
