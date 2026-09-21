"""Summarize coarse swap traces aligned to the game's FPS clock (not scanout)."""
import argparse
import csv
import datetime
import json
import re
import statistics
from pathlib import Path


def summarize(trace, log, start, end):
    rotations = sorted(log.parent.glob(log.stem + '.*' + log.suffix),
                       key=lambda path: int(path.stem.rsplit('.', 1)[1]), reverse=True)
    origin = None
    for source in rotations + [log]:
        for line in source.read_text(encoding='utf-8-sig').splitlines():
            match = re.search(r'FPS: .* t=(\d+) s\)', line)
            if match:
                stamp = datetime.datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
                origin = stamp - int(match[1])
                break
        if origin is not None:
            break
    if origin is None:
        raise ValueError('No FPS timestamp to align the trace with gameplay')
    with trace.open(encoding='utf-8-sig', newline='') as source:
        rows = [row for row in csv.DictReader(source)
                if start <= float(row['epoch_ms']) / 1000 - origin <= end]
    if not rows:
        raise ValueError('No frame samples in requested window')
    result = {'trace': str(trace), 'log': str(log), 'game_seconds': [start, end],
              'samples': len(rows), 'phases': {}}
    for name in ('interval_ms', 'between_swaps_ms', 'submit_ms', 'gpu_wait_ms', 'pacing_ms',
                 'engine_wait_ms', 'guest_fence_sleep_ms', 'shared_slot_wait_ms', 'backend_frame_wait_ms'):
        if name not in rows[0]:
            continue  # Older coarse traces contain only the five original columns.
        values = sorted(float(row[name]) for row in rows)
        percentile = lambda fraction: values[round(fraction * (len(values) - 1))]
        result['phases'][name] = {'mean': statistics.mean(values), 'p50': percentile(.5),
                                  'p95': percentile(.95), 'p99': percentile(.99), 'max': values[-1]}
    intervals = [float(row['interval_ms']) for row in rows]
    result['swap_entry_rate_hz'] = 1000 / statistics.mean(intervals)
    result['intervals_over_16_667_ms'] = sum(value > 1000 / 60 for value in intervals)
    if 'engine_extra_steps' in rows[0]:
        extra = [int(row['engine_extra_steps']) for row in rows]
        result['simulation_catchup'] = {
            'extra_steps': sum(extra),
            'swap_windows_with_extra_steps': sum(value > 0 for value in extra),
            'maximum_extra_steps_in_window': max(extra)}
    if 'process_cpu_ms' in rows[0]:
        result['cpu_accounting'] = {
            name: {'total_ms': sum(float(row[name]) for row in rows),
                   'mean_ms_per_swap': statistics.mean(float(row[name]) for row in rows)}
            for name in ('process_cpu_ms', 'swap_thread_cpu_ms')}
        result['longest_swap_windows'] = [
            {name: float(row[name]) for name in
             ('epoch_ms', 'interval_ms', 'between_swaps_ms', 'process_cpu_ms',
              'swap_thread_cpu_ms', 'swap_thread_id', 'engine_extra_steps')}
            for row in sorted(rows, key=lambda row: float(row['interval_ms']), reverse=True)[:10]]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('log', type=Path)
    parser.add_argument('--start', type=float, default=160)
    parser.add_argument('--end', type=float, default=210)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.start < 0 or args.end <= args.start:
        parser.error('end must exceed start, and start must be nonnegative')
    report = json.dumps(summarize(args.trace, args.log, args.start, args.end), indent=2) + '\n'
    if args.output:
        args.output.write_text(report, encoding='utf-8')
    else:
        print(report, end='')


if __name__ == '__main__':
    main()
