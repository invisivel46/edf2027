"""soak-report regressions on synthetic game.log files: no game needed."""
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('soak_report', ROOT / 'tools/soak-report.py')
soak = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(soak)


def stamp(seconds):
    hours, rest = divmod(int(seconds), 3600)
    minutes, whole = divmod(rest, 60)
    return f'[2026-09-23 {10 + hours:02d}:{minutes:02d}:{whole:02d}.{int(round((seconds % 1) * 1000)):03d}]'


def tline(seconds, thread, text, level='info'):
    return f'{stamp(seconds)} [{level}] [core] [{thread}] {text}'


ENTRY = 'Native full frame static world: frames=1 draws=10'
MISSION_CAM = "[NtCreateFile] FAILED: path='GAME:\\MISSION\\M202\\MISSION.CAM' -> 0xc000000f"
PRESENTER = ('Native untiled scene viewport: caller=0x8219c828, requested=1280x720, stored=1280x720, '
             'CPU transform/scissor updated without tile packets')


def memory(seconds, private, working, ticks):
    # The exact REXLOG_INFO format of LogProcessMemory in src/input_hooks.cpp.
    return tline(seconds, 't3', f'Native memory: private_mb={private:.1f} working_set_mb={working:.1f} '
                                f'peak_working_set_mb={working + 50:.1f} pagefile_mb={private:.1f} handles=900 '
                                f'sim_ticks={ticks} t={int(seconds)} s')


def frame_times(seconds, hist, maximum, spikes=0):
    # The format of RecordNativeFrameTimeLocked in guest_shader_bridge.cpp (see tests/test_frame_time_report.py).
    count = sum(int(item.split(':')[1]) for item in hist.split(','))
    return tline(seconds, 't3', f'Native frame times: frames={count} span_ms=5000.0 p50_ms=1.000 p90_ms=1.000 '
                                f'p99_ms=1.000 p99_9_ms=1.000 max_ms={maximum:.3f} mean_ms=8.000 '
                                f'spikes={spikes} suppressed_spikes=0 hist={hist}')


def fps(seconds, value):
    return tline(seconds, 't3', f'FPS: {value} (frames {int(value * 5)} over 5.0 s, t={int(seconds)} s)')


def backend(seconds, pipelines):
    return tline(seconds, 't4', f'Native scene backend spend: frames=9, splits=0, operations_last_frame=3, '
                                f'upload_stalls=0, descriptor_stalls=0, pipelines={pipelines} (hits=4, misses=2), '
                                f'sampler_tables=3 (hits=0, misses=0, evictions=0), retiring=1, frame_waits=0 '
                                f'averaging 0us')


def mesh_cache(seconds, entries, size):
    return tline(seconds, 't5', f'Native immediate mesh cache: builds=0, hits=0, updates=0, bytes={size}, '
                                f'entries={entries}, entry_evictions=0, budget_evictions=0')


def soak_log(minutes=20, gameplay=100, leak_mb_per_min=2.0, late_hist='8.25:590,40.00:10', restart=None):
    """Entry at 30 s, the Mission 1 loading screen 60..100 s, then gameplay for
    `minutes`: memory every 5 s (growing by leak_mb_per_min), frame-time windows
    (clean, the last five minutes with late_hist), a pipeline count that stops
    growing, a mesh cache that grows steadily."""
    out = [tline(0, 't3', 'boot'), tline(30, 't1', ENTRY), tline(60, 't3', MISSION_CAM, 'warning'),
           tline(60.5, 't9', PRESENTER), tline(gameplay, 't1', 'Native full frame end: frames=2')]
    end = gameplay + minutes * 60
    for s in range(gameplay + 5, end + 1, 5):
        minute = (s - gameplay) / 60.0
        if restart and restart[0] <= s < restart[1]:
            continue
        out.append(fps(s, 60.0))
        out.append(memory(s, 2000 + leak_mb_per_min * minute, 1500 + leak_mb_per_min * minute / 2, s * 60))
        late = s > end - 300
        out.append(frame_times(s, late_hist if late else '8.25:600', 40.1 if late else 8.4, spikes=10 if late else 0))
        if s % 60 == 0:
            out.append(backend(s, min(40, 20 + int(minute))))
            out.append(mesh_cache(s, 100 + int(minute) * 10, 4096 * (100 + int(minute) * 10)))
    if restart:
        out.append(tline(restart[0], 't8', PRESENTER))
        out.append(tline(restart[1], 't1', 'Native full frame end: frames=3'))
    out.sort(key=lambda text: text[:25])
    return out


