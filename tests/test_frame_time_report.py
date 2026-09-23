"""frame-time-report regressions on synthetic game.log files: no game needed."""
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('frame_time_report', ROOT / 'tools/frame-time-report.py')
report = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(report)


def stamp(seconds):
    whole = int(seconds)
    return f'[2026-09-23 10:{whole // 60:02d}:{whole % 60:02d}.{int(round((seconds - whole) * 1000)):03d}]'


def tline(seconds, thread, text, level='info'):
    return f'{stamp(seconds)} [{level}] [core] [{thread}] {text}'


def window(seconds, frames, span_ms, hist, maximum, mean, spikes=0, thread='t1'):
    # The exact REXLOG_INFO format of RecordNativeFrameTimeLocked in guest_shader_bridge.cpp;
    # the per-window percentiles are not what the tool merges, so they are placeholders.
    return tline(seconds, thread, f'Native frame times: frames={frames} span_ms={span_ms:.1f} p50_ms=1.000 '
                                  f'p90_ms=1.000 p99_ms=1.000 p99_9_ms=1.000 max_ms={maximum:.3f} '
                                  f'mean_ms={mean:.3f} spikes={spikes} suppressed_spikes=0 hist={hist}')


COUNTERS = ('pipelines', 'shader_compiles', 'mesh_builds', 'buffers', 'buffer_kb', 'textures', 'texture_kb',
            'declines', 'post_fallbacks', 'upload_stalls', 'descriptor_stalls', 'sampler_misses',
            'backend_frame_waits', 'frame_splits')


def spike(seconds, frame, ms, median, reason='median', top='off', thread='t1', **moved):
    counters = ' '.join(f'{name}={moved.get(name, 0)}' for name in COUNTERS)
    return tline(seconds, thread, f'Native frame spike: frame={frame} ms={ms:.3f} median_ms={median:.3f} '
                                  f'reason={reason} {counters} top_phases={top}')


def gpu(seconds, name, frames, total, maximum, thread='t1'):
    return tline(seconds, thread, f'Native GPU timing: pass={name} frames={frames} total_ms={total:.3f} '
                                  f'avg_ms={total / frames:.3f} max_ms={maximum:.3f}')


ENTRY = 'Native full frame static world: frames=1 draws=10'
MISSION_CAM = "[NtCreateFile] FAILED: path='GAME:\\MISSION\\M202\\MISSION.CAM' -> 0xc000000f"


def mission():
    """Menus to 20 s, entry on the scene thread t1 at 20 s, intro to 60 s, a
    loading screen on t9 from 60 s, gameplay from t1's return at 90 s."""
    return [
        tline(0, 't3', 'boot'),
        window(10, 300, 5000.0, '16.50:300', 16.9, 16.6),
        tline(20, 't1', ENTRY),
        # Intro: 8.25 ms frames; one window straddles entry and is a boundary.
        window(22, 600, 4950.0, '8.25:600', 8.4, 8.3),
        window(30, 600, 4950.0, '8.25:590,8.50:10', 8.6, 8.3),
        window(40, 600, 4950.0, '8.25:600', 8.4, 8.3),
        spike(41, 1500, 20.1, 8.3, pipelines=1, buffers=3, buffer_kb=96, top='frame.native.models:12.400,swap.gpu_wait:3.000'),
        gpu(42, 'frame', 600, 3000.0, 6.0), gpu(42, 'models', 600, 1200.0, 2.5),
        tline(60, 't3', MISSION_CAM, 'warning'),
        # Loading: slow frames, one window of 5 at 200 ms.
        window(70, 5, 1000.0, '200.00:5', 205.0, 200.0, spikes=5, thread='t9'),
        spike(70, 2000, 205.0, 200.0, reason='limit', thread='t9', textures=4, texture_kb=2048),
        tline(90, 't1', 'Native Utility draw: submitted=1'),
        # Gameplay: two windows, 8.25 then 10.00 ms, and two spikes.
        window(96, 600, 5000.0, '8.25:600', 8.4, 8.3),
        window(102, 600, 6000.0, '10.00:594,40.00:6', 40.2, 10.3, spikes=6),
        spike(100, 3000, 40.2, 10.0, reason='limit+median', mesh_builds=2, top='frame.native.static_world:30.000'),
        spike(101, 3001, 25.5, 10.0, reason='limit+median', top='frame.native.static_world:18.000'),
        spike(101.5, 3002, 22.0, 10.0),
        gpu(103, 'models', 600, 1800.0, 4.0),
    ]


