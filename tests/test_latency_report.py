"""latency-report regressions on synthetic game.log and CSV files: no game needed."""
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('latency_report', ROOT / 'tools/latency-report.py')
report = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(report)


def stamp(seconds):
    whole = int(seconds)
    return f'[2026-09-23 10:{whole // 60:02d}:{whole % 60:02d}.000]'


def window(seconds, kind='mouse', n=10, total=(40.0, 50.0, 60.0, 70.0), input_tick=(8.0, 14.0, 16.0, 16.6),
           low_latency=0, vsync=1, estimated=0, ui=(90.0, 7.5), expired=0):
    # The exact shape of edf::latency::FormatReport plus the span/in_flight suffix of Collect
    # (src/input_latency.cpp): stage=p50/p90/p99/max in milliseconds, two decimals.
    stages = {name: (1.0, 2.0, 3.0, 4.0) for name in report.STAGES}
    stages['input_tick'] = input_tick
    stages['total'] = total
    fields = ' '.join(f'{name}={"/".join(f"{v:.2f}" for v in stages[name])}' for name in report.STAGES)
    return (f'{stamp(seconds)} [info] [core] [t1] Input latency: kind={kind} n={n} {fields} '
            f'photon_estimated={estimated} ui_block_pct={ui[0]:.1f} ui_block_delay_ms={ui[1]:.2f} '
            f'low_latency={low_latency} vsync={vsync} expired={expired} discarded=0 overflow=0 '
            f'span_ms=5000 in_flight=3')


def csv_text(rows):
    header = ','.join(report.CSV_FIELDS)
    body = '\n'.join(','.join(str(v) for v in row) for row in rows)
    return header + '\n' + body + '\n'


def csv_row(kind, input_ns, stage_ms, low_latency=1, vsync=1, estimated=0):
    """A CSV row whose six stages take `stage_ms` milliseconds each, in order."""
    t = [input_ns]
    for ms in stage_ms:
        t.append(t[-1] + int(ms * 1e6))
    #      kind input  tick tick_ns record seq  submit acquire present_id present photon est low vsync
    return [kind, t[0], 7, t[1], t[2], 40, t[3], t[4], 500, t[5], t[6], estimated, low_latency, vsync]


class LatencyReportTests(unittest.TestCase):
    def test_log_line_matches_the_cpp_format(self):
        # FormatReport's test line (tests/input_latency_tests.cpp) with Collect's suffix.
        line = ('Input latency: kind=mouse n=1 input_tick=1.00/1.00/1.00/1.00 tick_record=1.00/1.00/1.00/1.00 '
                'record_submit=1.00/1.00/1.00/1.00 submit_acquire=1.00/1.00/1.00/1.00 '
                'acquire_present=1.00/1.00/1.00/1.00 present_photon=1.00/1.00/1.00/1.00 total=6.00/6.00/6.00/6.00 '
                'photon_estimated=0 ui_block_pct=50.0 ui_block_delay_ms=4.00 low_latency=1 vsync=1 expired=1 '
                'discarded=2 overflow=3 span_ms=5000 in_flight=0')
        result = report.from_log([line])
        row = result['mouse low_latency=1 vsync=1']
        self.assertEqual(row['n'], 1)
        self.assertEqual(row['stages']['total']['p50'], 6.0)
        self.assertEqual((row['expired'], row['discarded'], row['overflow']), (1, 2, 3))

    def test_windows_merge_by_sample_count(self):
        lines = [window(5, n=10, total=(40, 50, 60, 70)), window(10, n=30, total=(80, 90, 100, 120)),
                 window(15, n=0, total=(0, 0, 0, 0)), 'unrelated line']
        row = report.from_log(lines)['mouse low_latency=0 vsync=1']
        self.assertEqual(row['n'], 40)
        self.assertEqual(row['windows'], 3)
        self.assertAlmostEqual(row['stages']['total']['p50'], (40 * 10 + 80 * 30) / 40)
        self.assertAlmostEqual(row['stages']['total']['p99'], (60 * 10 + 100 * 30) / 40)
        self.assertEqual(row['stages']['total']['max'], 120)
        self.assertFalse(row['exact'])

    def test_rows_split_by_kind_and_setting(self):
        lines = [window(5, low_latency=0), window(10, low_latency=1, total=(20, 25, 30, 35)),
                 window(15, kind='key', n=2)]
        result = report.from_log(lines)
        self.assertEqual(sorted(result),
                         ['key low_latency=0 vsync=1', 'mouse low_latency=0 vsync=1', 'mouse low_latency=1 vsync=1'])
        self.assertEqual(result['mouse low_latency=1 vsync=1']['stages']['total']['p50'], 20)

    def test_csv_percentiles_are_exact_nearest_rank(self):
        rows = [csv_row('mouse', 1_000_000_000 + i * 1_000_000, (i + 1, 2, 3, 4, 1, 5)) for i in range(100)]
        rows.append(csv_row('key', 5_000_000_000, (1, 1, 1, 1, 1, 1), estimated=1))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'latency.csv'
            path.write_text(csv_text(rows), encoding='utf-8')
            result = report.load(path)
        mouse = result['mouse low_latency=1 vsync=1']
        self.assertTrue(mouse['exact'])
        self.assertEqual(mouse['n'], 100)
        tick = mouse['stages']['input_tick']
        self.assertEqual((tick['p50'], tick['p90'], tick['p99'], tick['max']), (50.0, 90.0, 99.0, 100.0))
        self.assertAlmostEqual(mouse['stages']['total']['p50'], 50 + 2 + 3 + 4 + 1 + 5)
        self.assertEqual(result['key low_latency=1 vsync=1']['photon_estimated'], 1)

    def test_csv_with_wrong_header_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'other.csv'
            path.write_text('a,b\n1,2\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                report.load(path)

    def test_rotated_log_parts_are_read(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / 'game.1.log').write_text(window(5, n=10) + '\n', encoding='utf-8')
            (base / 'game.log').write_text(window(10, n=5) + '\n', encoding='utf-8')
            result = report.load(base / 'game.log')
        self.assertEqual(result['mouse low_latency=0 vsync=1']['n'], 15)

    def test_compare_before_after(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            before, after = base / 'before.log', base / 'after.log'
            before.write_text(window(5, total=(117, 125, 133, 140)) + '\n', encoding='utf-8')
            after.write_text(window(5, total=(40, 45, 50, 55)) + '\n', encoding='utf-8')
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = report.main([str(before), str(after), '--compare', '--json'])
        self.assertEqual(code, 0)
        data = json.loads(output.getvalue())
        delta = data['compare']['mouse low_latency=0 vsync=1']['total']
        self.assertEqual((delta['p50'], delta['p99']), (-77.0, -83.0))

    def test_text_output_and_empty_log(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'game.log'
            path.write_text('no trace here\n', encoding='utf-8')
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = report.main([str(path)])
        self.assertEqual(code, 1)
        self.assertIn('no "Input latency:" samples', output.getvalue())

    def test_compare_needs_two_inputs(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            report.main(['one.log', '--compare'])


if __name__ == '__main__':
    unittest.main()
