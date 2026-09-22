#!/usr/bin/env python3
"""Unit tests for renderer-ab-postprocess.py: python -m unittest tools.test_renderer_ab_postprocess"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "renderer_ab_postprocess", Path(__file__).with_name("renderer-ab-postprocess.py"))
post = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = post
_spec.loader.exec_module(post)
images = post.images

W, H = 40, 20


def bmp(pixel, width: int = W, height: int = H) -> bytes:
    """Bottom-up 24-bit BMP; pixel(x, y) -> (r, g, b) with y top-down."""
    pitch = (width * 3 + 3) & ~3
    rows = bytearray()
    for y in range(height - 1, -1, -1):
        row = b"".join(bytes(pixel(x, y)[::-1]) for x in range(width))
        rows += row + b"\0" * (pitch - len(row))
    header = b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(rows), 0, 0, 0, 0)
    return header + info + bytes(rows)


def gameplay(x, y):
    return ((x * 6) % 256, (y * 12) % 256, ((x + y) * 5) % 256)


def gameplay_changed(x, y):
    return (255, 0, 0) if x < 4 and y < 2 else gameplay(x, y)  # 8 of 800 pixels = 1%


def loading(x, y):
    # black screen with a small "NOW LOADING" block in the lower right corner
    return (230, 230, 230) if x >= 32 and y >= 16 else (0, 0, 0)


def flat(x, y):
    return (90, 90, 90)


def image(pixel, width=W, height=H):
    return images.read_bmp(bmp(pixel, width, height))


class ClassifyTests(unittest.TestCase):
    def test_gameplay(self):
        result = post.classify(image(gameplay), sample_step=1)
        self.assertEqual(result["classification"], "gameplay", result)
        self.assertEqual(result["reasons"], [])

    def test_dark_loading_screen(self):
        result = post.classify(image(loading), sample_step=1)
        self.assertEqual(result["classification"], "loading")
        self.assertEqual(result["dark_fraction"], 0.96)  # 32 of 800 lit
        self.assertTrue(any("dark_fraction" in r for r in result["reasons"]))

    def test_flat_fill(self):
        result = post.classify(image(flat), sample_step=1)
        self.assertEqual((result["classification"], result["stddev_luma"]), ("loading", 0.0))
        self.assertTrue(any("stddev" in r for r in result["reasons"]))

    def test_thresholds_are_parameters(self):
        self.assertEqual(post.classify(image(loading), dark_fraction=0.99, flat_stddev=0,
                                       sample_step=1)["classification"], "gameplay")

    def test_sampling_grid(self):
        self.assertEqual(post.luma_stats(image(gameplay), sample_step=4)["samples"], 10 * 5)


class ThumbnailTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)

    def test_stdlib_png_roundtrip(self):
        path = self.dir / "t.png"
        post.write_thumbnail(image(gameplay), path, 20, use_pillow=False)
        small = images.read_png(path.read_bytes())
        self.assertEqual((small.width, small.height), (20, 10))
        self.assertEqual(small.rgb[:3], bytes(gameplay(0, 0)))

    def test_pillow_path_when_available(self):
        if post._PilImage is None:
            self.skipTest("Pillow not installed")
        path = self.dir / "t.png"
        post.write_thumbnail(image(gameplay), path, 20, use_pillow=True)
        self.assertEqual(images.read_png(path.read_bytes()).width, 20)

    def test_without_pillow_falls_back_to_stdlib(self):
        saved, post._PilImage = post._PilImage, None
        try:
            path = self.dir / "t.png"
            post.write_thumbnail(image(gameplay), path, 20, use_pillow=True)
            self.assertEqual(images.read_png(path.read_bytes()).rgb[:3], bytes(gameplay(0, 0)))
        finally:
            post._PilImage = saved

    def test_small_image_is_not_upscaled(self):
        self.assertEqual(post.thumb_size(40, 20, 320), (40, 20))

    def test_mask_max_pool_keeps_single_pixel(self):
        mask = bytearray(W * H)
        mask[5 * W + 7] = 255
        small = post.downsample_mask(bytes(mask), W, H, 10, 5)
        self.assertEqual(small.count(255), 1)
        self.assertEqual(small[1 * 10 + 1], 255)


class PairTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.base = self.root / "base"
        self.cand = self.root / "cand"
        self.base.mkdir()
        self.cand.mkdir()

    def write(self, directory: Path, frame: int, pixel, prefix: str = "cap"):
        (directory / f"{prefix}.output.{frame}.bmp").write_bytes(bmp(pixel))

    def test_matching_frames_loading_mismatch_and_orphans(self):
        self.write(self.base, 600, gameplay)
        self.write(self.cand, 600, gameplay)
        self.write(self.base, 1200, gameplay)
        self.write(self.cand, 1200, gameplay_changed)
        self.write(self.base, 1800, gameplay)
        self.write(self.cand, 1800, loading)  # candidate was still loading
        self.write(self.base, 2400, gameplay)
        self.write(self.cand, 3000, gameplay)
        self.write(self.cand, 600, flat, prefix="other")  # another prefix is ignored
        out = self.root / "pair-1"
        report = post.process_pair(self.base, self.cand, out, use_pillow=False, limit=0.5)
        self.assertEqual(report["matched"], 3)
        self.assertEqual(report["baseline_only"], [2400])
        self.assertEqual(report["candidate_only"], [3000])
        self.assertEqual(report["candidate_loading"], [1800])
        self.assertEqual(report["baseline_loading"], [])
        self.assertEqual(report["loading_mismatch"], [1800])
        frames = {f["frame"]: f for f in report["frames"]}
        self.assertEqual(frames[600]["compare"]["differing_percent"], 0.0)
        self.assertEqual(frames[600]["compare"]["psnr_db"], "inf")
        self.assertEqual(frames[1200]["compare"]["differing_percent"], 1.0)
        self.assertIn(1200, report["compare_failed"])
        self.assertTrue((out / frames[1200]["compare"]["mask_thumbnail"]).is_file())
        self.assertTrue((out / frames[3000]["candidate"]["thumbnail"]).is_file())
        saved = json.loads((out / "postprocess.json").read_text(encoding="utf-8"))
        self.assertEqual(saved["loading_mismatch"], [1800])

    def test_size_mismatch_and_unreadable(self):
        self.write(self.base, 600, gameplay)
        (self.cand / "cap.output.600.bmp").write_bytes(bmp(gameplay, 20, 10))
        (self.cand / "cap.output.1200.bmp").write_bytes(b"BMgarbage")
        report = post.process_pair(self.base, self.cand, self.root / "pair-1", use_pillow=False)
        frames = {f["frame"]: f for f in report["frames"]}
        self.assertIn("size mismatch", frames[600]["compare"]["error"])
        self.assertEqual(len(report["errors"]), 1)

    def test_missing_directories(self):
        report = post.process_pair(self.root / "none", self.root / "nope", self.root / "pair-1")
        self.assertEqual((report["matched"], report["frames"]), (0, []))

    def test_ab_alternate_on_candidate(self):
        for frame in range(600, 606):
            self.write(self.base, frame, gameplay)
            self.write(self.cand, frame, gameplay)
        log = self.cand / "game.log"
        log.write_text("".join(f"ab_alternate frame={f} native={f % 2}\n" for f in range(600, 606)))
        report = post.process_pair(self.base, self.cand, self.root / "pair-1", use_pillow=False,
                                   candidate_log=log, ab_alternate=True)
        ab = report["ab_alternate"]
        self.assertTrue(ab["passed"], ab)
        self.assertEqual(ab["summary"], {"pairs": 3, "failed": 0, "passed": True})
        self.assertEqual(ab["baseline"]["step"], 2)
        self.assertEqual([p["guest"] for p in ab["pairs"]], [600, 602, 604])
        sides = {f["frame"]: f.get("candidate_side") for f in report["frames"]}
        self.assertEqual((sides[600], sides[601]), ("guest", "native"))

    def test_ab_alternate_without_log(self):
        self.write(self.cand, 600, gameplay)
        report = post.process_pair(self.base, self.cand, self.root / "pair-1", ab_alternate=True,
                                   candidate_log=self.cand / "missing.log")
        self.assertFalse(report["ab_alternate"]["passed"])

    def test_parse_ab_output(self):
        text = ("baseline guest/guest step=1 pairs=2 median=0.1000% max=0.2000% min_psnr_db=40.00\n"
                "guest=601 native=602 differing=3.5000% max_diff=90 psnr_db=31.20 limit=0.7000% result=FAIL\n"
                "summary pairs=1 failed=1 baseline_max=0.2000% margin=0.5 result=FAIL\n")
        parsed = post.parse_ab_output(text)
        self.assertEqual(parsed["pairs"][0]["differing_percent"], 3.5)
        self.assertFalse(parsed["pairs"][0]["passed"])
        self.assertEqual(parsed["summary"]["failed"], 1)

    def test_sheet_and_cli(self):
        self.write(self.base, 600, gameplay)
        self.write(self.cand, 600, loading)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = post.main(["pair", "--baseline-dir", str(self.base), "--candidate-dir", str(self.cand),
                              "--out", str(self.root / "pair-2"), "--no-pillow", "--label", "run 2"])
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out.getvalue())["loading_mismatch"], [600])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(post.main(["sheet", str(self.root)]), 0)
        page = (self.root / "contact-sheet.html").read_text(encoding="utf-8")
        self.assertIn('src="pair-2/thumbs/baseline.600.png"', page)
        self.assertIn('src="pair-2/thumbs/candidate.600.png"', page)
        self.assertIn("pair-2/thumbs/diff.600.png", page)
        self.assertIn("candidate looks like loading", page)
        self.assertIn("loading vs gameplay", page)
        self.assertIn("heuristic", page)

    def test_empty_sheet(self):
        page = post.render_sheet(self.root).read_text(encoding="utf-8")
        self.assertIn("No pair captures", page)


if __name__ == "__main__":
    unittest.main()
