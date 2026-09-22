"""Runtime gate regressions on synthetic game.log files: no game needed."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('renderer_runtime_gate', ROOT / 'tools/renderer-runtime-gate.py')
gate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gate)


def stamp(seconds):
    whole = int(seconds)
    return f'[2026-09-22 10:{whole // 60:02d}:{whole % 60:02d}.{int(round((seconds - whole) * 1000)):03d}]'


def line(seconds, text, level='info'):
    return f'{stamp(seconds)} [{level}] [t1] {text}'


def fps(seconds, value, t):
    return line(seconds, f'FPS: {value} (frames {int(value * 5)} over 5.0 s, t={t} s)')


def group(seconds, address, batches, calls):
    return line(seconds, f'Native static group execution: group={address} completed=true '
                         f'recorded_batches={batches} compatibility_calls={calls}')


ENTRY = 'Native indexed input: draw=1, vertices=3'


class GateLogTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, lines):
        path = Path(self.tmp.name) / name
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return path

    def run_main(self, candidate, baseline, *extra):
        out = io.StringIO()
        argv = ['gate', str(candidate), '--baseline', str(baseline), *extra]
        with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(out):
            code = gate.main()
        return code, json.loads(out.getvalue())

    def test_mission_entry_is_seconds_from_first_stamp_to_first_scene_draw(self):
        lines = ['no stamp on this line', line(2.0, 'boot'), line(30.5, 'loading'),
                 line(44.25, ENTRY), line(50.0, 'Native indexed input: draw=1, vertices=9')]
        self.assertAlmostEqual(gate.mission_entry(lines), 42.25)

    def test_missing_entry_reports_no_samples_and_fails(self):
        base = self.write('base.log', [line(0, 'boot'), line(40, ENTRY), fps(60, 60.0, 60)])
        cand = self.write('cand.log', [line(0, 'boot'), fps(60, 60.0, 60), line(61, 'died', 'critical')])
        self.assertIsNone(gate.mission_entry(cand.read_text().splitlines()))
        summary = gate.summarize(cand, 10, 150)
        self.assertIsNone(summary['mission_entry_s'])
        self.assertEqual(summary['fps_samples'], 0)
        self.assertIsNone(summary['fps_median'])
        code, report = self.run_main(cand, base)
        self.assertEqual(code, 1)
        self.assertFalse(report['passed'])
        self.assertTrue(any('no FPS samples' in f for f in report['failures']))

    def test_window_is_relative_to_mission_entry(self):
        # Entry 30 s after the first stamp, so the 10..150 window is t=40..180.
        log = self.write('game.log', [
            line(0, 'boot'), line(30, ENTRY),
            fps(35, 10.0, 35), fps(39, 11.0, 39),
            fps(40, 50.0, 40), fps(100, 60.0, 100), fps(180, 70.0, 180),
            fps(181, 99.0, 181),
        ])
        summary = gate.summarize(log, 10, 150)
        self.assertEqual(summary['mission_entry_s'], 30.0)
        self.assertEqual(summary['fps_samples'], 3)
        self.assertEqual(summary['fps_min'], 50.0)

    def test_fps_median_and_regression_verdict(self):
        base = self.write('base.log', [line(0, 'boot'), line(20, ENTRY)] +
                          [fps(30 + i, v, 30 + i) for i, v in enumerate([58.0, 60.0, 62.0, 90.0])])
        good = self.write('good.log', [line(0, 'boot'), line(50, ENTRY)] +
                          [fps(60 + i, v, 60 + i) for i, v in enumerate([59.0, 58.0, 1.0])])
        bad = self.write('bad.log', [line(0, 'boot'), line(50, ENTRY)] +
                         [fps(60 + i, v, 60 + i) for i, v in enumerate([40.0, 50.0, 55.0])])
        self.assertEqual(gate.summarize(base, 10, 150)['fps_median'], 61.0)
        self.assertEqual(gate.summarize(good, 10, 150)['fps_median'], 58.0)
        code, report = self.run_main(good, base)  # 58 >= 61 * 0.95
        self.assertEqual(code, 0, report['failures'])
        code, report = self.run_main(bad, base)
        self.assertEqual(code, 1)
        self.assertIn('below baseline', report['failures'][0])

    def test_native_group_needs_batches_and_no_compatibility_calls(self):
        log = self.write('game.log', [
            line(0, 'boot'), line(10, ENTRY),
            fps(15, 60.0, 15),                    # before the window
            group(15.5, '0x100', 9, 0),           # outside: ignored
            fps(20, 60.0, 20),                    # window opens
            group(21, '0x1', 5, 0),               # native
            group(21, '0x2', 3, 2),               # nonempty, compatibility
            group(21, '0x3', 0, 0),               # empty: not counted
            group(21, '0x4', 4, 1),
            group(22, '0x4', 4, 0),               # native once it has a clean frame
            group(22, '0x1', 5, 3),               # stays native
            fps(200, 60.0, 200),                  # window closed
            group(201, '0x5', 7, 0),              # outside: ignored
        ])
        summary = gate.summarize(log, 10, 150)
        self.assertEqual(summary['nonempty_groups'], 3)
        self.assertEqual(summary['native_groups'], 2)
        code, report = self.run_main(log, log, '--min-native-groups', '3')
        self.assertEqual(code, 1)
        self.assertIn('2 fully native static groups, need 3', report['failures'])


if __name__ == '__main__':
    unittest.main()