class FrameTimeReportTests(unittest.TestCase):
    def test_histogram_percentiles_are_nearest_rank_bucket_upper_bounds(self):
        hist = report.parse_histogram('8.25:90,8.50:9,40.00:1')
        self.assertEqual(hist, {8.25: 90, 8.5: 9, 40.0: 1})
        p = report.histogram_percentiles(hist, 40.2)
        self.assertEqual(p['p50'], 8.5)    # 8.25..8.50
        self.assertEqual(p['p90'], 8.5)
        self.assertEqual(p['p99'], 8.75)
        self.assertEqual(p['p99_9'], 40.2)  # 40.00..40.25, capped at the observed max
        self.assertEqual(report.bucket_upper(60.0), 61.0)
        self.assertEqual(report.bucket_upper(150.0), 160.0)
        self.assertEqual(report.parse_histogram('none'), {})
        self.assertIsNone(report.histogram_percentiles({}, 0)['p50'])

    def test_spike_fields_parse_counters_and_phases(self):
        fields = report.parse_spike('frame=7 ms=31.020 median_ms=8.300 reason=limit+median pipelines=1 '
                                    'mesh_builds=0 top_phases=frame.native.models:12.300,swap.gpu_wait:4.100')
        self.assertEqual(fields['frame'], 7)
        self.assertAlmostEqual(fields['ms'], 31.02)
        self.assertEqual(fields['reason'], 'limit+median')
        self.assertEqual(fields['pipelines'], 1)
        self.assertEqual(fields['top_phases'], 'frame.native.models:12.300,swap.gpu_wait:4.100')

    def test_mission_phases(self):
        result = report.report(mission())
        self.assertEqual(result['mission_entry_s'], 20.0)
        phases = result['phases']
        self.assertEqual(list(phases), ['pre_entry', 'intro', 'loading', 'gameplay', 'boundary', 'all'])
        self.assertEqual(phases['pre_entry']['frames'], 300)
        # The 22 s window began 4.95 s earlier, before entry: boundary, not intro.
        self.assertEqual(phases['boundary']['frames'], 600)
        self.assertEqual(phases['intro']['frames'], 1200)
        self.assertEqual(phases['intro']['p50'], 8.5)
        self.assertEqual(phases['intro']['max_ms'], 8.6)
        self.assertEqual(phases['loading']['frames'], 5)
        self.assertEqual(phases['loading']['p50'], 205.0)  # 200..210 bucket capped at the max
        gameplay = phases['gameplay']
        self.assertEqual(gameplay['frames'], 1200)
        self.assertEqual(gameplay['p50'], 8.5)
        self.assertEqual(gameplay['p90'], 10.25)
        self.assertEqual(gameplay['p99_9'], 40.2)
        self.assertAlmostEqual(gameplay['mean_ms'], 9.3)
        self.assertEqual(gameplay['spikes_in_windows'], 6)
        self.assertEqual(phases['all']['frames'], 300 + 600 + 1200 + 5 + 1200)

    def test_spike_attribution(self):
        phases = report.report(mission(), top=2)['phases']
        gameplay = phases['gameplay']['spikes']
        self.assertEqual(gameplay['count'], 3)
        self.assertEqual(gameplay['reasons'], {'limit+median': 2, 'median': 1})
        self.assertEqual(gameplay['causes'], {'mesh_builds': 1})
        self.assertEqual(gameplay['without_counter_cause'], 2)
        self.assertEqual(gameplay['top_phase_leaders'], {'frame.native.static_world': 2})
        self.assertEqual([s['frame'] for s in gameplay['worst']], [3000, 3001])
        intro = phases['intro']['spikes']
        self.assertEqual(intro['causes'], {'pipelines': 1, 'buffers': 1, 'buffer_kb': 1})
        self.assertEqual(intro['top_phase_leaders'], {'frame.native.models': 1})
        self.assertEqual(phases['loading']['spikes']['causes'], {'textures': 1, 'texture_kb': 1})
        self.assertEqual(phases['all']['spikes']['count'], 5)

    def test_gpu_passes_per_phase(self):
        phases = report.report(mission())['phases']
        self.assertEqual(phases['intro']['gpu']['models'], dict(frames=600, avg_ms=2.0, max_ms=2.5))
        self.assertEqual(phases['intro']['gpu']['frame']['avg_ms'], 5.0)
        self.assertEqual(phases['gameplay']['gpu']['models'], dict(frames=600, avg_ms=3.0, max_ms=4.0))
        self.assertEqual(phases['all']['gpu']['models'], dict(frames=1200, avg_ms=2.5, max_ms=4.0))

    def test_log_without_entry_is_unphased(self):
        lines = [tline(0, 't3', 'boot'), window(10, 100, 1000.0, '10.00:100', 10.1, 10.1)]
        phases = report.report(lines)['phases']
        self.assertEqual(list(phases), ['unphased', 'all'])
        self.assertEqual(phases['unphased']['p99'], 10.1)

    def test_mission_without_pre_mission_scene_has_no_intro(self):
        # M204-like: the mission load comes before the first scene, which is gameplay.
        lines = [tline(0, 't3', 'boot'), tline(10, 't3', MISSION_CAM.replace('M202', 'M204'), 'warning'),
                 tline(20, 't1', ENTRY),
                 window(26, 600, 5000.0, '8.25:600', 8.4, 8.3),
                 window(31, 600, 5000.0, '8.25:600', 8.4, 8.3)]
        phases = report.report(lines)['phases']
        self.assertNotIn('intro', phases)
        self.assertEqual(phases['gameplay']['frames'], 1200)

    def test_main_prints_table_and_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'game.log'
            path.write_text('\n'.join(mission()) + '\n', encoding='utf-8')
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(report.main([str(path)]), 0)
            text = out.getvalue()
            self.assertIn('gameplay', text)
            self.assertIn('largest hook phase: frame.native.static_world 2', text)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(report.main([str(path), '--json']), 0)
            self.assertEqual(json.loads(out.getvalue())['phases']['gameplay']['frames'], 1200)
            empty = Path(tmp) / 'empty.log'
            empty.write_text(tline(0, 't3', 'boot') + '\n', encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(report.main([str(empty)]), 1)


if __name__ == '__main__':
    unittest.main()
