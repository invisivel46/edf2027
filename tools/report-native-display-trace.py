"""Summarize DXGI display feedback without mistaking missing observations for drops."""
import argparse
from collections import Counter
import csv
import datetime
import json
import io
from pathlib import Path
import re
import statistics


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
        raise ValueError('No FPS timestamp to align display feedback')
    # A live trace can end halfway through a buffered row. Only parse complete
    # lines from this snapshot; do not silently ignore malformed complete rows.
    contents = trace.read_text(encoding='utf-8-sig')
    if not contents.endswith('\n'):
        contents = contents.rsplit('\n', 1)[0] + '\n'
    rows = [dict((key, int(value)) for key, value in row.items())
            for row in csv.DictReader(io.StringIO(contents))
            if start <= int(row['epoch_ms']) / 1000 - origin <= end]
    if not rows:
        raise ValueError('No display feedback in requested window')
    statuses = Counter(f"0x{row['statistics_status'] & 0xffffffff:08x}" for row in rows)
    steps, refresh_spans = Counter(), Counter()
    previous = None
    unchanged = resets = sync_ticks = sync_refreshes = 0
    refresh_rates = []
    for row in rows:
        if row['statistics_status'] < 0 or row['sync_qpc'] == 0:
            previous = None
            continue
        if previous is not None:
            dp = (row['present_count'] - previous['present_count']) & 0xffffffff
            dr = (row['present_refresh_count'] - previous['present_refresh_count']) & 0xffffffff
            ds = (row['sync_refresh_count'] - previous['sync_refresh_count']) & 0xffffffff
            dq = row['sync_qpc'] - previous['sync_qpc']
            if dp > 0x7fffffff or dr > 0x7fffffff or ds > 0x7fffffff or dq < 0:
                resets += 1
            else:
                if ds and dq:
                    sync_ticks += dq
                    sync_refreshes += ds
                    refresh_rates.append(ds * row['qpc_frequency'] / dq)
                if dp == 0:
                    unchanged += 1
                else:
                    steps[dp] += 1
                    # Wider steps may hide intermediate frames. Do not call
                    # those skipped images or turn their sum into one interval.
                    if dp == 1:
                        refresh_spans[dr] += 1
        previous = row
    return {
        'trace': str(trace), 'game_seconds': [start, end], 'samples': len(rows),
        'observed_game_seconds': [rows[0]['epoch_ms'] / 1000 - origin,
                                  rows[-1]['epoch_ms'] / 1000 - origin],
        'statistics_statuses': dict(statuses), 'unchanged_observations': unchanged,
        'counter_resets': resets, 'observed_present_count_steps': dict(sorted(steps.items())),
        'refresh_spans_for_adjacent_present_counts': dict(sorted(refresh_spans.items())),
        'feedback_refresh_hz': sync_refreshes * rows[0]['qpc_frequency'] / sync_ticks if sync_ticks else None,
        'median_feedback_refresh_hz': statistics.median(refresh_rates) if refresh_rates else None,
        'limitation': 'DXGI feedback may be unavailable or incomplete; observation gaps are not proof of dropped images.'
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('log', type=Path)
    parser.add_argument('--start', type=float, default=140)
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
