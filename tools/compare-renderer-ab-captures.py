#!/usr/bin/env python3
"""Image-correctness gate for A/B-alternated renderer captures.

Run the game with --edf_native_ab_alternate=N, --edf_native_scene_capture=<prefix>,
--edf_native_output_capture_start_frame=S, --edf_native_output_capture_interval=1
and --edf_native_output_capture_limit=64 (at most 128). Frames are grouped in
runs of N from S and odd runs are native. Each render helper call logs
`ab_alternate frame=F native=0|1`, and each capture is <prefix>.output.F.bmp.
Sides come from that log; without a log, --period N (and --start S, default
the first captured frame) recomputes them the way the game does.

The same gate compares reuse-off frames with reuse-on frames: run with
--edf_native_reuse_off_alternate=N instead of edf_native_ab_alternate. The
same rule picks the sides (reuse off on the reference side, reuse on judged),
logged as `reuse_alternate frame=F native=0|1`; pass --tag reuse_alternate
with the log, or --period N as above.

Every native frame F is judged against its nearest captured guest frames
before (P) and after (Q), at most --max-gap frames away. The scene moves
between frames, so the native error is held to the guest-vs-guest error
between P and Q, which spans at least as much motion as F-vs-P or F-vs-Q.

Metrics work on 8x8-pixel block means (--block), grouped into tiles of
--tile x --tile blocks (40x40 px by default). The block error is the mean
absolute RGB difference of two block means (0..255), the tile error the mean
over its blocks. Averaging hides sub-block jitter (moving edges, dithering)
but keeps what matters here: a region whose colour or coarse structure
changed. Per native frame and tile:
  error  = min(tile error vs P, tile error vs Q)
  noise  = tile error P vs Q, maximum over the tile and its --dilate
           neighbours (an object moving across tiles is noise next to them too)
  bad    = error > --noise-ratio * noise + --floor
The frame fails when any of these holds:
  bad_tiles      fraction of bad tiles > --max-bad-tiles
  largest_region largest 4-connected group of bad tiles, as a fraction of all
                 tiles, > --max-region (catches one large missing area, such as
                 distant geometry cut away or the HUD missing)
  blank          the native frame is nearly flat (block luma stddev below
                 --blank-stddev) while its guest neighbours are not (black or
                 clear-colour-only frames)
The gate fails when more than --max-failing-frames (a fraction of the judged
native frames, default 0) fail, or when more than --max-persistent-tiles
(default 0) tiles are bad in at least --persist-fraction (default half) of the
native frames. Motion transients (a close object crossing the view) move
between frames. A small lasting loss stays on the same tile, such as one
missing HUD element that is too small for the per-frame limits.

The control judges every guest frame against its own guest neighbours the
same way ("guest as native"; --no-control skips it). It measures the gate's false-positive rate on
this capture set; a failing control does not fail the gate, it adds a warning
that the capture moves too much for the thresholds.

Prints one JSON report (like tools/renderer-runtime-gate.py). --diff-image
writes a PNG of the worst native frame: guest P | native F on top, the native
frame with bad tiles tinted red | the per-block error over the per-block limit
below. Exits 0 on pass, 1 on fail, 2 on unusable input. --self-test runs the
synthetic-image tests in tools/test_compare_renderer_ab_captures.py.
Standard library only.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import statistics
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


def _load_images():
    name = "compare_renderer_images"
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name("compare-renderer-images.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


images = _load_images()

LOG_LINE = re.compile(r"ab_alternate frame=(\d+) native=([01])")
TAGS = ("ab_alternate", "reuse_alternate")


def log_line(tag: str) -> re.Pattern:
    """The side line one alternation logs (edf_native_ab_alternate, edf_native_reuse_off_alternate)."""
    return re.compile(r"(?<![A-Za-z0-9_])" + re.escape(tag) + r" frame=(\d+) native=([01])")
CAPTURE_NAME = re.compile(r"\.output\.(\d+)\.(?:bmp|png)$", re.IGNORECASE)

# Calibrated on out/renderer-ab/ab-5c7d6e9 (distant static geometry lost past
# ~100-150 m: must fail) and its guest-as-native control (must pass); see
# docs/renderer-status.md section 6.
DEFAULTS = {
    "block": 8,
    "tile": 5,
    "noise_ratio": 2.0,
    "floor": 6.0,
    "max_bad_tiles": 0.08,
    "max_region": 0.05,
    "blank_stddev": 2.0,
    "dilate": 1,
    "persist_fraction": 0.5,
    "persist_min_frames": 4,
    "max_persistent_tiles": 0,
    "max_failing_frames": 0.0,
    "max_gap": 4,
}


class AbError(Exception):
    pass


def parse_log(text: str, tag: str = "ab_alternate") -> dict[int, bool]:
    """Frame -> native side, from `<tag> frame=F native=0|1` lines."""
    sides: dict[int, bool] = {}
    for match in (LOG_LINE if tag == "ab_alternate" else log_line(tag)).finditer(text):
        frame, native = int(match.group(1)), match.group(2) == "1"
        if sides.get(frame, native) != native:
            raise AbError(f"frame {frame} logged as both sides")
        sides[frame] = native
    return sides


def period_sides(frames, start: int, period: int) -> dict[int, bool]:
    """The game's AbSide (native_ab_alternate.h): runs of `period` from `start`, odd runs native."""
    return {f: f >= start and ((f - start) // period) % 2 == 1 for f in frames}


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


# ------------------------------------------------------------------ planning

@dataclass
class Triple:
    frame: int  # the frame judged (native, or a guest frame in the control)
    prev: int   # nearest captured guest frame before
    next: int   # nearest captured guest frame after


def neighbours(frame: int, guest: set[int], max_gap: int) -> tuple[int | None, int | None]:
    prev = next((frame - d for d in range(1, max_gap + 1) if frame - d in guest), None)
    after = next((frame + d for d in range(1, max_gap + 1) if frame + d in guest), None)
    return prev, after


def plan(sides: dict[int, bool], captured: set[int], max_gap: int,
         control: bool = False) -> tuple[list[Triple], list[int]]:
    """(judged triples, native frames skipped for lack of a guest frame on both sides)."""
    usable = {f: n for f, n in sides.items() if f in captured}
    guest = {f for f, n in usable.items() if not n}
    targets = sorted(guest if control else (f for f, n in usable.items() if n))
    triples, skipped = [], []
    for frame in targets:
        pool = guest - {frame}
        # A control frame's neighbours must be as far away as a native frame's would be:
        # with period 1 the adjacent frames are native, so this finds frame +-2.
        prev, after = neighbours(frame, pool, max_gap * (2 if control else 1))
        if prev is None or after is None:
            skipped.append(frame)
        else:
            triples.append(Triple(frame, prev, after))
    return triples, skipped


# ------------------------------------------------------------------ metrics

@dataclass
class Blocks:
    bw: int
    bh: int
    rgb: list[float]  # bw*bh*3 block means, row-major

    def luma_stddev(self) -> float:
        luma = [0.299 * self.rgb[i] + 0.587 * self.rgb[i + 1] + 0.114 * self.rgb[i + 2]
                for i in range(0, len(self.rgb), 3)]
        return statistics.pstdev(luma) if len(luma) > 1 else 0.0


def block_means(image, block: int) -> Blocks:
    """Mean RGB of every whole block x block square (a right/bottom remainder under one block is ignored)."""
    width = image.width
    bw, bh = max(1, width // block), max(1, image.height // block)
    bx_size, by_size = min(block, width), min(block, image.height)
    span = bx_size * 3
    rgb = image.rgb
    out: list[float] = []
    scale = 1.0 / (bx_size * by_size)
    for by in range(bh):
        acc = [0] * (bw * 3)
        for y in range(by * by_size, by * by_size + by_size):
            row = rgb[y * width * 3:(y + 1) * width * 3]
            for bx in range(bw):
                o = bx * span
                acc[bx * 3] += sum(row[o:o + span:3])
                acc[bx * 3 + 1] += sum(row[o + 1:o + span:3])
                acc[bx * 3 + 2] += sum(row[o + 2:o + span:3])
        out += [v * scale for v in acc]
    return Blocks(bw, bh, out)


def block_errors(a: Blocks, b: Blocks) -> list[float]:
    if (a.bw, a.bh) != (b.bw, b.bh):
        raise images.ImageError(f"size mismatch: {a.bw}x{a.bh} vs {b.bw}x{b.bh} blocks")
    pa, pb = a.rgb, b.rgb
    return [(abs(pa[i] - pb[i]) + abs(pa[i + 1] - pb[i + 1]) + abs(pa[i + 2] - pb[i + 2])) / 3.0
            for i in range(0, len(pa), 3)]


def tile_map(errors: list[float], bw: int, bh: int, tile: int) -> tuple[int, int, list[float]]:
    """Mean block error per tile of tile x tile blocks (edge tiles may be partial)."""
    tw, th = -(-bw // tile), -(-bh // tile)
    sums, counts = [0.0] * (tw * th), [0] * (tw * th)
    for by in range(bh):
        row = (by // tile) * tw
        for bx in range(bw):
            t = row + bx // tile
            sums[t] += errors[by * bw + bx]
            counts[t] += 1
    return tw, th, [s / c for s, c in zip(sums, counts)]


def dilate(values: list[float], tw: int, th: int, radius: int) -> list[float]:
    """Max over the (2*radius+1)^2 tile neighbourhood: motion noise next to a tile counts for it too."""
    if radius <= 0:
        return values
    rows = [max(values[y * tw + max(0, x - radius):y * tw + min(tw, x + radius + 1)])
            for y in range(th) for x in range(tw)]
    return [max(rows[yy * tw + x] for yy in range(max(0, y - radius), min(th, y + radius + 1)))
            for y in range(th) for x in range(tw)]


def largest_region(flags: list[bool], tw: int, th: int) -> int:
    """Size of the largest 4-connected group of flagged tiles."""
    seen = [False] * len(flags)
    best = 0
    for start, flagged in enumerate(flags):
        if not flagged or seen[start]:
            continue
        seen[start] = True
        stack, size = [start], 0
        while stack:
            t = stack.pop()
            size += 1
            x, y = t % tw, t // tw
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                n = ny * tw + nx
                if 0 <= nx < tw and 0 <= ny < th and flags[n] and not seen[n]:
                    seen[n] = True
                    stack.append(n)
        best = max(best, size)
    return best


@dataclass
class Settings:
    block: int = DEFAULTS["block"]
    tile: int = DEFAULTS["tile"]
    noise_ratio: float = DEFAULTS["noise_ratio"]
    floor: float = DEFAULTS["floor"]
    max_bad_tiles: float = DEFAULTS["max_bad_tiles"]
    max_region: float = DEFAULTS["max_region"]
    blank_stddev: float = DEFAULTS["blank_stddev"]
    dilate: int = DEFAULTS["dilate"]
    persist_fraction: float = DEFAULTS["persist_fraction"]
    persist_min_frames: int = DEFAULTS["persist_min_frames"]


@dataclass
class Judgement:
    report: dict
    score: float                    # how far past the limits (worst of the ratios)
    detail: dict                    # maps for the diff image


def judge(target: Blocks, prev: Blocks, after: Blocks, s: Settings) -> Judgement:
    e_prev, e_next, noise_b = block_errors(target, prev), block_errors(target, after), block_errors(prev, after)
    e_block = [min(p, n) for p, n in zip(e_prev, e_next)]
    tw, th, t_prev = tile_map(e_prev, target.bw, target.bh, s.tile)
    t_next = tile_map(e_next, target.bw, target.bh, s.tile)[2]
    t_noise = dilate(tile_map(noise_b, target.bw, target.bh, s.tile)[2], tw, th, s.dilate)
    t_err = [min(p, n) for p, n in zip(t_prev, t_next)]
    t_limit = [s.noise_ratio * n + s.floor for n in t_noise]
    bad = [e > limit for e, limit in zip(t_err, t_limit)]
    worst_tile = max(range(len(t_err)), key=lambda t: t_err[t] / t_limit[t])
    tiles = tw * th
    bad_fraction = sum(bad) / tiles
    region_fraction = largest_region(bad, tw, th) / tiles
    std_target = target.luma_stddev()
    std_guest = min(prev.luma_stddev(), after.luma_stddev())
    blank = std_target < s.blank_stddev <= std_guest
    mean_err, mean_noise = statistics.fmean(t_err), statistics.fmean(t_noise)
    failures = []
    if bad_fraction > s.max_bad_tiles:
        failures.append(f"bad_tiles {bad_fraction:.4f} > {s.max_bad_tiles}")
    if region_fraction > s.max_region:
        failures.append(f"largest_region {region_fraction:.4f} > {s.max_region}")
    if blank:
        failures.append(f"blank: luma stddev {std_target:.2f} < {s.blank_stddev} (guest {std_guest:.2f})")
    report = {
        "bad_tiles": round(bad_fraction, 4),
        "largest_region": round(region_fraction, 4),
        "mean_error": round(mean_err, 3),
        "mean_noise": round(mean_noise, 3),
        "error_over_noise": round(mean_err / mean_noise, 3) if mean_noise > 0 else None,
        "p95_tile_error": round(sorted(t_err)[int(0.95 * (tiles - 1))], 3),
        "worst_tile": {"x": worst_tile % tw, "y": worst_tile // tw, "error": round(t_err[worst_tile], 2),
                       "limit": round(t_limit[worst_tile], 2),
                       "ratio": round(t_err[worst_tile] / t_limit[worst_tile], 2)},
        "luma_stddev": round(std_target, 2),
        "guest_luma_stddev": round(std_guest, 2),
        "tiles": [tw, th],
        "passed": not failures,
        "failures": failures,
    }
    score = max(bad_fraction / s.max_bad_tiles if s.max_bad_tiles > 0 else float(bad_fraction > 0),
                region_fraction / s.max_region if s.max_region > 0 else float(region_fraction > 0),
                10.0 if blank else 0.0)
    limit_b = [s.noise_ratio * n + s.floor for n in noise_b]
    return Judgement(report, score, {"bad": bad, "tw": tw, "th": th, "e_block": e_block, "limit_block": limit_b})


# ------------------------------------------------------------------ diff image

def _half(image) -> tuple[int, int, bytearray]:
    """Nearest-neighbour half-size RGB."""
    w, h = max(1, image.width // 2), max(1, image.height // 2)
    out = bytearray(w * h * 3)
    stride = image.width * 3
    for y in range(h):
        row = image.rgb[2 * y * stride:(2 * y + 1) * stride]
        for c in range(3):
            out[y * w * 3 + c:(y + 1) * w * 3:3] = row[c::6][:w]
    return w, h, out


def write_rgb_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def write_diff_image(path: Path, guest, native, detail: dict, s: Settings, bw: int) -> None:
    w, h, g = _half(guest)
    _, _, n = _half(native)
    tinted = bytearray(n)
    heat = bytearray(w * h * 3)
    tile_px = s.block * s.tile / 2.0
    block_px = s.block / 2.0
    tw, bad = detail["tw"], detail["bad"]
    e_block, limit_block = detail["e_block"], detail["limit_block"]
    bh = len(e_block) // bw
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            tx, ty = int(x / tile_px), int(y / tile_px)
            if tx < tw and ty * tw + tx < len(bad) and bad[ty * tw + tx]:
                tinted[i] = min(255, (n[i] + 255) // 2)
                tinted[i + 1] = n[i + 1] // 2
                tinted[i + 2] = n[i + 2] // 2
            bx, by = int(x / block_px), int(y / block_px)
            if bx < bw and by < bh:
                ratio = e_block[by * bw + bx] / limit_block[by * bw + bx]
                v = min(255, int(255 * ratio / 2))  # 128 = at the limit, 255 = twice the limit
                heat[i:i + 3] = bytes((v, v if ratio <= 1 else 0, v if ratio <= 1 else 0))
    rows = []
    for left, right in ((g, n), (tinted, heat)):
        for y in range(h):
            rows.append(bytes(left[y * w * 3:(y + 1) * w * 3]) + bytes(right[y * w * 3:(y + 1) * w * 3]))
    write_rgb_png(path, 2 * w, 2 * h, b"".join(rows))


# ------------------------------------------------------------------ main

def _run(triples: list[Triple], load, s: Settings) -> list[tuple[Triple, Judgement]]:
    return [(t, judge(load(t.frame), load(t.prev), load(t.next), s)) for t in triples]


def persistent_tiles(results: list[tuple[Triple, Judgement]], fraction: float, min_frames: int) -> list[dict]:
    """Tiles bad in at least `fraction` of the judged frames (none below `min_frames` frames)."""
    if len(results) < min_frames:
        return []
    counts = [0] * len(results[0][1].detail["bad"])
    for _, j in results:
        for t, flagged in enumerate(j.detail["bad"]):
            counts[t] += flagged
    tw = results[0][1].detail["tw"]
    return [{"x": t % tw, "y": t // tw, "fraction": round(c / len(results), 3)}
            for t, c in enumerate(counts) if c and c >= fraction * len(results)]


def _summarize(results: list[tuple[Triple, Judgement]], label: str, s: Settings) -> dict:
    failing = [t.frame for t, j in results if not j.report["passed"]]
    worst = max(results, key=lambda r: r[1].score)
    col = lambda key: [j.report[key] for _, j in results]
    return {
        f"{label}_frames": len(results),
        "failing_frames": failing,
        "failing_fraction": round(len(failing) / len(results), 4),
        "worst": {"frame": worst[0].frame, "prev": worst[0].prev, "next": worst[0].next, **worst[1].report},
        "median": {k: round(statistics.median(col(k)), 4)
                   for k in ("bad_tiles", "largest_region", "mean_error", "mean_noise")},
        "max": {k: round(max(col(k)), 4) for k in ("bad_tiles", "largest_region", "mean_error")},
        "persistent_tiles": persistent_tiles(results, s.persist_fraction, s.persist_min_frames),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("captures", type=Path, nargs="?", help="directory holding <prefix>.output.<F>.bmp captures")
    parser.add_argument("log", type=Path, nargs="?", help="game log containing ab_alternate lines")
    parser.add_argument("--prefix", help="capture file-name prefix (base name of edf_native_scene_capture)")
    parser.add_argument("--period", type=int, help="edf_native_ab_alternate value, when there is no log")
    parser.add_argument("--tag", choices=TAGS, default="ab_alternate",
                        help="the log's side lines: ab_alternate (default) or reuse_alternate "
                             "(edf_native_reuse_off_alternate: reuse off is the reference side)")
    parser.add_argument("--start", type=int, help="capture start frame with --period (default: first capture)")
    parser.add_argument("--max-gap", type=int, default=DEFAULTS["max_gap"],
                        help="farthest guest neighbour, in frames (default %(default)s)")
    parser.add_argument("--block", type=int, default=DEFAULTS["block"], help="block size in pixels (default %(default)s)")
    parser.add_argument("--tile", type=int, default=DEFAULTS["tile"], help="tile size in blocks (default %(default)s)")
    parser.add_argument("--noise-ratio", type=float, default=DEFAULTS["noise_ratio"],
                        help="tile is bad above this multiple of the guest-vs-guest tile error (default %(default)s)")
    parser.add_argument("--floor", type=float, default=DEFAULTS["floor"],
                        help="plus this absolute tile error, 0..255 (default %(default)s)")
    parser.add_argument("--max-bad-tiles", type=float, default=DEFAULTS["max_bad_tiles"],
                        help="frame fails above this fraction of bad tiles (default %(default)s)")
    parser.add_argument("--max-region", type=float, default=DEFAULTS["max_region"],
                        help="frame fails when its largest connected bad region is above this fraction of all "
                             "tiles (default %(default)s)")
    parser.add_argument("--blank-stddev", type=float, default=DEFAULTS["blank_stddev"],
                        help="native frame is blank below this block luma stddev (default %(default)s)")
    parser.add_argument("--dilate", type=int, default=DEFAULTS["dilate"],
                        help="each tile's noise is the max over this many neighbouring tiles each way (default %(default)s)")
    parser.add_argument("--persist-fraction", type=float, default=DEFAULTS["persist_fraction"],
                        help="a tile bad in at least this fraction of the native frames is persistent "
                             "(default %(default)s)")
    parser.add_argument("--persist-min-frames", type=int, default=DEFAULTS["persist_min_frames"],
                        help="judge persistence only with at least this many native frames (default %(default)s)")
    parser.add_argument("--max-persistent-tiles", type=int, default=DEFAULTS["max_persistent_tiles"],
                        help="gate fails above this many persistent tiles (default %(default)s)")
    parser.add_argument("--max-failing-frames", type=float, default=DEFAULTS["max_failing_frames"],
                        help="gate fails above this fraction of failing native frames (default %(default)s)")
    parser.add_argument("--no-control", dest="control", action="store_false",
                        help="skip judging guest frames as if native (the false-positive check)")
    parser.add_argument("--diff-image", type=Path, help="write a PNG of the worst native frame here")
    parser.add_argument("--self-test", action="store_true", help="run the synthetic-image unit tests and exit")
    args = parser.parse_args(argv)
    if args.self_test:
        import unittest
        spec = importlib.util.spec_from_file_location(
            "test_compare_renderer_ab_captures", Path(__file__).with_name("test_compare_renderer_ab_captures.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(module))
        return 0 if result.wasSuccessful() else 1
    if args.captures is None:
        parser.error("captures directory is required")
    if args.log is None and args.period is None:
        parser.error("give a game log with ab_alternate lines, or --period")
    if args.period is not None and args.period <= 0:
        parser.error("--period must be positive")
    if args.block <= 0 or args.tile <= 0 or args.max_gap <= 0:
        parser.error("--block, --tile and --max-gap must be positive")
    s = Settings(args.block, args.tile, args.noise_ratio, args.floor, args.max_bad_tiles, args.max_region,
                 args.blank_stddev, args.dilate, args.persist_fraction, args.persist_min_frames)
    try:
        captures = find_captures(args.captures, args.prefix)
        if args.log is not None:
            sides = parse_log(args.log.read_text(encoding="utf-8", errors="replace"), args.tag)
            if not sides:
                raise AbError(f"log has no {args.tag} lines (edf_native_ab_alternate / edf_native_reuse_off_alternate off?)")
            source = "log"
        else:
            if not captures:
                raise AbError(f"no <prefix>.output.<F> captures in {args.captures}")
            start = args.start if args.start is not None else min(captures)
            sides = period_sides(captures, start, args.period)
            source = f"period={args.period} start={start}"
        unlogged = sorted(set(captures) - set(sides))
        triples, skipped = plan(sides, set(captures), args.max_gap)
        if not triples:
            raise AbError("no native frame with a captured guest frame on both sides")
        cache: dict[int, Blocks] = {}
        full: dict[int, object] = {}

        def load(frame: int) -> Blocks:
            if frame not in cache:
                full[frame] = images.read_image(captures[frame])
                cache[frame] = block_means(full[frame], s.block)
            return cache[frame]
        results = _run(triples, load, s)
        control = None
        if args.control:
            control_triples, _ = plan(sides, set(captures), args.max_gap, control=True)
            control = _run(control_triples, load, s) if control_triples else []
    except (OSError, AbError, images.ImageError, zlib.error) as error:
        print(json.dumps({"passed": False, "error": str(error)}, indent=2))
        print(f"error: {error}", file=sys.stderr)
        return 2

    summary = _summarize(results, "native", s)
    failures = []
    if summary["failing_fraction"] > args.max_failing_frames:
        failures.append(f"{len(summary['failing_frames'])}/{len(results)} native frames fail "
                        f"(allowed fraction {args.max_failing_frames}); worst frame {summary['worst']['frame']}: "
                        + "; ".join(summary["worst"]["failures"]))
    persistent = summary["persistent_tiles"]
    if len(persistent) > args.max_persistent_tiles:
        failures.append(f"{len(persistent)} tiles bad in at least {s.persist_fraction} of the native frames "
                        f"(allowed {args.max_persistent_tiles}), first at tile "
                        f"({persistent[0]['x']},{persistent[0]['y']})")
    warnings = []
    if unlogged:
        warnings.append(f"{len(unlogged)} captures have no side, ignored (first {unlogged[0]})")
    if skipped:
        warnings.append(f"native frames without a guest frame on both sides, not judged: {skipped}")
    report = {
        "captures": str(args.captures), "sides_from": source,
        "settings": {**vars(s), "max_failing_frames": args.max_failing_frames,
                     "max_persistent_tiles": args.max_persistent_tiles, "max_gap": args.max_gap},
        "summary": summary,
        "frames": [{"frame": t.frame, "prev": t.prev, "next": t.next, **j.report} for t, j in results],
    }
    if control is not None:
        if control:
            report["control"] = _summarize(control, "guest", s)
            if len(report["control"]["persistent_tiles"]) > args.max_persistent_tiles:
                warnings.append(f"control: {len(report['control']['persistent_tiles'])} persistent bad tiles in the "
                                "guest frames")
            if report["control"]["failing_frames"]:
                warnings.append(f"control: {len(report['control']['failing_frames'])}/{len(control)} guest frames "
                                "fail against their own neighbours; the thresholds are too tight for this "
                                "capture's motion")
        else:
            warnings.append("control: no guest frame with guest frames on both sides")
    if args.diff_image:
        worst_t, worst_j = max(results, key=lambda r: r[1].score)
        write_diff_image(args.diff_image, full[worst_t.prev], full[worst_t.frame], worst_j.detail, s,
                         cache[worst_t.frame].bw)
        report["diff_image"] = str(args.diff_image)
    report.update(passed=not failures, failures=failures, warnings=warnings)
    print(json.dumps(report, indent=2))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
