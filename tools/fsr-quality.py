#!/usr/bin/env python3
"""FSR image quality from output captures (edf_native_fsr, native_fsr.h).

Three measurements over the BMP captures the native renderer writes
(edf_native_scene_capture=<prefix>, <prefix>.output.<F>.bmp) or any
24/32-bit uncompressed BMP. Standard library only.

  reference  FSR native_aa against a supersampled reference: an FSR-off
             capture of the same frame at N times the resolution
             (edf_native_render_width/height), box-downsampled by N. Reports
             mean absolute error, PSNR and the error on edge pixels (where the
             reference has detail aliasing lives), for FSR and, with
             --baseline, for a plain FSR-off capture at the FSR resolution, so
             the two can be compared. Pairs files in order.
  stability  Temporal stability on still frames (camera and scene at rest):
             per-pixel luma standard deviation across the frames. Reports the
             mean, the 99th percentile and the fraction of flickering pixels
             (deviation over --flicker). --baseline gives the FSR-off set.
  ghosting   A camera pan captured with FSR on and, as a reference, the same
             frames with FSR off. Per frame, the pixels where FSR differs from
             the reference by more than --threshold; a differing pixel "lags"
             when FSR is closer to the reference's previous frame than to its
             current one (history the upscaler kept too long). Reports the
             differing fraction, the lag fraction of those, and the mean error.

Luma is Rec. 709 over the captured (display-encoded) bytes, in 0..1.
--region x,y,w,h limits every measurement to a rectangle (for example to
leave the HUD out; in the FSR-resolution image's pixels).

Exit status: 0, or 1 when a --max-* limit is exceeded, 2 on unreadable or
mismatched input. --json prints the numbers as one JSON object.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import re
import struct
import sys
from array import array
from dataclasses import dataclass
from pathlib import Path


class QualityError(Exception):
    pass


@dataclass
class Luma:
    width: int
    height: int
    values: array  # 'f', top-down, width*height

    def at(self, x: int, y: int) -> float:
        return self.values[y * self.width + x]


def read_bmp_luma(data: bytes) -> Luma:
    if len(data) < 54 or data[:2] != b"BM":
        raise QualityError("not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    if struct.unpack_from("<I", data, 14)[0] < 40:
        raise QualityError("unsupported BMP header")
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if bits not in (24, 32) or compression not in (0, 3) or planes != 1:
        raise QualityError(f"unsupported BMP format: {bits} bpp, compression {compression}")
    top_down = height < 0
    height = abs(height)
    if width <= 0 or height <= 0:
        raise QualityError("empty BMP")
    step = bits // 8
    pitch = (width * step + 3) & ~3
    if offset + pitch * height > len(data):
        raise QualityError("truncated BMP")
    values = array("f", bytes(4 * width * height))
    kr, kg, kb = 0.2126 / 255.0, 0.7152 / 255.0, 0.0722 / 255.0
    for y in range(height):
        src_y = y if top_down else height - 1 - y
        row = data[offset + src_y * pitch: offset + src_y * pitch + width * step]
        blue, green, red = row[0::step], row[1::step], row[2::step]
        base = y * width
        for x in range(width):
            values[base + x] = kr * red[x] + kg * green[x] + kb * blue[x]
    return Luma(width, height, values)


def load(path: str) -> Luma:
    try:
        return read_bmp_luma(Path(path).read_bytes())
    except OSError as error:
        raise QualityError(f"{path}: {error}") from error
    except QualityError as error:
        raise QualityError(f"{path}: {error}") from error


def downsample(image: Luma, factor: int) -> Luma:
    """Box filter by an integer factor (the supersampled reference)."""
    if factor < 1 or image.width % factor or image.height % factor:
        raise QualityError(f"{image.width}x{image.height} is not a multiple of {factor}")
    width, height = image.width // factor, image.height // factor
    out = array("f", bytes(4 * width * height))
    scale = 1.0 / (factor * factor)
    src = image.values
    for y in range(height):
        for x in range(width):
            total = 0.0
            for sy in range(factor):
                base = (y * factor + sy) * image.width + x * factor
                for sx in range(factor):
                    total += src[base + sx]
            out[y * width + x] = total * scale
    return Luma(width, height, out)


Region = tuple  # (x, y, w, h)


def parse_region(text: str | None, width: int, height: int) -> Region:
    if not text:
        return (0, 0, width, height)
    try:
        x, y, w, h = (int(part) for part in text.split(","))
    except ValueError as error:
        raise QualityError(f"--region must be x,y,w,h: {text}") from error
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > width or y + h > height:
        raise QualityError(f"--region {text} is outside {width}x{height}")
    return (x, y, w, h)


def region_indices(width: int, region: Region):
    x0, y0, w, h = region
    for y in range(y0, y0 + h):
        base = y * width
        for x in range(x0, x0 + w):
            yield base + x


def same_size(images, what: str):
    first = images[0]
    for image in images[1:]:
        if (image.width, image.height) != (first.width, first.height):
            raise QualityError(f"{what}: {image.width}x{image.height} differs from {first.width}x{first.height}")


def edge_mask(reference: Luma, threshold: float) -> array:
    """Pixels whose reference luma differs from a 4-neighbour by more than threshold."""
    w, h, v = reference.width, reference.height, reference.values
    mask = array("b", bytes(w * h))
    for y in range(h):
        for x in range(w):
            c = v[y * w + x]
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < w and 0 <= ny < h and abs(v[ny * w + nx] - c) > threshold:
                    mask[y * w + x] = 1
                    break
    return mask


def error_stats(image: Luma, reference: Luma, indices, edges: array) -> dict:
    total = squared = edge_total = 0.0
    count = edge_count = 0
    a, b = image.values, reference.values
    for i in indices:
        d = abs(a[i] - b[i])
        total += d
        squared += d * d
        count += 1
        if edges[i]:
            edge_total += d
            edge_count += 1
    mse = squared / count if count else 0.0
    return {
        "mean_abs": total / count if count else 0.0,
        "psnr": float("inf") if mse == 0 else 10 * math.log10(1.0 / mse),
        "edge_mean_abs": edge_total / edge_count if edge_count else 0.0,
        "edge_pixels": edge_count,
        "pixels": count,
    }


def measure_reference(fsr, reference, baseline, scale: int, region_text, edge_threshold: float) -> dict:
    if len(fsr) != len(reference) or (baseline and len(baseline) != len(fsr)):
        raise QualityError("reference: the FSR, reference and baseline lists must pair up")
    frames = []
    for index, (fsr_path, reference_path) in enumerate(zip(fsr, reference)):
        image = load(fsr_path)
        truth = downsample(load(reference_path), scale)
        if (truth.width, truth.height) != (image.width, image.height):
            raise QualityError(f"reference: {reference_path} downsampled by {scale} is {truth.width}x{truth.height}, "
                               f"{fsr_path} is {image.width}x{image.height}")
        region = parse_region(region_text, image.width, image.height)
        edges = edge_mask(truth, edge_threshold)
        entry = {"fsr": fsr_path, "reference": reference_path,
                 "fsr_error": error_stats(image, truth, region_indices(image.width, region), edges)}
        if baseline:
            plain = load(baseline[index])
            same_size([image, plain], "baseline")
            entry["baseline"] = baseline[index]
            entry["baseline_error"] = error_stats(plain, truth, region_indices(image.width, region), edges)
        frames.append(entry)
    summary = {"frames": len(frames),
               "fsr_mean_abs": sum(f["fsr_error"]["mean_abs"] for f in frames) / len(frames),
               "fsr_edge_mean_abs": sum(f["fsr_error"]["edge_mean_abs"] for f in frames) / len(frames)}
    if baseline:
        summary["baseline_mean_abs"] = sum(f["baseline_error"]["mean_abs"] for f in frames) / len(frames)
        summary["baseline_edge_mean_abs"] = sum(f["baseline_error"]["edge_mean_abs"] for f in frames) / len(frames)
    return {"measure": "reference", "scale": scale, "summary": summary, "frames": frames}


def temporal_deviation(paths, region_text) -> dict:
    if len(paths) < 2:
        raise QualityError("stability needs at least two frames")
    images = [load(path) for path in paths]
    same_size(images, "stability")
    width, height = images[0].width, images[0].height
    region = parse_region(region_text, width, height)
    count = len(images)
    deviations = []
    for i in region_indices(width, region):
        mean = sum(image.values[i] for image in images) / count
        variance = sum((image.values[i] - mean) ** 2 for image in images) / count
        deviations.append(math.sqrt(variance))
    deviations.sort()
    return {"frames": count, "pixels": len(deviations), "deviations": deviations}


def summarize_deviation(result: dict, flicker: float) -> dict:
    deviations = result.pop("deviations")
    n = len(deviations)
    return {**result,
            "mean_std": sum(deviations) / n,
            "p99_std": deviations[min(n - 1, int(0.99 * n))],
            "max_std": deviations[-1],
            "flicker_fraction": sum(1 for d in deviations if d > flicker) / n}


def measure_stability(fsr, baseline, region_text, flicker: float) -> dict:
    out = {"measure": "stability", "flicker": flicker,
           "fsr": summarize_deviation(temporal_deviation(fsr, region_text), flicker)}
    if baseline:
        out["baseline"] = summarize_deviation(temporal_deviation(baseline, region_text), flicker)
    return out


def measure_ghosting(fsr, reference, region_text, threshold: float) -> dict:
    if len(fsr) != len(reference) or len(fsr) < 2:
        raise QualityError("ghosting needs the same number (at least two) of FSR and reference frames")
    fsr_images = [load(path) for path in fsr]
    reference_images = [load(path) for path in reference]
    same_size(fsr_images + reference_images, "ghosting")
    width, height = fsr_images[0].width, fsr_images[0].height
    region = parse_region(region_text, width, height)
    frames = []
    for t in range(1, len(fsr_images)):
        a, now, before = fsr_images[t].values, reference_images[t].values, reference_images[t - 1].values
        differing = lagging = count = 0
        total = 0.0
        for i in region_indices(width, region):
            d = abs(a[i] - now[i])
            total += d
            count += 1
            if d > threshold:
                differing += 1
                if abs(a[i] - before[i]) < d:
                    lagging += 1
        frames.append({"frame": fsr[t], "mean_abs": total / count, "differing_fraction": differing / count,
                       "lag_fraction": lagging / differing if differing else 0.0})
    n = len(frames)
    return {"measure": "ghosting", "threshold": threshold, "frames": frames,
            "summary": {"pairs": n,
                        "mean_abs": sum(f["mean_abs"] for f in frames) / n,
                        "differing_fraction": sum(f["differing_fraction"] for f in frames) / n,
                        "lag_fraction": sum(f["lag_fraction"] for f in frames) / n}}


def expand(patterns):
    """Files in frame order: globs expanded, sorted by the last number in the name."""
    def key(path: str):
        numbers = re.findall(r"\d+", Path(path).name)
        return (int(numbers[-1]) if numbers else -1, path)
    out = []
    for pattern in patterns or []:
        matches = glob.glob(pattern)
        if not matches and not any(ch in pattern for ch in "*?["):
            matches = [pattern]
        out.extend(sorted(matches, key=key))
    return out


def report(result: dict) -> str:
    lines = []
    measure = result["measure"]
    if measure == "reference":
        s = result["summary"]
        lines.append(f"reference (supersampled x{result['scale']}): {s['frames']} frame(s)")
        lines.append(f"  FSR       mean |error| {s['fsr_mean_abs']:.5f}  edges {s['fsr_edge_mean_abs']:.5f}")
        if "baseline_mean_abs" in s:
            lines.append(f"  FSR off   mean |error| {s['baseline_mean_abs']:.5f}  edges {s['baseline_edge_mean_abs']:.5f}")
        for f in result["frames"]:
            e = f["fsr_error"]
            lines.append(f"  {f['fsr']}: mean {e['mean_abs']:.5f} psnr {e['psnr']:.2f} dB edges {e['edge_mean_abs']:.5f} "
                         f"({e['edge_pixels']} px)")
    elif measure == "stability":
        for name in ("fsr", "baseline"):
            if name in result:
                s = result[name]
                lines.append(f"stability {name}: {s['frames']} frames, mean std {s['mean_std']:.5f} p99 {s['p99_std']:.5f} "
                             f"max {s['max_std']:.5f} flicker {100 * s['flicker_fraction']:.3f}% (> {result['flicker']})")
    else:
        s = result["summary"]
        lines.append(f"ghosting: {s['pairs']} pair(s), mean |error| {s['mean_abs']:.5f}, differing "
                     f"{100 * s['differing_fraction']:.3f}%, lagging {100 * s['lag_fraction']:.2f}% of those")
        for f in result["frames"]:
            lines.append(f"  {f['frame']}: mean {f['mean_abs']:.5f} differing {100 * f['differing_fraction']:.3f}% "
                         f"lagging {100 * f['lag_fraction']:.2f}%")
    return "\n".join(lines)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="measure", required=True)
    ref = sub.add_parser("reference", help="FSR against a supersampled FSR-off reference")
    ref.add_argument("--fsr", nargs="+", required=True, help="FSR native_aa captures (globs allowed)")
    ref.add_argument("--reference", nargs="+", required=True, help="FSR-off captures at --scale times the resolution")
    ref.add_argument("--baseline", nargs="*", help="FSR-off captures at the FSR resolution")
    ref.add_argument("--scale", type=int, default=2)
    ref.add_argument("--edge-threshold", type=float, default=0.08)
    ref.add_argument("--max-error", type=float, help="fail when FSR's mean |error| exceeds this")
    stab = sub.add_parser("stability", help="luma deviation across still frames")
    stab.add_argument("--fsr", nargs="+", required=True)
    stab.add_argument("--baseline", nargs="*")
    stab.add_argument("--flicker", type=float, default=0.02)
    stab.add_argument("--max-flicker", type=float, help="fail when FSR's flicker fraction exceeds this")
    ghost = sub.add_parser("ghosting", help="a pan with FSR against the same pan without")
    ghost.add_argument("--fsr", nargs="+", required=True)
    ghost.add_argument("--reference", nargs="+", required=True)
    ghost.add_argument("--threshold", type=float, default=0.06)
    ghost.add_argument("--max-lag", type=float, help="fail when the lag fraction exceeds this")
    for p in (ref, stab, ghost):
        p.add_argument("--region", help="x,y,w,h in FSR-resolution pixels")
        p.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.measure == "reference":
            result = measure_reference(expand(args.fsr), expand(args.reference), expand(args.baseline), args.scale,
                                       args.region, args.edge_threshold)
            failed = args.max_error is not None and result["summary"]["fsr_mean_abs"] > args.max_error
        elif args.measure == "stability":
            result = measure_stability(expand(args.fsr), expand(args.baseline), args.region, args.flicker)
            failed = args.max_flicker is not None and result["fsr"]["flicker_fraction"] > args.max_flicker
        else:
            result = measure_ghosting(expand(args.fsr), expand(args.reference), args.region, args.threshold)
            failed = args.max_lag is not None and result["summary"]["lag_fraction"] > args.max_lag
    except QualityError as error:
        print(f"fsr-quality: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=1, default=float) if args.json else report(result))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
