"""coverage-report regressions on synthetic game.log files: no game needed."""
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('coverage_report', ROOT / 'tools/coverage-report.py')
report = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(report)


def tline(seconds, text):
    return f'[2026-09-23 10:00:{int(seconds):02d}.000] [info] [core] [t3] {text}'


def summary(seconds, kind, frames, covered, uncovered, parity):
    # The format of NativeCoverageCensus::Summary (src/native_graphics/native_coverage_census.cpp).
    total = covered + uncovered
    coverage = 100.0 * covered / total if total else 100.0
    return tline(seconds, f'Native coverage summary: kind={kind} seconds={seconds:.1f} frames={frames} items=0 covered_items=0 '
                          f'uncovered_items=0 parity_items=0 covered_objects={covered} uncovered_objects={uncovered} '
                          f'parity_objects={parity} coverage={coverage:.3f}%')


def item(seconds, status, name, vtable, reason, frames, objects, peak=1, detail='-'):
    return tline(seconds, f'Native coverage: {status} class={name} vtable=0x{vtable:08X} reason={reason} frames={frames} '
                          f'objects={objects} peak={peak} first_seen=0.0s last_seen={seconds:.1f}s detail={detail}')


class CoverageReportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, lines):
        path = self.dir / name
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return path

    def run_report(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = report.main([str(a) for a in args])
        return code, out.getvalue(), err.getvalue()

    def standard_log(self):
        # A window at 30 s (in the rotated part), then the final summary with larger totals.
        self.write('game.1.log', [
            tline(1, 'Native full frame static world: frames=1 draws=10'),
            summary(30, 'window', 1800, 900, 12, 3),
            item(30, 'uncovered', 'unknown', 0x82009990, 'registry_unknown_class', 1800, 3600, 2, 'slot=0x82123456'),
            item(30, 'uncovered', 'pass:sky', 0, 'declined', 1, 1, 1, 'sky_pass_has_no_program'),
            item(30, 'parity', 'effect_object', 0, 'undrawn_key', 3, 3),
            item(30, 'covered', 'clGiantAnt', 0x82005198, 'models', 1800, 900, 1),
        ])
        return self.write('game.log', [
            summary(45, 'final', 2700, 1500, 20, 5),
            item(45, 'uncovered', 'unknown', 0x82009990, 'registry_unknown_class', 2700, 5400, 2, 'slot=0x82123456'),
            item(45, 'uncovered', 'clWeird', 0x82001000, 'effect_no_builder', 10, 20, 4, 'slot=0x82000000'),
            item(45, 'uncovered', 'pass:sky', 0, 'declined', 1, 1, 1, 'sky_pass_has_no_program'),
            item(45, 'parity', 'effect_object', 0, 'undrawn_key', 5, 5),
            item(45, 'covered', 'clGiantAnt', 0x82005198, 'models', 2700, 1500, 1),
            item(45, 'covered', 'clBuilding', 0x820026BC, 'static_world', 2700, 5000, 2),
        ])

    def test_last_line_per_item_across_rotated_parts(self):
        items, summaries = report.parse(report.gate.read_log_lines(self.standard_log()))
        by_key = {(i['status'], i['class'], i['reason']): i for i in items}
        self.assertEqual(len(items), 6)
        self.assertEqual(by_key[('uncovered', 'unknown', 'registry_unknown_class')]['objects'], 5400)
        self.assertEqual(by_key[('uncovered', 'unknown', 'registry_unknown_class')]['detail'], 'slot=0x82123456')
        self.assertEqual(by_key[('covered', 'clGiantAnt', 'models')]['frames'], 2700)
        self.assertEqual([s['kind'] for s in summaries], ['window', 'final'])

    def test_unallowed_uncovered_fails(self):
        code, out, _ = self.run_report(self.standard_log())
        self.assertEqual(code, 1)
        self.assertIn('NOT ALLOWED', out)
        self.assertIn('FAIL: uncovered unknown:registry_unknown_class (5400 objects over 2700 frames)', out)
        self.assertIn('FAIL: uncovered clWeird:effect_no_builder', out)
        self.assertIn('FAIL: uncovered pass:sky:declined', out)

    def test_allow_patterns_pass(self):
        log = self.standard_log()
        allow_file = self.write('allow.txt', ['# accepted gaps', '0x82009990:registry_unknown_class', '', 'pass:*:declined  # sky'])
        code, out, _ = self.run_report(log, '--allow', 'clWeird:effect_*', '--allow-file', allow_file, '--allow', 'clNothing:x')
        self.assertEqual(code, 0, out)
        self.assertIn('PASS', out)
        self.assertIn('allowed by 0x82009990:registry_unknown_class', out)
        self.assertIn('allowed by pass:*:declined', out)
        self.assertIn('note: --allow clNothing:x matched nothing', out)

    def test_coverage_and_gates(self):
        log = self.standard_log()
        allows = ['--allow', '*:*']
        code, out, _ = self.run_report(log, *allows, '--json')
        self.assertEqual(code, 0)
        result = json.loads(out)
        # Covered 1500 + 5000, uncovered 5400 + 20 + 1: parity is left out.
        self.assertAlmostEqual(result['coverage'], 100.0 * 6500 / (6500 + 5421))
        self.assertEqual(result['objects'], {'uncovered': 5421, 'parity': 5, 'covered': 6500})
        self.assertEqual(result['covered_classes'], ['clBuilding', 'clGiantAnt'])
        self.assertEqual(result['uncovered_classes'], ['clWeird', 'pass:sky', 'unknown'])
        self.assertTrue(result['final'])
        self.assertEqual(result['frames'], 2700)
        # Uncovered first, by objects descending.
        self.assertEqual([i['class'] for i in result['items']][:3], ['unknown', 'clWeird', 'pass:sky'])
        code, out, _ = self.run_report(log, *allows, '--min-coverage', '60')
        self.assertEqual(code, 1)
        self.assertIn('below 60.0%', out)

    def test_require_final(self):
        log = self.write('game.log', [
            summary(30, 'window', 1800, 900, 0, 0),
            item(30, 'covered', 'clGiantAnt', 0x82005198, 'models', 1800, 900),
        ])
        self.assertEqual(self.run_report(log)[0], 0)
        code, out, _ = self.run_report(log, '--require-final')
        self.assertEqual(code, 1)
        self.assertIn('no final summary', out)

    def test_no_census_lines(self):
        log = self.write('game.log', [tline(1, 'FPS: 60.0 (frames 300 over 5.0 s, t=5 s)')])
        code, _, err = self.run_report(log)
        self.assertEqual(code, 2)
        self.assertIn('edf_native_coverage_census', err)

    def test_bad_allow(self):
        with self.assertRaises(SystemExit):
            with contextlib.redirect_stderr(io.StringIO()):
                report.main([str(self.standard_log()), '--allow', 'nocolon'])


if __name__ == '__main__':
    unittest.main()
