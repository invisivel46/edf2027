#!/usr/bin/env python3
"""Unit tests for compare-renderer-ab-captures.py: python tools/test_compare_renderer_ab_captures.py"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location(
    "compare_renderer_ab_captures", Path(__file__).with_name("compare-renderer-ab-captures.py"))
ab = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = ab
_spec.loader.exec_module(ab)

W, H = 10, 10  # 100 pixels: one changed pixel is one percent


def bmp(changed: int) -> bytes:
    """Bottom-up 24-bit BMP, grey 64 with the first `changed` pixels set to 200."""
    pitch = (W * 3 + 3) & ~3
    pixels = [200 if i < changed else 64 for i in range(W * H)]
    rows = bytearray()
    for y in range(H - 1, -1, -1):
        row = b"".join(bytes((pixels[y * W + x],) * 3) for x in range(W))
        rows += row + b"\0" * (pitch - len(row))
    header = b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(rows), 0, 0, 0, 0)
    return header + info + bytes(rows)


def log(sides: dict[int, int]) -> str:
    lines = ["[info] unrelated line"]
    lines += [f"[info] ab_alternate frame={f} native={n}" for f, n in sorted(sides.items())]
    return "\n".join(lines) + "\n"


class Fixture:
    def __init__(self, test: unittest.TestCase, sides: dict[int, int], changed: dict[int, int],
                 prefix: str = "cap"):
        tmp = tempfile.TemporaryDirectory()
        test.addCleanup(tmp.cleanup)
        self.dir = Path(tmp.name)
        for frame, count in changed.items():
            (self.dir / f"{prefix}.output.{frame}.bmp").write_bytes(bmp(count))
        self.log = self.dir / "game.log"
        self.log.write_text(log(sides))

    def run(self, *extra: str) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            code = ab.main([str(self.dir), str(self.log), *extra])
        return code, out.getvalue()


class ParseTests(unittest.TestCase):
    def test_log(self):
        self.assertEqual(ab.parse_log(log({600: 0, 601: 1})), {600: False, 601: True})
        with self.assertRaises(ab.AbError):
            ab.parse_log("ab_alternate frame=5 native=0\nab_alternate frame=5 native=1\n")

    def test_plan_period_one_uses_step_two(self):
        sides = {f: f % 2 == 1 for f in range(600, 606)}
        p = ab.plan(sides, set(sides))
        self.assertEqual(p.pairs, [(600, 601), (602, 603), (604, 605)])
        self.assertEqual((p.baseline_step, p.baseline), (2, [(600, 602), (602, 604)]))

    def test_plan_period_two_uses_step_one(self):
        sides = {f: ((f - 600) // 2) % 2 == 1 for f in range(600, 608)}
        p = ab.plan(sides, set(sides))
        self.assertEqual(p.pairs, [(601, 602), (605, 606)])
        self.assertEqual((p.baseline_step, p.baseline), (1, [(600, 601), (604, 605)]))

    def test_plan_skips_missing_captures(self):
        sides = {f: f % 2 == 1 for f in range(600, 606)}
        p = ab.plan(sides, set(sides) - {601})
        self.assertEqual(p.pairs, [(602, 603), (604, 605)])


class MainTests(unittest.TestCase):
    sides = {f: f % 2 for f in range(600, 606)}

    def test_within_baseline_passes(self):
        # guest frames drift by 4 pixels per two frames; native adds no more
        code, out = Fixture(self, self.sides, {600: 0, 601: 3, 602: 4, 603: 5, 604: 8, 605: 9}).run("--margin", "0")
        self.assertEqual(code, 0, out)
        self.assertIn("step=2", out)
        self.assertIn("summary pairs=3 failed=0", out)

    def test_native_regression_fails(self):
        code, out = Fixture(self, self.sides, {600: 0, 601: 3, 602: 4, 603: 60, 604: 8, 605: 9}).run("--margin", "1")
        self.assertEqual(code, 1, out)
        self.assertIn("guest=602 native=603 differing=56.0000%", out)
        self.assertIn("failed=1", out)

    def test_margin_absorbs_small_excess(self):
        fixture = Fixture(self, self.sides, {600: 0, 601: 6, 602: 4, 603: 4, 604: 8, 605: 9})
        self.assertEqual(fixture.run("--margin", "0")[0], 1)
        self.assertEqual(fixture.run("--margin", "2")[0], 0)

    def test_median_baseline(self):
        sides = {f: f % 2 for f in range(600, 608)}
        fixture = Fixture(self, sides, {600: 0, 601: 3, 602: 2, 603: 5, 604: 4, 605: 7, 606: 14, 607: 15})
        self.assertEqual(fixture.run("--margin", "0")[0], 0)  # max baseline 10%
        self.assertEqual(fixture.run("--margin", "0", "--baseline-stat", "median")[0], 1)  # median 2%

    def test_prefix_filters_other_runs(self):
        fixture = Fixture(self, self.sides, {600: 0, 601: 0, 602: 0, 603: 0})
        (fixture.dir / "other.output.600.bmp").write_bytes(bmp(W * H))
        self.assertEqual(fixture.run()[0], 2)
        self.assertEqual(fixture.run("--prefix", "cap")[0], 0)

    def test_missing_log_lines(self):
        code, out = Fixture(self, {}, {600: 0, 601: 0}).run()
        self.assertEqual(code, 2)
        self.assertIn("no ab_alternate lines", out)

    def test_no_baseline(self):
        code, out = Fixture(self, self.sides, {600: 0, 601: 0}).run()
        self.assertEqual(code, 2)
        self.assertIn("baseline", out)


if __name__ == "__main__":
    unittest.main()
