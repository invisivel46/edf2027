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


def hook(seconds, phase, calls, total, maximum, thread=1, sampled=False):
    # The exact REXLOG_INFO formats from HookTiming::Finish in guest_shader_bridge.cpp.
    if sampled:
        text = (f'Native sampled hook timing: phase={phase} samples={calls} period=16 total_ms={total} '
                f'max_ms={maximum} (sampled inclusive CPU wall time)')
    else:
        text = (f'Native hook timing: phase={phase} calls={calls} total_ms={total} max_ms={maximum} '
                f'(inclusive CPU wall time)')
    return f'{stamp(seconds)} [info] [t{thread}] {text}'


ENTRY = 'Native indexed input: draw=1, vertices=3'
# Verbatim shapes from the mission 1 logs of 2026-09-22.
MISSION_CAM = "[NtCreateFile] FAILED: path='GAME:\\MISSION\\M202\\MISSION.CAM' -> 0xc000000f"
PRESENTER = ('Native untiled scene viewport: caller=0x8219c828, requested=1280x720, stored=1280x720, '
             'CPU transform/scissor updated without tile packets')


def tline(seconds, thread, text, level='info'):
    return f'{stamp(seconds)} [{level}] [core] [{thread}] {text}'


def tfps(seconds, value, thread='t3'):
    return tline(seconds, thread, f'FPS: {value} (frames {int(value * 5)} over 5.0 s, t={int(seconds)} s)')


