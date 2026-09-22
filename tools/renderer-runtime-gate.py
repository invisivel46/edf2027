"""Runtime acceptance gate for the native renderer.

Compares a candidate game.log against a baseline game.log from the same input
script. Passes only when the candidate's game FPS in the measured window is
not worse than the baseline and, when --min-native-groups is set, enough
nonempty static groups executed with zero compatibility calls.

When both logs carry "Native hook timing" lines (--edf_native_hook_timings=true)
the report also has a "phases" key with each renderer phase's cost in the same
window: ms per call and ms per frame, where a frame is one engine.render_helper
call. --max-phase and --expect-drop gate on ms per frame (ms per call when
either log has no engine.render_helper timing). Hook buckets are per thread and
inclusive, so phases overlap and must not be summed. A bucket is attributed to
the window its report line falls in, so a boundary bucket may hold a few calls
from just before the window.

Offline suites prove narrow contracts; this gate is what decides whether a
renderer change is an improvement in the running game.
"""
import argparse
import json
import re
import statistics
import sys
from pathlib import Path

FPS = re.compile(r'FPS: ([\d.]+) \(frames \d+ over [\d.]+ s, t=(\d+) s\)')
PACING = re.compile(r'Native D3D12 host pacing: samples=(\d+),.*repeated_images=(\d+)')
GROUP = re.compile(r'Native static group execution: group=(0x[0-9a-f]+) completed=(\w+) '
                   r'recorded_batches=(\d+) compatibility_calls=(\d+)')
MARKER = re.compile(r'\[(error|critical)\]')
STAMP = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]')
# Non-sampled report from HookTiming::Finish in guest_shader_bridge.cpp. The
# sampled variant ("Native sampled hook timing") counts samples, not calls.
HOOK = re.compile(r'Native hook timing: phase=([\w.]+) calls=(\d+) total_ms=([\d.eE+-]+) max_ms=([\d.eE+-]+)')
FRAME_PHASE = 'engine.render_helper'
PHASES = ('engine.render_helper', 'render.queued', 'render.material_group', 'render.children',
          'render.gather', 'render.model', 'render.finish', 'render.list', 'render.buckets')
# The first scene draw after the loading screen. Loading takes anywhere from
# seconds to minutes, so windows are measured from here, not from launch.
ENTRY = 'Native indexed input: draw=1,'


def mission_entry(lines):
    from datetime import datetime
    first = entry = None
    for line in lines:
        m = STAMP.match(line)
        if not m:
            continue
        stamp = datetime.fromisoformat(m.group(1))
        first = first or stamp
        if ENTRY in line:
            entry = stamp
            break
    return (entry - first).total_seconds() if first and entry else None


def summarize(path, start, end):
    fps, samples, repeats, groups, errors, hooks = [], 0, 0, {}, 0, {}
    in_window = False
    lines = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    entry = mission_entry(lines)
    if entry is None:
        return dict(log=str(path), mission_entry_s=None, fps_samples=0, fps_median=None, fps_min=None,
                    host_presents=0, host_repeated_images=0, nonempty_groups=0, native_groups=0, errors=0,
                    hook_phases={})
    start, end = entry + start, entry + end
    for line in lines:
        m = FPS.search(line)
        if m:
            t = int(m.group(2))
            in_window = start <= t <= end
            if in_window:
                fps.append(float(m.group(1)))
            continue
        if MARKER.search(line):
            errors += 1
        if not in_window:
            continue
        m = PACING.search(line)
        if m:
            samples += int(m.group(1))
            repeats += int(m.group(2))
            continue
        m = HOOK.search(line)
        if m:
            h = hooks.setdefault(m.group(1), dict(calls=0, total_ms=0.0, max_ms=0.0))
            h['calls'] += int(m.group(2))
            h['total_ms'] += float(m.group(3))
            h['max_ms'] = max(h['max_ms'], float(m.group(4)))
            continue
        m = GROUP.search(line)
        if m and int(m.group(3)) > 0:
            native = int(m.group(4)) == 0
            groups[m.group(1)] = groups.get(m.group(1), False) or native
    return dict(
        log=str(path),
        mission_entry_s=round(entry, 1),
        fps_samples=len(fps),
        fps_median=statistics.median(fps) if fps else None,
        fps_min=min(fps) if fps else None,
        host_presents=samples,
        host_repeated_images=repeats,
        nonempty_groups=len(groups),
        native_groups=sum(groups.values()),
        errors=errors,
        hook_phases=hooks,
    )


