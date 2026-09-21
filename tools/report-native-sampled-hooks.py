"""Estimate inclusive hook costs from complete sampled-report windows."""
import argparse
from collections import defaultdict
import datetime
import json
from pathlib import Path
import re

SAMPLE = re.compile(r'\[t(\d+)\].*Native sampled hook timing: phase=(\S+) samples=(\d+) period=(\d+) total_ms=([\d.]+)')


def summarize(log, start, end):
    rotations = sorted(log.parent.glob(log.stem + '.*' + log.suffix),
                       key=lambda path: int(path.stem.rsplit('.', 1)[1]), reverse=True)
    origin = None
    previous, totals = {}, defaultdict(lambda: [0, 0.0, 0.0, 0])
    for source in rotations + [log]:
        for line in source.read_text(encoding='utf-8-sig').splitlines():
            if not line.startswith('['):
                continue
            try:
                stamp = datetime.datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
            except ValueError:
                continue
            fps = re.search(r'FPS: .* t=(\d+) s\)', line)
            if fps and origin is None:
                origin = stamp - int(fps[1])
            match = SAMPLE.search(line)
            if not match or origin is None:
                continue
            thread, phase, samples, period, milliseconds = match.groups()
            elapsed = stamp - origin
            key = thread, phase, int(period)
            before = previous.get(key)
            previous[key] = elapsed
            if before is None or before < start or elapsed > end or elapsed <= before:
                continue
            total = totals[key]
            total[0] += int(samples)
            total[1] += float(milliseconds) * int(period)
            total[2] += elapsed - before
            total[3] += 1
    phases = defaultdict(lambda: {'samples': 0, 'reports': 0, 'sampled_ms': 0.0, 'estimated_ms_per_60hz_frame': 0.0})
    for (_, phase, period), (samples, milliseconds, seconds, reports) in totals.items():
        item = phases[phase]
        item['samples'] += samples
        item['reports'] += reports
        item['sampled_ms'] += milliseconds / period
        item['estimated_ms_per_60hz_frame'] += milliseconds / seconds / 60
    for item in phases.values():
        item['microseconds_per_call'] = item['sampled_ms'] * 1000 / item['samples']
    return {'log': str(log), 'game_seconds': [start, end],
            'phases': dict(sorted(phases.items(), key=lambda pair: pair[1]['estimated_ms_per_60hz_frame'], reverse=True)),
            'limitation': 'Sampled estimates, inclusive of nested scopes; phases must not be summed. Missing phases have no complete report window.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--start', type=float, default=140)
    parser.add_argument('--end', type=float, default=180)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.start < 0 or args.end <= args.start:
        parser.error('end must exceed start, and start must be nonnegative')
    result = json.dumps(summarize(args.log, args.start, args.end), indent=2) + '\n'
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    else:
        print(result, end='')


if __name__ == '__main__':
    main()
