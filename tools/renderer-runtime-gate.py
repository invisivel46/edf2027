"""Runtime acceptance gate for the native renderer.

Compares a candidate game.log against a baseline game.log from the same input
script. Passes only when the candidate's game FPS in the measured window is
not worse than the baseline and, when --min-native-groups is set, enough
nonempty static groups executed with zero compatibility calls.

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
    fps, samples, repeats, groups, errors = [], 0, 0, {}, 0
    in_window = False
    lines = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    entry = mission_entry(lines)
    if entry is None:
        return dict(log=str(path), mission_entry_s=None, fps_samples=0, fps_median=None, fps_min=None,
                    host_presents=0, host_repeated_images=0, nonempty_groups=0, native_groups=0, errors=0)
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
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('candidate')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--start', type=int, default=10, help='seconds after mission entry where measuring starts')
    ap.add_argument('--end', type=int, default=150, help='seconds after mission entry where measuring ends')
    ap.add_argument('--tolerance', type=float, default=0.05, help='allowed fractional FPS drop')
    ap.add_argument('--min-native-groups', type=int, default=0)
    args = ap.parse_args()

    base = summarize(args.baseline, args.start, args.end)
    cand = summarize(args.candidate, args.start, args.end)
    failures = []
    if not base['fps_samples'] or not cand['fps_samples']:
        failures.append('no FPS samples in the window; the run did not reach it')
    elif cand['fps_median'] < base['fps_median'] * (1 - args.tolerance):
        failures.append(f"median FPS {cand['fps_median']:.1f} is below baseline {base['fps_median']:.1f}")
    if cand['native_groups'] < args.min_native_groups:
        failures.append(f"{cand['native_groups']} fully native static groups, need {args.min_native_groups}")
    report = dict(window_after_entry=[args.start, args.end], baseline=base, candidate=cand,
                  passed=not failures, failures=failures)
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