def phase_costs(hooks, metric):
    """Per-phase cost of one run; metric is 'ms_per_frame' or 'ms_per_call'."""
    frames = hooks.get(FRAME_PHASE, {}).get('calls', 0)
    out = {}
    for phase, h in hooks.items():
        cost = dict(calls=h['calls'], total_ms=h['total_ms'], max_ms=h['max_ms'],
                    ms_per_call=h['total_ms'] / h['calls'] if h['calls'] else None,
                    ms_per_frame=h['total_ms'] / frames if frames else None)
        cost['cost'] = cost[metric]
        out[phase] = cost
    return out


def compare_phases(base_hooks, cand_hooks, extra=()):
    """The "phases" report, or None unless both runs have hook timing lines."""
    if not base_hooks or not cand_hooks:
        return None
    frames = all(h.get(FRAME_PHASE, {}).get('calls') for h in (base_hooks, cand_hooks))
    metric = 'ms_per_frame' if frames else 'ms_per_call'
    base, cand = phase_costs(base_hooks, metric), phase_costs(cand_hooks, metric)

    def delta(b, c, key):
        return c[key] - b[key] if b and c and b[key] is not None and c[key] is not None else None

    rows = {}
    for phase in dict.fromkeys((*PHASES, *extra)):
        b, c = base.get(phase), cand.get(phase)
        rows[phase] = dict(baseline=b, candidate=c, delta_ms_per_call=delta(b, c, 'ms_per_call'),
                           delta_ms_per_frame=delta(b, c, 'ms_per_frame'), delta=delta(b, c, 'cost'))
    return dict(metric=metric, frames=dict(baseline=base_hooks.get(FRAME_PHASE, {}).get('calls', 0),
                                           candidate=cand_hooks.get(FRAME_PHASE, {}).get('calls', 0)),
                by_phase=rows)


def phase_limit(text):
    phase, sep, value = text.partition('=')
    try:
        if not sep or not phase:
            raise ValueError
        return phase, float(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f'expected PHASE=MS, got {text!r}')


def phase_failures(phases, max_phase, expect_drop):
    if not max_phase and not expect_drop:
        return []
    if phases is None:
        return ['phase limits need "Native hook timing" lines in both logs (--edf_native_hook_timings=true)']
    unit = 'ms/frame' if phases['metric'] == 'ms_per_frame' else 'ms/call'
    failures = []
    for phase, limit in max_phase:
        c = phases['by_phase'][phase]['candidate']
        if c is None:
            failures.append(f'{phase}: no candidate timing in the window')
        elif c['cost'] > limit:
            failures.append(f"{phase} costs {c['cost']:.4f} {unit}, above the {limit:g} limit")
    for phase in expect_drop:
        b, c = phases['by_phase'][phase]['baseline'], phases['by_phase'][phase]['candidate']
        if b is None or c is None:
            failures.append(f'{phase}: needs timing in both runs to show a drop')
        elif not c['cost'] < b['cost']:
            failures.append(f"{phase} costs {c['cost']:.4f} {unit}, not below baseline {b['cost']:.4f}")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('candidate')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--start', type=int, default=10, help='seconds after mission entry where measuring starts')
    ap.add_argument('--end', type=int, default=150, help='seconds after mission entry where measuring ends')
    ap.add_argument('--tolerance', type=float, default=0.05, help='allowed fractional FPS drop')
    ap.add_argument('--min-native-groups', type=int, default=0)
    ap.add_argument('--max-phase', type=phase_limit, action='append', default=[], metavar='PHASE=MS',
                    help='fail when the candidate phase costs more than MS per frame (repeatable)')
    ap.add_argument('--expect-drop', action='append', default=[], metavar='PHASE',
                    help='fail unless the candidate phase costs less than the baseline (repeatable)')
    args = ap.parse_args()

    base = summarize(args.baseline, args.start, args.end)
    cand = summarize(args.candidate, args.start, args.end)
    extra = [phase for phase, _ in args.max_phase] + args.expect_drop
    phases = compare_phases(base.pop('hook_phases'), cand.pop('hook_phases'), extra)
    failures = []
    if not base['fps_samples'] or not cand['fps_samples']:
        failures.append('no FPS samples in the window; the run did not reach it')
    elif cand['fps_median'] < base['fps_median'] * (1 - args.tolerance):
        failures.append(f"median FPS {cand['fps_median']:.1f} is below baseline {base['fps_median']:.1f}")
    if cand['native_groups'] < args.min_native_groups:
        failures.append(f"{cand['native_groups']} fully native static groups, need {args.min_native_groups}")
    failures += phase_failures(phases, args.max_phase, args.expect_drop)
    report = dict(window_after_entry=[args.start, args.end], baseline=base, candidate=cand, phases=phases,
                  passed=not failures, failures=failures)
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
