#!/usr/bin/env python3
"""Compare A/B-alternated renderer captures (guest vs native on consecutive frames).

Run the game with edf_native_ab_alternate=N, edf_native_scene_capture=<prefix>,
edf_native_output_capture_start_frame=S, edf_native_output_capture_interval=1
and a large enough edf_native_output_capture_limit (at most 128). Each render
helper call logs `ab_alternate frame=F native=0|1`, and each capture is
<prefix>.output.F.bmp with the same F.

Every guest frame F whose next frame F+1 is native forms a guest/native pair.
The scene still moves between F and F+1, so each pair is judged against the
guest-vs-guest baseline: consecutive guest frames (F, F+1) when N >= 2, else
guest frames (F, F+2). A pair passes when its differing percentage is within
the baseline (max by default) plus --margin percentage points.
Exits 0 when every pair passes, 1 on a failing pair, 2 on unusable input.
"""
from __future__ import annotations

import argparse
import importlib.util
import math
import re
import statistics
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


def _load_images():
    spec = importlib.util.spec_from_file_location(
        "compare_renderer_images", Path(__file__).with_name("compare-renderer-images.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault(spec.name, module)
    spec.loader.exec_module(module)
    return module


images = _load_images()

LOG_LINE = re.compile(r"ab_alternate frame=(\d+) native=([01])")
CAPTURE_NAME = re.compile(r"\.output\.(\d+)\.(?:bmp|png)$", re.IGNORECASE)


class AbError(Exception):
    pass


def parse_log(text: str) -> dict[int, bool]:
    """Frame -> native side, from `ab_alternate frame=F native=0|1` lines."""
    sides: dict[int, bool] = {}
    for match in LOG_LINE.finditer(text):
        frame, native = int(match.group(1)), match.group(2) == "1"
        if sides.get(frame, native) != native:
            raise AbError(f"frame {frame} logged as both sides")
        sides[frame] = native
    return sides


def find_captures(directory: Path, prefix: str | None) -> dict[int, Path]:
    captures: dict[int, Path] = {}
    for path in sorted(directory.iterdir()):
        match = CAPTURE_NAME.search(path.name)
        if not match or (prefix and not path.name.startswith(prefix + ".output.")):
            continue
        frame = int(match.group(1))
        if frame in captures:
            raise AbError(f"frame {frame} captured twice: {captures[frame].name}, {path.name} (use --prefix)")
        captures[frame] = path
    return captures


@dataclass
class Pairing:
    pairs: list[tuple[int, int]]      # (guest F, native F+1)
    baseline: list[tuple[int, int]]   # (guest, guest)
    baseline_step: int                # 1 or 2; 0 when no baseline exists


def plan(sides: dict[int, bool], captured: set[int]) -> Pairing:
    usable = {f: n for f, n in sides.items() if f in captured}
    guest = sorted(f for f, n in usable.items() if not n)
    pairs = [(f, f + 1) for f in guest if usable.get(f + 1) is True]
    for step in (1, 2):
        baseline = [(f, f + step) for f in guest if usable.get(f + step) is False]
        if baseline:
            return Pairing(pairs, baseline, step)
    return Pairing(pairs, [], 0)


@dataclass
class Result:
    first: int
    second: int
    percent: float
    max_diff: int
    psnr: float


def measure(captures: dict[int, Path], frames: list[tuple[int, int]], threshold: int,
            cache: dict[int, object]) -> list[Result]:
    def load(frame: int):
        if frame not in cache:
            cache[frame] = images.read_image(captures[frame])
        return cache[frame]
    results = []
    for a, b in frames:
        c = images.compare(load(a), load(b), threshold)
        results.append(Result(a, b, c.percent, c.max_diff, c.psnr))
    return results


def _psnr(value: float) -> str:
    return "inf" if math.isinf(value) else f"{value:.2f}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", type=Path, help="directory holding <prefix>.output.<F>.bmp captures")
    parser.add_argument("log", type=Path, help="game log containing ab_alternate lines")
    parser.add_argument("--prefix", help="capture file-name prefix (base name of edf_native_scene_capture)")
    parser.add_argument("--threshold", type=int, default=2,
                        help="per-channel difference (0-255) a pixel must exceed to count (default 2)")
    parser.add_argument("--margin", type=float, default=0.5,
                        help="allowed percentage points above the baseline (default 0.5)")
    parser.add_argument("--baseline-stat", choices=("max", "median"), default="max",
                        help="guest-vs-guest statistic the pairs are held to (default max)")
    args = parser.parse_args(argv)
    if not 0 <= args.threshold <= 255:
        parser.error("--threshold must be within 0..255")
    try:
        sides = parse_log(args.log.read_text(encoding="utf-8", errors="replace"))
        if not sides:
            raise AbError("log has no ab_alternate lines (edf_native_ab_alternate off?)")
        captures = find_captures(args.captures, args.prefix)
        unlogged = sorted(set(captures) - set(sides))
        if unlogged:
            print(f"warning: {len(unlogged)} captures have no logged side, ignored (first {unlogged[0]})")
        pairing = plan(sides, set(captures))
        if not pairing.pairs:
            raise AbError("no guest frame F with a captured native frame F+1")
        if not pairing.baseline:
            raise AbError("no guest/guest frame pair (F,F+1) or (F,F+2) for the baseline")
        cache: dict[int, object] = {}
        baseline = measure(captures, pairing.baseline, args.threshold, cache)
        pairs = measure(captures, pairing.pairs, args.threshold, cache)
    except (OSError, AbError, images.ImageError, zlib.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    percents = [r.percent for r in baseline]
    reference = max(percents) if args.baseline_stat == "max" else statistics.median(percents)
    limit = reference + args.margin
    print(f"baseline guest/guest step={pairing.baseline_step} pairs={len(baseline)} "
          f"median={statistics.median(percents):.4f}% max={max(percents):.4f}% "
          f"min_psnr_db={_psnr(min(r.psnr for r in baseline))}")
    failed = 0
    for r in pairs:
        passed = r.percent <= limit
        failed += not passed
        print(f"guest={r.first} native={r.second} differing={r.percent:.4f}% max_diff={r.max_diff} "
              f"psnr_db={_psnr(r.psnr)} limit={limit:.4f}% result={'PASS' if passed else 'FAIL'}")
    print(f"summary pairs={len(pairs)} failed={failed} baseline_{args.baseline_stat}={reference:.4f}% "
          f"margin={args.margin} result={'PASS' if not failed else 'FAIL'}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
