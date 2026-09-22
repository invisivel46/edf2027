"""Tests for tools/compare-renderer-images.py with tiny synthetic images."""
import contextlib
import importlib.util
import io
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
_spec = importlib.util.spec_from_file_location('compare_renderer_images', TOOLS / 'compare-renderer-images.py')
cmp = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = cmp  # dataclasses resolve annotations through sys.modules
_spec.loader.exec_module(cmp)


def bmp(pixels, width, height, bits=24, top_down=True):
    """pixels: top-down list of (r, g, b)."""
    step = bits // 8
    pitch = (width * step + 3) & ~3
    body = bytearray()
    rows = range(height) if top_down else range(height - 1, -1, -1)
    for y in rows:
        row = bytearray()
        for r, g, b in pixels[y * width:(y + 1) * width]:
            row += bytes((b, g, r)) + (b'\xff' if step == 4 else b'')
        body += row + b'\0' * (pitch - len(row))
    header = b'BM' + struct.pack('<IHHI', 54 + len(body), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, width, -height if top_down else height, 1, bits, 0, len(body), 0, 0, 0, 0)
    return header + info + bytes(body)


def png_rgba(pixels, width, height, filters):
    """Encode with the given per-row filter types to exercise every unfilter path."""
    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    raw = bytearray()
    prev = bytearray(width * 4)
    for y in range(height):
        line = bytearray()
        for r, g, b in pixels[y * width:(y + 1) * width]:
            line += bytes((r, g, b, 255))
        ftype = filters[y % len(filters)]
        out = bytearray()
        for i, value in enumerate(line):
            left = line[i - 4] if i >= 4 else 0
            upleft = prev[i - 4] if i >= 4 else 0
            predictor = [0, left, prev[i], (left + prev[i]) >> 1, paeth(left, prev[i], upleft)][ftype]
            out.append((value - predictor) & 255)
        raw += bytes((ftype,)) + out
        prev = line

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


W, H = 5, 3  # odd width forces BMP row padding
BASE = [((x * 50) % 256, (y * 90) % 256, (x * y * 17 + 3) % 256) for y in range(H) for x in range(W)]


class CompareRendererImages(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def write(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return path

    def run_main(self, *args):
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(io.StringIO()):
            code = cmp.main([str(a) for a in args])
        return code, stdout.getvalue()

    def test_bmp_layouts_decode_identically(self):
        expected = bytes(c for p in BASE for c in p)
        for bits in (24, 32):
            for top_down in (True, False):
                with self.subTest(bits=bits, top_down=top_down):
                    image = cmp.read_bmp(bmp(BASE, W, H, bits, top_down))
                    self.assertEqual((image.width, image.height), (W, H))
                    self.assertEqual(image.rgb, expected)

    def test_png_matches_bmp(self):
        image = cmp.read_png(png_rgba(BASE, W, H, [0, 1, 2, 3, 4]))
        self.assertEqual(image.rgb, cmp.read_bmp(bmp(BASE, W, H)).rgb)
        image = cmp.read_png(png_rgba(BASE, W, H, [4, 3, 2]))
        self.assertEqual(image.rgb, cmp.read_bmp(bmp(BASE, W, H)).rgb)

    def test_identical_images_pass(self):
        a = self.write('a.bmp', bmp(BASE, W, H))
        b = self.write('b.png', png_rgba(BASE, W, H, [1]))
        code, out = self.run_main(a, b)
        self.assertEqual(code, 0)
        self.assertIn('differing=0 ', out)
        self.assertIn('psnr_db=inf', out)

    def test_threshold_and_limit(self):
        changed = list(BASE)
        r, g, b = changed[0]
        changed[0] = (r, g, (b + 2) % 256)  # at threshold: not counted
        r, g, b = changed[7]
        changed[7] = ((r + 40) % 256, g, b)  # over threshold: counted
        a = self.write('a.bmp', bmp(BASE, W, H))
        c = self.write('c.bmp', bmp(changed, W, H))
        result = cmp.compare(cmp.read_image(a), cmp.read_image(c), 2)
        self.assertEqual(result.differing, 1)
        self.assertEqual(result.max_diff, 40)
        mse = (2 * 2 + 40 * 40) / (W * H * 3)
        self.assertAlmostEqual(result.psnr, 10 * math.log10(255 * 255 / mse))
        # 1 of 15 pixels = 6.67%: fails the default 0.5% limit, passes a 10% limit.
        mask = self.root / 'mask.png'
        code, out = self.run_main(a, c, '--mask', mask)
        self.assertEqual(code, 1)
        self.assertIn('FAIL', out)
        self.assertEqual(self.run_main(a, c, '--limit', 10)[0], 0)
        # Threshold 40 means a difference must exceed 40 to count.
        self.assertEqual(cmp.compare(cmp.read_image(a), cmp.read_image(c), 40).differing, 0)
        grey = cmp.read_png(mask.read_bytes())
        white = [i for i in range(W * H) if grey.rgb[i * 3] == 255]
        self.assertEqual(white, [7])
        self.assertTrue(all(grey.rgb[i * 3] == 0 for i in range(W * H) if i != 7))

    def test_size_mismatch_and_bad_input_exit_2(self):
        a = self.write('a.bmp', bmp(BASE, W, H))
        small = self.write('s.bmp', bmp(BASE[:W], W, 1))
        junk = self.write('j.bmp', b'not an image')
        self.assertEqual(self.run_main(a, small)[0], 2)
        self.assertEqual(self.run_main(a, junk)[0], 2)


if __name__ == '__main__':
    unittest.main()
