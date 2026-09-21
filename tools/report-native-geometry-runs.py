#!/usr/bin/env python3
"""Compare stopped native-renderer runs without counting warm-up samples.

Example: python tools/report-native-geometry-runs.py four.log direct.log --output report.json
FPS covers complete reporting intervals within --start..--end game seconds.
Hook costs are inclusive wall time per call, aggregated from complete per-thread
reporting buckets in that window; nested phases must not be added together.
"""
import argparse
import datetime
import json
import re
from pathlib import Path

FPS = re.compile(r'FPS: ([\d.]+) \(frames (\d+) over ([\d.]+) s, t=(\d+) s\)')
HOOK = re.compile(r'Native hook timing: phase=(\S+) calls=(\d+) total_ms=([\d.]+)')
WORKERS = re.compile(r'Native geometry workers: draws=(\d+), batches=(\d+), worker_mask=(0x[\da-f]+), max_concurrent=(\d+), recording_cpu_ms=([\d.]+), wait_ms=([\d.]+)')
PHASES = {'indexed.native', 'indexed.mesh', 'indexed.bindings', 'activation.native',
          'immediate.native', 'submission.flush', 'swap.gpu_wait', 'mesh.lookup',
          'mesh.observe', 'mesh.acquire'}

def inspect(path, start, end, phases=PHASES, thread_id=None):
    samples, timings, prior, workers = [], {}, {}, []
    origin = None
    errors = 0
    # The logger rotates at 5 MB. Older numbered files precede the active
    # file; reading only game.log can silently lose the measurement window.
    rotated = []
    pattern = re.compile(re.escape(path.stem) + r'\.(\d+)' + re.escape(path.suffix))
    for candidate in path.parent.glob(path.stem + '.*' + path.suffix):
        match = pattern.fullmatch(candidate.name)
        if match:
            rotated.append((int(match.group(1)), candidate))
    files = [candidate for _, candidate in sorted(rotated, reverse=True)] + [path]
    lines = [line for source in files for line in source.read_text(
        encoding='utf-8-sig', errors='replace').splitlines()]
    for line in lines:
        errors += '[error]' in line
        if not line.startswith('['):
            continue
        try:
            stamp = datetime.datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
        except ValueError:
            continue
        match = FPS.search(line)
        if match:
            fps, frames, seconds, elapsed = map(float, match.groups())
            if origin is None:
                origin = stamp - elapsed
            if elapsed - seconds >= start and elapsed <= end:
                samples.append((fps, frames, seconds))
        if origin is None:
            continue
        elapsed = stamp - origin
        match = HOOK.search(line)
        if match:
            phase, calls, milliseconds = match.groups()
            thread = re.search(r'\[t(\d+)\]', line)
            key = (thread.group(1) if thread else '', phase)
            previous = prior.get(key)
            prior[key] = elapsed
            if ((phases is None or phase in phases) and
                    (thread_id is None or key[0] == str(thread_id)) and
                    previous is not None and previous >= start and elapsed <= end):
                item = timings.setdefault(phase, {'calls': 0, 'milliseconds': 0.0})
                item['calls'] += int(calls)
                item['milliseconds'] += float(milliseconds)
        match = WORKERS.search(line)
        if match and start <= elapsed <= end:
            draws, batches, mask, concurrent, cpu, wait = match.groups()
            workers.append({'elapsed': elapsed, 'draws': int(draws), 'batches': int(batches),
                            'mask': int(mask, 16), 'max_concurrent': int(concurrent),
                            'cpu_ms': float(cpu), 'wait_ms': float(wait)})
    for item in timings.values():
        item['microseconds_per_call'] = item['milliseconds'] * 1000 / item['calls']
    seconds = sum(sample[2] for sample in samples)
    result = {'log': str(path), 'log_files': [str(source) for source in files],
              'window_game_seconds': [start, end],
              'fps_samples': len(samples), 'fps_sample_seconds': seconds,
              'fps': sum(sample[1] for sample in samples) / seconds if seconds else None,
              'hooks': timings, 'logged_errors': errors}
    if thread_id is not None:
        result['hook_thread'] = int(thread_id)
    if len(workers) >= 2:
        first, last = workers[0], workers[-1]
        batches = last['batches'] - first['batches']
        result['workers'] = {'sample_seconds': last['elapsed'] - first['elapsed'],
                             'batches': batches, 'worker_mask': hex(last['mask']),
                             'max_concurrent': last['max_concurrent'],
                             'recording_cpu_ms': last['cpu_ms'] - first['cpu_ms'],
                             'producer_wait_ms': last['wait_ms'] - first['wait_ms']}
        if batches:
            result['workers']['wait_ms_per_batch'] = result['workers']['producer_wait_ms'] / batches
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--start', type=float, default=90)
    parser.add_argument('--end', type=float, default=150)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--all-phases', action='store_true', help='include every recorded hook phase (inclusive timings overlap)')
    parser.add_argument('--thread', type=int, help='restrict hook timings to this logged thread ID; FPS and worker totals remain process-wide')
    args = parser.parse_args()
    if args.start < 0 or args.end <= args.start:
        parser.error('end must exceed start, and start must be nonnegative')
    report = json.dumps([inspect(path, args.start, args.end, None if args.all_phases else PHASES, args.thread)
                         for path in args.logs], indent=2) + '\n'
    if args.output:
        args.output.write_text(report, encoding='utf-8')
    else:
        print(report, end='')

if __name__ == '__main__':
    main()
