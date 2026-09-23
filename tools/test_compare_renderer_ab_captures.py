#!/usr/bin/env python3
"""Synthetic-image tests for compare-renderer-ab-captures.py.

  python -m unittest tools.test_compare_renderer_ab_captures
  python tools/compare-renderer-ab-captures.py --self-test
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path

_name = "compare_renderer_ab_captures"
if _name in sys.modules:
    ab = sys.modules[_name]
else:
    _spec = importlib.util.spec_from_file_location(_name, Path(__file__).with_name("compare-renderer-ab-captures.py"))
    ab = importlib.util.module_from_spec(_spec)
    sys.modules[_name] = ab
    _spec.loader.exec_module(ab)

# 96x64 px, 4 px blocks, tiles of 2x2 blocks: 12x8 = 96 tiles of 8x8 px.
W, H = 96, 64
SMALL = ["--block", "4", "--tile", "2"]
SKY = (200, 210, 220)


def scene(frame: int, hud: bool = True):
    """A textured scene panning one pixel per frame, with a static HUD square at (80..88, 4..12)."""
    def pixel(x, y):
        if hud and 80 <= x < 88 and 4 <= y < 12:
            return (40, 230, 90)
        u = x + frame
        v = int(120 + 40 * math.sin(u / 30.0) * math.cos(y / 12.0))
        return (v, v + 40, int(100 + 40 * math.sin((u + y) / 30.0)))
    return pixel


def without_hud(frame: int):
    return scene(frame, hud=False)


def cut_far(frame: int):
    """Everything above y = 24 in the left two thirds replaced by sky: a large structured loss."""
    base = scene(frame)
    return lambda x, y: SKY if y < 24 and x < 64 else base(x, y)


def black(frame: int):
    return lambda x, y: (0, 0, 0)


def bmp(pixel) -> bytes:
    """Bottom-up 24-bit BMP; pixel(x, y) -> (r, g, b) with y top-down."""
    pitch = (W * 3 + 3) & ~3
    rows = bytearray()
    for y in range(H - 1, -1, -1):
        row = b"".join(bytes(pixel(x, y)[::-1]) for x in range(W))
        rows += row + b"\0" * (pitch - len(row))
    header = b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(rows), 0, 0, 0, 0)
    return header + info + bytes(rows)


class Fixture:
    """Frames 600..600+count-1, period 1 from 600 (odd frames native). native(frame) draws native frames."""

    def __init__(self, test: unittest.TestCase, native=scene, count: int = 12, log: bool = True,
                 prefix: str = "cap"):
        tmp = tempfile.TemporaryDirectory()
        test.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        self.log = self.dir / "game.log"
        lines = ["[info] unrelated line"]
        for frame in range(600, 600 + count):
            is_native = frame % 2 == 1
            draw = native(frame) if is_native else scene(frame)
            (self.dir / f"{prefix}.output.{frame}.bmp").write_bytes(bmp(draw))
            lines.append(f"[info] ab_alternate frame={frame} native={int(is_native)}")
        if log:
            self.log.write_text("\n".join(lines) + "\n")

    def run(self, *extra: str, log: bool = True) -> tuple[int, dict, str]:
        out, err = io.StringIO(), io.StringIO()
        argv = [str(self.dir)] + ([str(self.log)] if log else []) + SMALL + list(extra)
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = ab.main(argv)
        return code, json.loads(out.getvalue()), err.getvalue()


class SideTests(unittest.TestCase):
    def test_parse_log(self):
        self.assertEqual(ab.parse_log("ab_alternate frame=600 native=0\nab_alternate frame=601 native=1\n"),
                         {600: False, 601: True})
        with self.assertRaises(ab.AbError):
            ab.parse_log("ab_alternate frame=5 native=0\nab_alternate frame=5 native=1\n")

    def test_parse_log_reuse_tag(self):
        # edf_native_reuse_off_alternate logs its own tag; each tag reads only its own lines.
        text = "reuse_alternate frame=600 native=0\nreuse_alternate frame=601 native=1\nab_alternate frame=9 native=1\n"
        self.assertEqual(ab.parse_log(text, "reuse_alternate"), {600: False, 601: True})
        self.assertEqual(ab.parse_log(text), {9: True})
        self.assertEqual(ab.parse_log("xreuse_alternate frame=3 native=1\n", "reuse_alternate"), {})

    def test_period_sides_match_the_game(self):
        # AbSide in native_ab_alternate.h: runs of N from the start frame, odd runs native.
        self.assertEqual(ab.period_sides(range(10, 16), 10, 1), {10: False, 11: True, 12: False, 13: True,
                                                                 14: False, 15: True})
        self.assertEqual([ab.period_sides([f], 10, 2)[f] for f in range(8, 16)],
                         [False, False, False, False, True, True, False, False])

    def test_plan_period_one(self):
        sides = ab.period_sides(range(600, 606), 600, 1)
        triples, skipped = ab.plan(sides, set(sides), 4)
        self.assertEqual([(t.prev, t.frame, t.next) for t in triples], [(600, 601, 602), (602, 603, 604)])
        self.assertEqual(skipped, [605])
        control, _ = ab.plan(sides, set(sides), 4, control=True)
        self.assertEqual([(t.prev, t.frame, t.next) for t in control], [(600, 602, 604)])

    def test_plan_period_two_and_missing_capture(self):
        sides = ab.period_sides(range(600, 608), 600, 2)
        triples, _ = ab.plan(sides, set(sides), 4)
        self.assertEqual([(t.prev, t.frame, t.next) for t in triples], [(601, 602, 604), (601, 603, 604)])
        triples, skipped = ab.plan(sides, set(sides) - {604}, 4)
        self.assertEqual([(t.prev, t.frame, t.next) for t in triples], [(601, 602, 605), (601, 603, 605)])
        self.assertEqual(ab.plan(sides, set(sides) - {604, 605}, 4)[1], [602, 603, 606, 607])


class MetricTests(unittest.TestCase):
    def test_largest_region(self):
        flags = [bool(int(c)) for c in "1100" "1001" "0011"]
        self.assertEqual(ab.largest_region(flags, 4, 3), 3)
        self.assertEqual(ab.largest_region([False] * 6, 3, 2), 0)

    def test_dilate(self):
        values = [0.0] * 9
        values[4] = 5.0
        self.assertEqual(ab.dilate(values, 3, 3, 1), [5.0] * 9)
        values = [0.0] * 9
        values[0] = 2.0
        self.assertEqual(ab.dilate(values, 3, 3, 1), [2, 2, 0, 2, 2, 0, 0, 0, 0])
        self.assertIs(ab.dilate(values, 3, 3, 0), values)

    def test_tile_map_partial_edge(self):
        tw, th, tiles = ab.tile_map([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 3, 2, 2)
        self.assertEqual((tw, th), (2, 1))
        self.assertEqual(tiles, [3.0, 4.5])

    def test_block_means(self):
        image = ab.images.read_bmp(bmp(scene(0)))
        blocks = ab.block_means(image, 4)
        self.assertEqual((blocks.bw, blocks.bh), (24, 16))
        self.assertAlmostEqual(blocks.rgb[0], sum(scene(0)(x, y)[0] for x in range(4) for y in range(4)) / 16)


class GateTests(unittest.TestCase):
    def test_matching_native_passes(self):
        code, report, _ = Fixture(self).run()
        self.assertEqual(code, 0, report)
        self.assertTrue(report["passed"])
        self.assertEqual(report["summary"]["native_frames"], 5)
        self.assertEqual(report["summary"]["failing_frames"], [])
        self.assertEqual(report["control"]["failing_frames"], [])
        self.assertIn("not judged: [611]", " ".join(report["warnings"]))

    def test_large_structured_loss_fails(self):
        code, report, _ = Fixture(self, native=cut_far).run()
        self.assertEqual(code, 1)
        self.assertEqual(report["summary"]["failing_frames"], [601, 603, 605, 607, 609])
        worst = report["summary"]["worst"]
        self.assertGreater(worst["largest_region"], 0.2)
        self.assertTrue(any(f.startswith("largest_region") for f in worst["failures"]))
        self.assertEqual(report["control"]["failing_frames"], [])

    def test_black_frame_fails_as_blank(self):
        code, report, _ = Fixture(self, native=black).run()
        self.assertEqual(code, 1)
        self.assertTrue(any(f.startswith("blank") for f in report["summary"]["worst"]["failures"]))

    def test_missing_hud_fails_as_persistent(self):
        # One HUD tile per frame is under the per-frame limits; it is bad in every native frame.
        code, report, _ = Fixture(self, native=without_hud).run()
        self.assertEqual(code, 1, report)
        self.assertEqual(report["summary"]["failing_frames"], [])
        self.assertEqual({(t["x"], t["y"]) for t in report["summary"]["persistent_tiles"]}, {(10, 0), (10, 1)})
        self.assertIn("tiles bad in at least 0.5 of the native frames", report["failures"][0])
        self.assertEqual(Fixture(self, native=without_hud).run("--max-persistent-tiles", "2")[0], 0)

    def test_one_transient_frame(self):
        fixture = Fixture(self, native=lambda f: cut_far(f) if f == 605 else scene(f))
        code, report, _ = fixture.run()
        self.assertEqual((code, report["summary"]["failing_frames"]), (1, [605]))
        self.assertEqual(fixture.run("--max-failing-frames", "0.25")[0], 0)

    def test_period_without_log(self):
        fixture = Fixture(self, native=cut_far, log=False)
        code, report, _ = fixture.run("--period", "1", log=False)
        self.assertEqual(code, 1)
        self.assertEqual(report["sides_from"], "period=1 start=600")

    def test_diff_image(self):
        fixture = Fixture(self, native=cut_far)
        out = fixture.dir / "worst.png"
        code, report, _ = fixture.run("--diff-image", str(out))
        self.assertEqual(code, 1)
        image = ab.images.read_image(out)
        self.assertEqual((image.width, image.height), (W, H))  # 2x2 panels at half size
        # the lower-left panel tints the bad tiles red
        tinted = image.rgb[(H // 2 + 2) * W * 3 + 2 * 3: (H // 2 + 2) * W * 3 + 3 * 3]
        self.assertGreater(tinted[0], tinted[1])

    def test_unusable_input(self):
        fixture = Fixture(self, log=False)
        fixture.log.write_text("no alternate lines\n")
        code, report, err = fixture.run()
        self.assertEqual(code, 2)
        self.assertIn("no ab_alternate lines", report["error"])
        fixture = Fixture(self, count=2)
        code, report, _ = fixture.run()
        self.assertEqual(code, 2)
        self.assertIn("both sides", report["error"])

    def test_prefix_filters_other_runs(self):
        fixture = Fixture(self)
        (fixture.dir / "other.output.600.bmp").write_bytes(bmp(black(0)))
        self.assertEqual(fixture.run()[0], 2)
        self.assertEqual(fixture.run("--prefix", "cap")[0], 0)


if __name__ == "__main__":
    unittest.main()
