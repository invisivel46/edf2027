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

Mission 1 has three phases after entry, and when each starts differs between
runs: the pre-mission scene ("intro"), a loading screen ("loading"), then
gameplay. --phase picks what is gated:
  entry     the fixed --start..--end window after entry (the original gate)
  gameplay  --start..--end after the gameplay marker, loading time excluded;
            every metric uses it
  all       FPS of intro, loading and gameplay gated separately
The default is "all" when both logs have a loading marker after entry,
otherwise "entry"; the report says which was used and why. Every mode adds a
"phases_fps" key with per-phase fps_median/fps_min/fps_samples for both runs.
In "all" the baseline/candidate keys stay the entry window, for compatibility.

After entry the log is cut into scene and loading segments, from markers the
log itself carries:
  loading starts  "[NtCreateFile] FAILED: path='GAME:\\MISSION\\<id>\\MISSION.CAM'"
                  (the mission load) or "Native untiled scene viewport:
                  caller=0x8219c828" (the first frame of the loading-screen
                  presenter thread; every loading screen before entry has it too)
  scene resumes   the next line from the thread that logged the entry draw (the
                  scene renderer), which is silent during a loading screen
Intro is the first scene segment from entry+--start; loading is every loading
segment; gameplay is every later scene segment clipped to
gameplay+--start..gameplay+--end, so a return to loading (mission end or
restart) is excluded from gameplay and counted as loading. An FPS sample counts
for a phase only when its whole span (stamp minus "over N s") lies inside one
of the phase's segments.

