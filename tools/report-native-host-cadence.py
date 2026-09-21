"""Report game FPS and host Present-return cadence for complete log windows."""
import argparse
import datetime
import json
import re
import runpy
from pathlib import Path


def summarize(log, start, end):
    helper = runpy.run_path(str(Path(__file__).with_name('report-native-geometry-runs.py')))
    game = helper['inspect'](log, start, end)
    origin = None
    previous = None
    records = []
    fps_samples = []
    for source in game['log_files']:
        for line in Path(source).read_text(encoding='utf-8-sig').splitlines():
            if not line.startswith('['):
                continue
            try:
                stamp = datetime.datetime.strptime(line[1:24], '%Y-%m-%d %H:%M:%S.%f').timestamp()
            except ValueError:
                continue
            fps = helper['FPS'].search(line)
            if fps:
                rate, frames, duration, elapsed = map(float, fps.groups())
                if origin is None:
                    origin = stamp - elapsed
                if elapsed - duration >= start and elapsed <= end:
                    fps_samples.append(rate)
            if origin is None or 'Native D3D12 host pacing:' not in line:
                continue
            elapsed = stamp - origin
            if previous is not None and previous >= start and elapsed <= end:
                fields = dict(re.findall(r'(\w+)=([\d.]+)', line))
                records.append({key: float(value) for key, value in fields.items()})
            previous = elapsed
    samples = int(sum(item['samples'] for item in records))
    result = {'log': str(log), 'game_seconds': [start, end],
              'game_fps': game['fps'], 'fps_samples': len(fps_samples),
              'lowest_fps_sample': min(fps_samples) if fps_samples else None,
              'logged_errors': game['logged_errors'], 'host_reports': len(records),
              'host_samples': samples}
    if samples:
        result.update({
            'repeated_images': int(sum(item['repeated_images'] for item in records)),
            'skipped_sequences': int(sum(item['skipped_sequences'] for item in records)),
            'mean_interval_ms': sum(item['interval_avg_ms'] * item['samples'] for item in records) / samples,
            'maximum_interval_ms': max(item['interval_max_ms'] for item in records),
            'maximum_acquire_copy_ms': max(item['acquire_copy_max_ms'] for item in records)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--start', type=float, default=140)
    parser.add_argument('--end', type=float, default=440)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.start < 0 or args.end <= args.start:
        parser.error('end must exceed start, and start must be nonnegative')
    report = json.dumps(summarize(args.log, args.start, args.end), indent=2) + '\n'
    if args.output:
        args.output.write_text(report, encoding='utf-8')
    else:
        print(report, end='')


if __name__ == '__main__':
    main()