def phased(entry, load, resume, intro, loading, game, end, restart=None):
    """Entry draw on the scene renderer t1, FPS on the main thread t3, a loading
    screen presented by t9 from `load`, and t1 back at `resume`. `restart` adds
    a second loading screen (t8) from restart[0] to restart[1]."""
    out = [tline(0, 't3', 'boot'), tline(entry, 't1', ENTRY)]
    out += [tfps(s, intro) for s in range(entry + 5, load + 1, 5)]
    out += [tline(load, 't3', MISSION_CAM, 'warning'), tline(load + 0.5, 't9', PRESENTER)]
    out += [tfps(s, loading, 't9') for s in range(load + 3, resume, 5)]
    out += [tline(resume, 't1', 'Native Utility draw: submitted=93597')]
    stop = restart[0] if restart else end
    out += [tfps(s, game) for s in range(resume + 5, stop + 1, 5)]
    if restart:
        out += [tline(restart[0] + 0.5, 't8', PRESENTER)]
        out += [tfps(s, loading, 't8') for s in range(restart[0] + 3, restart[1], 5)]
        out += [tline(restart[1], 't1', 'Native scene tree: traversals=9')]
        out += [tfps(s, game) for s in range(restart[1] + 5, end + 1, 5)]
    return out


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

    def timed_log(self, name, model_total, queued_total, entry=10):
        """Two 5 s buckets of 300 frames in the window plus noise outside it."""
        return self.write(name, [
            line(0, 'boot'), line(entry, ENTRY),
            fps(entry + 5, 60.0, entry + 5),
            hook(entry + 5.5, 'render.model', 9999, 9999.0, 99.0),        # before the window
            fps(entry + 10, 60.0, entry + 10),
            hook(entry + 11, 'engine.render_helper', 300, 1500.0, 9.5, thread=2),
            hook(entry + 11, 'render.model', 2400, model_total / 2, 0.4, thread=2),
            hook(entry + 11, 'render.model', 600, model_total / 2, 0.7, thread=5),
            hook(entry + 12, 'render.queued', 300, queued_total, 1.25e-1),
            hook(entry + 12, 'render.model', 50, 5.0, 3.0, sampled=True),  # sampled: ignored
            hook(entry + 16, 'engine.render_helper', 300, 1500.0, 8.0, thread=2),
            fps(entry + 200, 60.0, entry + 200),
            hook(entry + 201, 'render.model', 9999, 9999.0, 99.0),       # after the window
        ])

    def test_phase_costs_per_frame_and_per_call_in_window(self):
        log = self.timed_log('game.log', model_total=900.0, queued_total=60.0)
        hooks = gate.summarize(log, 10, 150)['hook_phases']
        self.assertEqual(hooks['render.model'], dict(calls=3000, total_ms=900.0, max_ms=0.7))
        self.assertEqual(hooks['engine.render_helper']['calls'], 600)
        code, report = self.run_main(log, log)
        self.assertEqual(code, 0, report['failures'])
        phases = report['phases']
        self.assertEqual(phases['metric'], 'ms_per_frame')
        self.assertEqual(phases['frames'], dict(baseline=600, candidate=600))
        model = phases['by_phase']['render.model']
        self.assertAlmostEqual(model['candidate']['ms_per_frame'], 1.5)
        self.assertAlmostEqual(model['candidate']['ms_per_call'], 0.3)
        self.assertEqual(model['delta'], 0.0)
        self.assertEqual(set(gate.PHASES), set(phases['by_phase']))
        self.assertIsNone(phases['by_phase']['render.gather']['candidate'])
        self.assertIsNone(phases['by_phase']['render.gather']['delta'])
        self.assertNotIn('hook_phases', report['candidate'])

    def test_phase_limits_and_expected_drops(self):
        base = self.timed_log('base.log', model_total=900.0, queued_total=60.0)
        cand = self.timed_log('cand.log', model_total=600.0, queued_total=90.0, entry=40)
        code, report = self.run_main(cand, base, '--max-phase', 'render.model=1.0',
                                     '--expect-drop', 'render.model')
        self.assertEqual(code, 0, report['failures'])
        self.assertAlmostEqual(report['phases']['by_phase']['render.model']['delta_ms_per_frame'], -0.5)
        self.assertAlmostEqual(report['phases']['by_phase']['render.queued']['delta'], 0.05)
        code, report = self.run_main(cand, base, '--max-phase', 'render.model=0.9',
                                     '--max-phase', 'render.queued=1', '--expect-drop', 'render.queued',
                                     '--expect-drop', 'render.gather')
        self.assertEqual(code, 1)
        self.assertEqual(report['failures'], [
            'render.model costs 1.0000 ms/frame, above the 0.9 limit',
            'render.queued costs 0.1500 ms/frame, not below baseline 0.1000',
            'render.gather: needs timing in both runs to show a drop',
        ])

    def test_phases_absent_without_timing_in_both_logs(self):
        timed = self.timed_log('timed.log', model_total=900.0, queued_total=60.0)
        plain = self.write('plain.log', [line(0, 'boot'), line(10, ENTRY), fps(20, 60.0, 20)])
        code, report = self.run_main(timed, plain)
        self.assertEqual(code, 0, report['failures'])
        self.assertIsNone(report['phases'])
        code, report = self.run_main(timed, plain, '--expect-drop', 'render.model')
        self.assertEqual(code, 1)
        self.assertIn('need "Native hook timing" lines in both logs', report['failures'][0])

    def test_per_call_fallback_without_render_helper(self):
        log = self.write('game.log', [line(0, 'boot'), line(10, ENTRY), fps(20, 60.0, 20),
                                      hook(21, 'render.model', 256, 64.0, 1.0)])
        code, report = self.run_main(log, log, '--max-phase', 'render.model=0.2')
        self.assertEqual(report['phases']['metric'], 'ms_per_call')
        self.assertEqual(report['failures'], ['render.model costs 0.2500 ms/call, above the 0.2 limit'])

    def test_markers_cut_scene_and_loading_segments(self):
        lines = phased(entry=30, load=80, resume=150, intro=50.0, loading=57.0, game=27.0, end=250)
        # t9 and t3 talk during the loading screen; only t1 (the entry draw's thread) ends it.
        self.assertEqual(gate.markers(lines), dict(entry=30.0, load=80.0, gameplay=150.0, loading_screens=1))
        self.assertEqual(gate.segments(lines), [['scene', 30.0, 80.0], ['loading', 80.0, 150.0],
                                                ['scene', 150.0, 250.0]])

    def test_presenter_viewport_alone_starts_loading(self):
        lines = [tline(0, 't3', 'boot'), tline(10, 't1', ENTRY), tline(40, 't9', PRESENTER),
                 tline(41, 't9', 'Native full scene begin: count=1'), tline(90, 't1', 'Native font draw')]
        self.assertEqual(gate.markers(lines), dict(entry=10.0, load=40.0, gameplay=90.0, loading_screens=1))

    def test_presenter_before_entry_is_not_a_loading_marker(self):
        lines = [tline(0, 't3', 'boot'), tline(5, 't9', PRESENTER), tline(10, 't1', ENTRY), tline(90, 't1', 'x')]
        self.assertEqual(gate.markers(lines), dict(entry=10.0, load=None, gameplay=None, loading_screens=0))

    def test_phase_fps_keeps_only_samples_wholly_inside_a_phase(self):
        lines = phased(entry=30, load=80, resume=150, intro=50.0, loading=57.0, game=27.0, end=250)
        lines.append(tfps(152, 1.0))  # spans 147..152: straddles the resume, counted nowhere
        phases = gate.phase_fps(lines, 10, 150)
        self.assertEqual(phases['intro']['windows_s'], [[40.0, 80.0]])
        self.assertEqual(phases['intro']['fps_samples'], 8)            # 45..80
        self.assertEqual(phases['loading']['fps_samples'], 13)         # 88..148; 83 straddles the load
        self.assertEqual(phases['loading']['fps_median'], 57.0)
        self.assertEqual(phases['gameplay']['windows_s'], [[160.0, 250.0]])
        self.assertEqual(phases['gameplay']['fps_samples'], 18)        # 165..250
        self.assertEqual(phases['gameplay']['fps_min'], 27.0)

    def test_return_to_loading_is_excluded_from_gameplay(self):
        lines = phased(entry=30, load=80, resume=150, intro=50.0, loading=57.0, game=27.0, end=300,
                       restart=(200, 240))
        self.assertEqual(gate.markers(lines)['loading_screens'], 2)
        phases = gate.phase_fps(lines, 10, 150)
        self.assertEqual(phases['gameplay']['windows_s'], [[160.0, 200.5], [240.0, 300.0]])
        self.assertEqual(phases['gameplay']['fps_median'], 27.0)
        self.assertEqual(phases['loading']['windows_s'], [[80.0, 150.0], [200.5, 240.0]])
        self.assertEqual(phases['loading']['fps_median'], 57.0)
        self.assertEqual(phases['gameplay']['fps_min'], 27.0)

    def test_all_mode_is_default_and_gates_every_phase(self):
        # The baseline reaches gameplay sooner; a single window after entry would
        # compare its gameplay against the candidate's loading screen.
        base = self.write('base.log', phased(entry=30, load=80, resume=150, intro=55.0, loading=57.0,
                                             game=27.0, end=330))
        good = self.write('good.log', phased(entry=40, load=150, resume=220, intro=54.0, loading=57.0,
                                             game=26.5, end=380))
        code, report = self.run_main(good, base)
        self.assertEqual(report['phase_mode'], 'all')
        self.assertIn('found in both logs', report['phase_mode_reason'])
        self.assertEqual(code, 0, report['failures'])
        self.assertEqual(set(report['phases_fps']), {'intro', 'loading', 'gameplay'})
        row = report['phases_fps']['gameplay']
        self.assertEqual(row['baseline'], dict(fps_median=27.0, fps_min=27.0, fps_samples=28))
        self.assertEqual(row['candidate']['fps_median'], 26.5)
        self.assertEqual(report['baseline']['mission_entry_s'], 30.0)   # entry window kept for compatibility
        self.assertEqual(report['window_after_entry'], [10, 150])

        slow = self.write('slow.log', phased(entry=40, load=150, resume=220, intro=12.0, loading=57.0,
                                             game=20.0, end=380))
        code, report = self.run_main(slow, base)
        self.assertEqual(code, 1)
        self.assertEqual(report['failures'], ['intro: median FPS 12.0 is below baseline 55.0',
                                              'gameplay: median FPS 20.0 is below baseline 27.0'])

    def test_all_mode_fails_when_candidate_never_reaches_gameplay(self):
        base = self.write('base.log', phased(entry=30, load=80, resume=150, intro=55.0, loading=57.0,
                                             game=27.0, end=330))
        stuck = self.write('stuck.log', [tline(0, 't3', 'boot'), tline(30, 't1', ENTRY)] +
                           [tfps(s, 55.0) for s in range(35, 141, 5)] +
                           [tline(140, 't3', MISSION_CAM, 'warning'), tline(140.5, 't9', PRESENTER)] +
                           [tfps(s, 57.0, 't9') for s in range(145, 331, 5)])
        code, report = self.run_main(stuck, base)
        self.assertEqual(report['phase_mode'], 'all')
        self.assertEqual(code, 1)
        self.assertEqual(report['failures'],
                         ['gameplay: no FPS samples in the candidate window; the run did not reach it'])
        self.assertEqual(report['phases_fps']['loading']['candidate']['fps_median'], 57.0)
        self.assertIsNone(report['candidate']['markers_s']['gameplay'])

    def test_default_falls_back_to_entry_without_a_marker_in_both_logs(self):
        base = self.write('base.log', phased(entry=30, load=80, resume=150, intro=55.0, loading=57.0,
                                             game=27.0, end=330))
        plain = self.write('plain.log', [line(0, 'boot'), line(30, ENTRY)] +
                           [fps(30 + i, 56.0, 30 + i) for i in range(10, 150, 5)])
        code, report = self.run_main(plain, base)
        self.assertEqual(report['phase_mode'], 'entry')
        self.assertEqual(report['phase_mode_reason'], 'no loading marker after entry in the candidate log')
        self.assertEqual(set(report['phases_fps']), {'entry'})
        self.assertEqual(report['phases_fps']['entry']['candidate']['fps_median'], 56.0)
        code, report = self.run_main(plain, base, '--phase', 'all')
        self.assertEqual(report['phase_mode'], 'all')
        self.assertIn('intro', report['phases_fps'])

    def test_gameplay_mode_windows_every_metric_from_the_gameplay_marker(self):
        lines = phased(entry=30, load=80, resume=150, intro=55.0, loading=57.0, game=27.0, end=330)
        text = 'Native static group execution: group={} completed=true recorded_batches=5 compatibility_calls=0'
        lines += [tline(170, 't3', text.format('0x1')), tline(100, 't3', text.format('0x2'))]  # 0x2: loading
        lines.sort(key=lambda text: text[:25])
        log = self.write('game.log', lines)
        summary = gate.summarize(log, 10, 150, anchor='gameplay')
        self.assertEqual(summary['fps_median'], 27.0)
        self.assertEqual(summary['native_groups'], 1)
        code, report = self.run_main(log, log, '--phase', 'gameplay')
        self.assertEqual(code, 0, report['failures'])
        self.assertEqual(report['phase_mode'], 'gameplay')
        self.assertEqual(report['window_after_gameplay'], [10, 150])
        self.assertEqual(set(report['phases_fps']), {'gameplay'})

    def test_entry_mode_reports_phases_fps_for_the_entry_window(self):
        log = self.write('game.log', [line(0, 'boot'), line(10, ENTRY), fps(20, 60.0, 20)])
        code, report = self.run_main(log, log)
        self.assertEqual(report['phase_mode'], 'entry')
        self.assertEqual(report['phases_fps']['entry']['baseline'],
                         dict(fps_median=60.0, fps_min=60.0, fps_samples=1))

    def test_bad_max_phase_argument_is_rejected(self):
        with self.assertRaises(Exception):
            gate.phase_limit('render.model')
        self.assertEqual(gate.phase_limit('render.model=2.5'), ('render.model', 2.5))


if __name__ == '__main__':
    unittest.main()