Offline suites prove narrow contracts; this gate is what decides whether a
renderer change is an improvement in the running game.
"""
import argparse
import json
import re
import statistics
import sys
from datetime import datetime
from pathlib import Path

FPS = re.compile(r'FPS: ([\d.]+) \(frames \d+ over ([\d.]+) s, t=(\d+) s\)')
PACING = re.compile(r'Native D3D12 host pacing: samples=(\d+),.*repeated_images=(\d+)')
GROUP = re.compile(r'Native static group execution: group=(0x[0-9a-f]+) completed=(\w+) '
                   r'recorded_batches=(\d+) compatibility_calls=(\d+)')
MARKER = re.compile(r'\[(error|critical)\]')
STAMP = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]')
THREAD = re.compile(r'\] \[(t\d+)\] ')
# Non-sampled report from HookTiming::Finish in guest_shader_bridge.cpp. The
# sampled variant ("Native sampled hook timing") counts samples, not calls.
HOOK = re.compile(r'Native hook timing: phase=([\w.]+) calls=(\d+) total_ms=([\d.eE+-]+) max_ms=([\d.eE+-]+)')
FRAME_PHASE = 'engine.render_helper'
PHASES = ('engine.render_helper', 'render.queued', 'render.material_group', 'render.children',
          'render.gather', 'render.model', 'render.finish', 'render.list', 'render.buckets',
          # Sub-phases of render.queued in the native static world pass. Inclusive
          # and overlapping: resolve is also counted inside instances on a cache
          # miss, and the handoff binds and replays inside handoff.
          'render.queued.eligibility', 'render.queued.resolve', 'render.queued.instances',
          'render.queued.record', 'render.queued.handoff', 'render.queued.handoff_binds',
          'render.queued.handoff_replays',
          # The full-frame renderer (edf_native_full_frame): the whole native
          # frame, its per-view scene begin, each pass in order, and its end.
          'frame.native', 'frame.native.begin', 'frame.native.static_world', 'frame.native.models',
          'frame.native.sky', 'frame.native.effects', 'frame.native.transparent', 'frame.native.post',
          'frame.native.end')
# The first scene draw after the loading screen. Loading takes anywhere from
# seconds to minutes, so windows are measured from here, not from launch.
ENTRY = 'Native indexed input: draw=1,'
# Loading starts: the mission camera file lookup (M202 for mission 1) or the
# loading-screen presenter thread's first frame.
LOAD = re.compile(r"MISSION\\[^'\\]+\\MISSION\.CAM'|Native untiled scene viewport: caller=0x8219c828,")
PHASE_MODES = ('entry', 'gameplay', 'all')
GAME_PHASES = ('intro', 'loading', 'gameplay')


def stamped(lines):
    """(seconds from the first stamp, line) for every stamped line."""
    first = None
    for line in lines:
        m = STAMP.match(line)
        if not m:
            continue
        stamp = datetime.fromisoformat(m.group(1))
        first = first or stamp
        yield (stamp - first).total_seconds(), line


def mission_entry(lines):
    for offset, line in stamped(lines):
        if ENTRY in line:
            return offset
    return None


def segments(lines):
    """Scene/loading segments after entry as [kind, from, to] in seconds from the
    first stamp; the last one ends at the latest stamp."""
    out, thread, last = [], None, None
    for offset, line in stamped(lines):
        last = offset if last is None else max(last, offset)
        if not out:
            if ENTRY in line:
                m = THREAD.search(line)
                thread = m.group(1) if m else None
                out.append(['scene', offset, None])
            continue
        kind = out[-1][0]
        if kind == 'scene' and LOAD.search(line):
            out[-1][2] = offset
            out.append(['loading', offset, None])
        elif kind == 'loading' and thread:
            m = THREAD.search(line)
            if m and m.group(1) == thread:
                out[-1][2] = offset
                out.append(['scene', offset, None])
    if out:
        out[-1][2] = last
    return out


def markers(lines):
    """Seconds from the first stamp to entry, the first loading screen after it
    and gameplay (the first scene after loading); None when absent."""
    segs = segments(lines)
    loading = [s for s in segs if s[0] == 'loading']
    later = [s for s in segs[1:] if s[0] == 'scene']
    return dict(entry=segs[0][1] if segs else None, load=loading[0][1] if loading else None,
                gameplay=later[0][1] if later else None, loading_screens=len(loading))


def phase_windows(segs, start, end):
    """Intervals per game phase, in seconds from the first stamp."""
    windows = dict(intro=[], loading=[], gameplay=[])
    if not segs:
        return windows
    windows['intro'] = [[segs[0][1] + start, segs[0][2]]] if segs[0][2] >= segs[0][1] + start else []
    windows['loading'] = [[a, b] for kind, a, b in segs if kind == 'loading']
    later = [s for s in segs[1:] if s[0] == 'scene']
    if later:
        lo, hi = later[0][1] + start, later[0][1] + end
        windows['gameplay'] = [[max(a, lo), min(b, hi)] for _, a, b in later if min(b, hi) > max(a, lo)]
    return windows


def in_windows(t, span, windows):
    return any(a <= t - span and t <= b for a, b in windows)


def fps_samples(lines):
    """(stamp, span, fps) for every FPS line."""
    return [(t, float(m.group(2)), float(m.group(1))) for t, line in stamped(lines) for m in [FPS.search(line)] if m]


def fps_stats(values):
    return dict(fps_samples=len(values), fps_median=statistics.median(values) if values else None,
                fps_min=min(values) if values else None)


def phase_fps(lines, start, end):
    """FPS per game phase; a sample counts only when its whole span lies in one phase segment."""
    samples = fps_samples(lines)
    out = {}
    for phase, windows in phase_windows(segments(lines), start, end).items():
        row = fps_stats([v for t, span, v in samples if in_windows(t, span, windows)])
        row['windows_s'] = [[round(a, 1), round(b, 1)] for a, b in windows]
        out[phase] = row
    return out


def summarize(path, start, end, anchor='entry'):
    """Window metrics. anchor 'entry' places FPS samples by their t= field (the
    original gate); anchor 'gameplay' keeps samples whose whole span lies in a
    gameplay segment, so loading screens inside the window are excluded."""
    fps, samples, repeats, groups, errors, hooks = [], 0, 0, {}, 0, {}
    in_window = False
    lines = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    entry = mission_entry(lines)
    marks = markers(lines)
    marks_s = {k: v if v is None or k == 'loading_screens' else round(v, 1) for k, v in marks.items()}
    origin = entry if anchor == 'entry' else marks['gameplay']
    if origin is None:
        return dict(log=str(path), mission_entry_s=None if entry is None else round(entry, 1), markers_s=marks_s,
                    fps_samples=0, fps_median=None, fps_min=None, host_presents=0, host_repeated_images=0,
                    nonempty_groups=0, native_groups=0, errors=0, hook_phases={})
    start, end = origin + start, origin + end
    gameplay = phase_windows(segments(lines), start - origin, end - origin)['gameplay']
    offset = None
    first = None
    for line in lines:
        st = STAMP.match(line)
        if st and anchor != 'entry':
            stamp = datetime.fromisoformat(st.group(1))
            first = first or stamp
            offset = (stamp - first).total_seconds()
        m = FPS.search(line)
        if m:
            if anchor == 'entry':
                t = int(m.group(3))
                in_window = start <= t <= end
            else:
                in_window = offset is not None and in_windows(offset, float(m.group(2)), gameplay)
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
        mission_entry_s=round(entry, 1) if entry is not None else None,
        markers_s=marks_s,
        **fps_stats(fps),
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


def fps_failure(base, cand, tolerance, label=''):
    """The FPS rule for one window: candidate median not below baseline minus tolerance."""
    prefix = f'{label}: ' if label else ''
    if not base['fps_samples'] or not cand['fps_samples']:
        which = ' and '.join(n for n, r in (('baseline', base), ('candidate', cand)) if not r['fps_samples'])
        if not label:
            return 'no FPS samples in the window; the run did not reach it'
        return f'{prefix}no FPS samples in the {which} window; the run did not reach it'
    if cand['fps_median'] < base['fps_median'] * (1 - tolerance):
        return f"{prefix}median FPS {cand['fps_median']:.1f} is below baseline {base['fps_median']:.1f}"
    return None


def choose_mode(requested, base_marks, cand_marks):
    if requested:
        return requested, 'requested with --phase'
    missing = [n for n, m in (('baseline', base_marks), ('candidate', cand_marks)) if m['load'] is None]
    if not missing:
        return 'all', 'loading marker after entry found in both logs'
    return 'entry', f"no loading marker after entry in the {' and '.join(missing)} log"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('candidate')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--start', type=int, default=10, help='seconds after the phase anchor where measuring starts')
    ap.add_argument('--end', type=int, default=150, help='seconds after the phase anchor where measuring ends')
    ap.add_argument('--phase', choices=PHASE_MODES, default=None,
                    help='entry, gameplay or all (default: all when both logs have the mission-load marker, '
                         'else entry)')
    ap.add_argument('--tolerance', type=float, default=0.05, help='allowed fractional FPS drop')
    ap.add_argument('--min-native-groups', type=int, default=0)
    ap.add_argument('--max-phase', type=phase_limit, action='append', default=[], metavar='PHASE=MS',
                    help='fail when the candidate phase costs more than MS per frame (repeatable)')
    ap.add_argument('--expect-drop', action='append', default=[], metavar='PHASE',
                    help='fail unless the candidate phase costs less than the baseline (repeatable)')
    args = ap.parse_args()

    base_lines = Path(args.baseline).read_text(encoding='utf-8', errors='replace').splitlines()
    cand_lines = Path(args.candidate).read_text(encoding='utf-8', errors='replace').splitlines()
    mode, reason = choose_mode(args.phase, markers(base_lines), markers(cand_lines))
    print(f'renderer-runtime-gate: phase mode {mode} ({reason})', file=sys.stderr)

    anchor = 'gameplay' if mode == 'gameplay' else 'entry'
    base = summarize(args.baseline, args.start, args.end, anchor)
    cand = summarize(args.candidate, args.start, args.end, anchor)
    extra = [phase for phase, _ in args.max_phase] + args.expect_drop
    phases = compare_phases(base.pop('hook_phases'), cand.pop('hook_phases'), extra)

    fps_keys = ('fps_median', 'fps_min', 'fps_samples')
    if mode == 'all':
        base_phases = phase_fps(base_lines, args.start, args.end)
        cand_phases = phase_fps(cand_lines, args.start, args.end)
        phases_fps = {p: dict(windows_s=dict(baseline=base_phases[p]['windows_s'],
                                             candidate=cand_phases[p]['windows_s']),
                              baseline={k: base_phases[p][k] for k in fps_keys},
                              candidate={k: cand_phases[p][k] for k in fps_keys})
                      for p in GAME_PHASES}
    else:
        phases_fps = {mode: dict(windows_s=None, baseline={k: base[k] for k in fps_keys},
                                 candidate={k: cand[k] for k in fps_keys})}

    failures = []
    if mode == 'all':
        for p, row in phases_fps.items():
            failure = fps_failure(row['baseline'], row['candidate'], args.tolerance, p)
            if failure:
                failures.append(failure)
    else:
        failure = fps_failure(base, cand, args.tolerance)
        if failure:
            failures.append(failure)
    if cand['native_groups'] < args.min_native_groups:
        failures.append(f"{cand['native_groups']} fully native static groups, need {args.min_native_groups}")
    failures += phase_failures(phases, args.max_phase, args.expect_drop)
    report = dict(window_after_entry=[args.start, args.end], phase_mode=mode, phase_mode_reason=reason,
                  baseline=base, candidate=cand, phases=phases, phases_fps=phases_fps,
                  passed=not failures, failures=failures)
    if mode == 'gameplay':
        report['window_after_gameplay'] = [args.start, args.end]
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
