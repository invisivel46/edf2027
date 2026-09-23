"""Tests for tools/fsr-quality.py with tiny synthetic BMP captures."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
_spec = importlib.util.spec_from_file_location('fsr_quality', TOOLS / 'fsr-quality.py')
fq = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = fq
_spec.loader.exec_module(fq)


def bmp(grey, width, height, bits=24, top_down=False):
    """grey: top-down list of 0..255 values, written as (g, g, g)."""
    step = bits // 8
    pitch = (width * step + 3) & ~3
    body = bytearray()
    rows = range(height) if top_down else range(height - 1, -1, -1)
    for y in rows:
        row = bytearray()
        for g in grey[y * width:(y + 1) * width]:
            row += bytes((g, g, g)) + (b'\xff' if step == 4 else b'')
        body += row + b'\0' * (pitch - len(row))
    header = b'BM' + struct.pack('<IHHI', 54 + len(body), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, width, -height if top_down else height, 1, bits, 0, len(body), 0, 0, 0, 0)
    return header + info + bytes(body)


def edge(width, height, cut, scale=1):
    """A vertical edge at x = cut (in pixels of a width-wide image), rendered
    at `scale` times the resolution with exact coverage per pixel."""
    w, h = width * scale, height * scale
    out = []
    for _y in range(h):
        for x in range(w):
            left, right = x / scale, (x + 1) / scale
            coverage = min(max((cut - left) / (right - left), 0.0), 1.0)
            out.append(round(255 * coverage))
    return out, w, h


class Files:
    def __init__(self):
        self.dir = tempfile.TemporaryDirectory()

    def write(self, name, grey, width, height, **kw):
        path = Path(self.dir.name) / name
        path.write_bytes(bmp(grey, width, height, **kw))
        return str(path)

    def close(self):
        self.dir.cleanup()


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = fq.main(argv)
    return code, out.getvalue()


class ReadTests(unittest.TestCase):
    def test_bmp_orientations_and_depths_agree(self):
        grey = [0, 64, 128, 255, 10, 20]
        for bits in (24, 32):
            for top_down in (False, True):
                image = fq.read_bmp_luma(bmp(grey, 3, 2, bits, top_down))
                self.assertEqual((image.width, image.height), (3, 2))
                for i, g in enumerate(grey):
                    self.assertAlmostEqual(image.values[i], g / 255, places=5)

    def test_rejects_garbage(self):
        with self.assertRaises(fq.QualityError):
            fq.read_bmp_luma(b'not a bmp at all' * 8)

    def test_downsample_box(self):
        image = fq.Luma(4, 2, fq.array('f', [0, 1, 1, 1, 0, 0, 1, 1]))
        small = fq.downsample(image, 2)
        self.assertEqual((small.width, small.height), (2, 1))
        self.assertAlmostEqual(small.values[0], 0.25)
        self.assertAlmostEqual(small.values[1], 1.0)
        with self.assertRaises(fq.QualityError):
            fq.downsample(fq.Luma(3, 2, fq.array('f', [0] * 6)), 2)


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.files = Files()

    def tearDown(self):
        self.files.close()

    def test_antialiased_beats_aliased_against_the_supersampled_reference(self):
        width, height, cut = 16, 4, 7.5
        hi, hw, hh = edge(width, height, cut, scale=2)
        reference = self.files.write('ref.bmp', hi, hw, hh)
        smooth, _, _ = edge(width, height, cut)                   # what ideal AA produces
        aliased = [255 if (i % width) < 8 else 0 for i in range(width * height)]  # pixel centres only
        fsr = self.files.write('fsr.bmp', smooth, width, height)
        plain = self.files.write('off.bmp', aliased, width, height)
        result = fq.measure_reference([fsr], [reference], [plain], 2, None, 0.08)
        s = result['summary']
        self.assertLess(s['fsr_mean_abs'], 1e-3)
        self.assertGreater(s['baseline_mean_abs'], s['fsr_mean_abs'])
        self.assertGreater(result['frames'][0]['fsr_error']['edge_pixels'], 0)
        code, text = run(['reference', '--fsr', fsr, '--reference', reference, '--baseline', plain, '--max-error', '0.01'])
        self.assertEqual(code, 0, text)
        self.assertIn('FSR off', text)
        code, _ = run(['reference', '--fsr', plain, '--reference', reference, '--max-error', '0.001'])
        self.assertEqual(code, 1)

    def test_mismatched_sizes_are_input_errors(self):
        a = self.files.write('a.bmp', [0] * 16, 4, 4)
        b = self.files.write('b.bmp', [0] * 16, 4, 4)  # not 2x
        code, _ = run(['reference', '--fsr', a, '--reference', b])
        self.assertEqual(code, 2)

    def test_region(self):
        width, height = 8, 8
        truth = self.files.write('t.bmp', [0] * (4 * width * height), 2 * width, 2 * height)
        image = [0] * (width * height)
        image[0] = 255  # outside the region below
        fsr = self.files.write('f.bmp', image, width, height)
        result = fq.measure_reference([fsr], [truth], [], 2, '2,2,4,4', 0.08)
        self.assertEqual(result['frames'][0]['fsr_error']['pixels'], 16)
        self.assertEqual(result['summary']['fsr_mean_abs'], 0.0)


class StabilityTests(unittest.TestCase):
    def setUp(self):
        self.files = Files()

    def tearDown(self):
        self.files.close()

    def test_still_frames_are_stable_and_flicker_is_found(self):
        width, height = 6, 5
        still = [self.files.write(f'still.{i}.bmp', [100] * (width * height), width, height) for i in range(4)]
        flickering = []
        for i in range(4):
            grey = [100] * (width * height)
            grey[7] = 100 if i % 2 else 160  # one pixel alternates
            flickering.append(self.files.write(f'flick.{i}.bmp', grey, width, height))
        result = fq.measure_stability(still, flickering, None, 0.02)
        self.assertEqual(result['fsr']['mean_std'], 0.0)
        self.assertEqual(result['fsr']['flicker_fraction'], 0.0)
        self.assertAlmostEqual(result['baseline']['flicker_fraction'], 1 / (width * height))
        self.assertGreater(result['baseline']['max_std'], 0.1)
        code, _ = run(['stability', '--fsr', *flickering, '--max-flicker', '0'])
        self.assertEqual(code, 1)
        code, text = run(['stability', '--fsr', *still, '--json'])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(text)['fsr']['frames'], 4)

    def test_needs_two_frames(self):
        one = self.files.write('one.bmp', [0] * 4, 2, 2)
        self.assertEqual(run(['stability', '--fsr', one])[0], 2)

    def test_glob_orders_by_frame_number(self):
        paths = [self.files.write(f'cap.output.{n}.bmp', [0] * 4, 2, 2) for n in (10, 9, 100)]
        ordered = fq.expand([str(Path(paths[0]).parent / 'cap.output.*.bmp')])
        self.assertEqual([Path(p).name for p in ordered], ['cap.output.9.bmp', 'cap.output.10.bmp', 'cap.output.100.bmp'])


class GhostingTests(unittest.TestCase):
    def setUp(self):
        self.files = Files()

    def tearDown(self):
        self.files.close()

    def bar(self, x0, width=12, height=3):
        grey = [0] * (width * height)
        for y in range(height):
            for x in range(x0, min(x0 + 3, width)):
                grey[y * width + x] = 255
        return grey

    def test_a_pan_without_history_lag_has_no_ghosting(self):
        frames = [self.files.write(f'ref.{t}.bmp', self.bar(t * 2), 12, 3) for t in range(4)]
        result = fq.measure_ghosting(frames, frames, None, 0.06)
        self.assertEqual(result['summary']['differing_fraction'], 0.0)
        self.assertEqual(result['summary']['lag_fraction'], 0.0)

    def test_a_trailing_image_is_ghosting(self):
        reference = [self.files.write(f'ref.{t}.bmp', self.bar(t * 2), 12, 3) for t in range(4)]
        # FSR kept last frame's bar on top of this frame's: a trail.
        ghosted = []
        for t in range(4):
            now, before = self.bar(t * 2), self.bar(max(t - 1, 0) * 2)
            ghosted.append(self.files.write(f'fsr.{t}.bmp', [max(a, b) for a, b in zip(now, before)], 12, 3))
        result = fq.measure_ghosting(ghosted, reference, None, 0.06)
        self.assertGreater(result['summary']['differing_fraction'], 0.0)
        self.assertEqual(result['summary']['lag_fraction'], 1.0)
        code, text = run(['ghosting', '--fsr', *ghosted, '--reference', *reference, '--max-lag', '0.5'])
        self.assertEqual(code, 1)
        self.assertIn('lagging', text)

    def test_noise_is_not_lag(self):
        reference = [self.files.write(f'ref.{t}.bmp', self.bar(t * 2), 12, 3) for t in range(3)]
        noisy = []
        for t in range(3):
            grey = self.bar(t * 2)
            grey[5] = min(255, grey[5] + 60) if grey[5] < 128 else grey[5] - 60
            noisy.append(self.files.write(f'noisy.{t}.bmp', grey, 12, 3))
        result = fq.measure_ghosting(noisy, reference, None, 0.06)
        self.assertGreater(result['summary']['differing_fraction'], 0.0)
        self.assertLess(result['summary']['lag_fraction'], 1.0)


if __name__ == '__main__':
    unittest.main()