class SoakReportTests(unittest.TestCase):
    def test_windows_start_at_gameplay_and_cover_the_soak(self):
        result = soak.report(soak_log())
        self.assertEqual(result['origin'], 'gameplay')
        self.assertEqual(result['origin_s'], 100.0)
        self.assertEqual([w['index'] for w in result['windows']], [0, 1, 2, 3, 4])
        # The log ends 5 s into window 4 (the sample at 1300 s): shown, but partial.
        self.assertEqual([w['partial'] for w in result['windows']], [False] * 4 + [True])
        self.assertEqual(result['windows'][0]['from_s'], 100.0)
        self.assertEqual(result['windows'][0]['fps_median'], 60.0)
        self.assertEqual(result['windows'][0]['frames'], 59 * 600)   # 105..395
        self.assertEqual(result['loading_after_origin'], [])

    def test_memory_growth_slope_and_window_means(self):
        result = soak.report(soak_log(leak_mb_per_min=2.0))
        private = result['memory']['private_mb']
        self.assertAlmostEqual(private['slope_per_min'], 2.0, places=1)
        self.assertAlmostEqual(private['growth'], 2.0 * 15, delta=0.5)   # window 3 mean - window 0 mean
        self.assertAlmostEqual(result['memory']['working_set_mb']['slope_per_min'], 1.0, places=1)
        self.assertEqual(result['memory']['handles']['growth'], 0.0)
        flat = soak.report(soak_log(leak_mb_per_min=0.0))
        self.assertAlmostEqual(flat['memory']['private_mb']['slope_per_min'], 0.0, places=6)

    def test_late_stutter_is_the_last_window_p99_against_the_first(self):
        result = soak.report(soak_log())
        stutter = result['late_stutter']
        self.assertEqual(stutter['first_p99_ms'], 8.4)                # the 8.25 bucket, capped at the max
        self.assertEqual(stutter['last_p99_ms'], 40.1)
        self.assertEqual(stutter['last_window'], 3)                   # the partial window 4 is left out
        self.assertAlmostEqual(stutter['ratio'], 40.1 / 8.4)
        self.assertEqual(stutter['worst_window'], 3)                  # 15..20 min after gameplay
        self.assertEqual(result['windows'][3]['spikes'], 59 * 10)
        clean = soak.report(soak_log(late_hist='8.25:600'))
        self.assertLess(clean['late_stutter']['ratio'], 1.05)          # 8.50 vs 8.40: the same bucket

    def test_cache_sizes_first_last_and_growth(self):
        caches = soak.report(soak_log())['caches']
        self.assertEqual(caches['backend.pipelines']['first'], 20)   # 120 s: 0.33 min into gameplay
        self.assertEqual(caches['backend.pipelines']['last'], 39)
        self.assertEqual(caches['backend.sampler_tables']['growth'], 0)
        self.assertEqual(caches['backend.retiring']['last'], 1)
        self.assertEqual(caches['immediate_mesh.entries']['growth'], 190)
        self.assertEqual(caches['immediate_mesh.bytes']['last'], 4096 * 290)
        self.assertNotIn('textures.resident', caches)

    def test_gates(self):
        log = soak_log(leak_mb_per_min=2.0)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'game.log'
            path.write_text('\n'.join(log) + '\n', encoding='utf-8')

            def run(*extra):
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    code = soak.main([str(path), '--json', *extra])
                return code, json.loads(out.getvalue())

            code, result = run()
            self.assertEqual(code, 0, result['failures'])
            code, result = run('--max-growth-mb-per-min', '1.0', '--max-growth-mb', '100',
                               '--max-p99-ratio', '2', '--max-cache-growth', 'immediate_mesh.entries=50',
                               '--max-cache-growth', 'backend.sampler_tables=0', '--require-gameplay')
            self.assertEqual(code, 1)
            self.assertEqual(len(result['failures']), 3, result['failures'])
            self.assertIn('private bytes grow 2.00 MB/min, above 1', result['failures'][0])
            self.assertIn('above 2x', result['failures'][1])
            self.assertIn('immediate_mesh.entries grew by 190', result['failures'][2])
            with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
                soak.main([str(path), '--max-cache-growth', 'nonsense=1'])

    def test_leaving_gameplay_is_reported_and_can_fail(self):
        result = soak.report(soak_log(restart=(700, 760)))
        self.assertEqual(result['loading_after_origin'], [[700.0, 760.0]])
        args = type('Args', (), dict(max_growth_mb_per_min=None, max_growth_mb=None, max_p99_ratio=None,
                                     max_cache_growth=[], require_gameplay=True))()
        self.assertEqual(soak.failures(result, args), ['left gameplay for a loading screen after the origin '
                                                       '(700.0-760.0 s)'])

    def test_missing_memory_lines_fail_memory_gates_clearly(self):
        lines = [l for l in soak_log() if 'Native memory' not in l]
        result = soak.report(lines)
        self.assertEqual(result['memory'], {})
        args = type('Args', (), dict(max_growth_mb_per_min=1.0, max_growth_mb=None, max_p99_ratio=None,
                                     max_cache_growth=[], require_gameplay=False))()
        self.assertIn('--edf_native_memory_log=true', soak.failures(result, args)[0])

    def test_origin_falls_back_without_gameplay_and_text_output(self):
        lines = [tline(0, 't3', 'boot')] + [fps(s, 30.0) for s in range(5, 700, 5)]
        result = soak.report(lines, window_s=300)
        self.assertEqual(result['origin'], 'first stamp')
        self.assertEqual(len(result['windows']), 3)
        out = io.StringIO()
        soak.print_text(dict(result, failures=['x']), file=out)
        self.assertIn('origin: first stamp', out.getvalue())
        self.assertIn('FAIL: x', out.getvalue())
        with tempfile.TemporaryDirectory() as tmp:
            empty = Path(tmp) / 'empty.log'
            empty.write_text('no stamps\n', encoding='utf-8')
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(soak.main([str(empty)]), 2)
            full = Path(tmp) / 'game.log'
            full.write_text('\n'.join(soak_log()) + '\n', encoding='utf-8')
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(soak.main([str(full)]), 0)
            self.assertIn('cache backend.pipelines: 20 -> 39', out.getvalue())
            self.assertIn('late stutter', out.getvalue())


if __name__ == '__main__':
    unittest.main()
