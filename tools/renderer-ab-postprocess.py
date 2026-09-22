#!/usr/bin/env python3
"""Image post-processing for tools/run-renderer-ab.ps1 (one baseline/candidate pair).

  pair   Compare the output captures of a baseline and a candidate run on
         matching indexed-output frame numbers (<prefix>.output.<F>.bmp),
         classify every capture as loading or gameplay, write thumbnails and
         print one JSON report. With --ab-alternate it also runs
         tools/compare-renderer-ab-captures.py on the candidate run.
  sheet  Build <dir>/contact-sheet.html from every <dir>/pair-*/postprocess.json.

Frame numbers count indexed (3D) output frames, not wall-clock time, so the
same F in two runs is usually the same point of the mission. It is not
guaranteed: a run that stalls in a menu or a loading screen can still drift.
The loading classification exists to make exactly that visible.

Loading-screen detection is a HEURISTIC, not a scene query. A capture is
classified "loading" when either
  - at least --dark-fraction (default 0.85) of sampled pixels have luma below
    --dark-luma (default 24), i.e. the frame is mostly black with a little
    text or a spinner, or
  - the luma standard deviation is below --flat-stddev (default 6), i.e. the
    frame is a nearly uniform fill (fade, clear colour, splash).
A dark night mission or a fade-out in gameplay can be misclassified; treat a
"loading" flag as a reason to look at the contact sheet, not as a verdict.

Uses Pillow for thumbnail resampling when it is installed; otherwise only the
standard library (BMP reading and PNG writing as in compare-renderer-images.py).
The image verdicts are reported, never enforced: the exit code is 0 whenever a
report was written (missing or unreadable captures are listed in it).
"""
from __future__ import annotations

import argparse
import html
import importlib.util
import json
import math
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def _load(name: str, file: str):
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, TOOLS / file)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


images = _load("compare_renderer_images", "compare-renderer-images.py")

try:  # optional: better thumbnails only; statistics never depend on it
    from PIL import Image as _PilImage
except ImportError:  # pragma: no cover - depends on the machine
    _PilImage = None

CAPTURE_NAME = re.compile(r"\.output\.(\d+)\.(?:bmp|png)$", re.IGNORECASE)
AB_PAIR = re.compile(r"guest=(\d+) native=(\d+) differing=([\d.]+)% max_diff=(\d+) psnr_db=(\S+) "
                     r"limit=([\d.]+)% result=(PASS|FAIL)")
AB_BASELINE = re.compile(r"baseline guest/guest step=(\d+) pairs=(\d+) median=([\d.]+)% max=([\d.]+)%")
AB_SUMMARY = re.compile(r"summary pairs=(\d+) failed=(\d+) .*result=(PASS|FAIL)")

DEFAULTS = {"dark_luma": 24, "dark_fraction": 0.85, "flat_stddev": 6.0, "sample_step": 4}


# ---------------------------------------------------------------- captures

def find_captures(directory: Path, prefix: str) -> dict[int, Path]:
    """Frame -> path for <prefix>.output.<F>.bmp|png in directory (missing dir = none)."""
    found: dict[int, Path] = {}
    if not directory.is_dir():
        return found
    for path in sorted(directory.iterdir()):
        match = CAPTURE_NAME.search(path.name)
        if match and path.name.startswith(prefix + ".output."):
            found.setdefault(int(match.group(1)), path)
    return found


# ---------------------------------------------------------- classification

def luma_stats(image, sample_step: int = 4, dark_luma: int = 24) -> dict:
    """Mean/stddev of Rec.601 luma and the dark-pixel fraction over a sample grid."""
    step = max(1, sample_step)
    rgb, width = image.rgb, image.width
    count = dark = 0
    total = total_sq = 0.0
    for y in range(0, image.height, step):
        row = y * width * 3
        for x in range(0, width, step):
            i = row + x * 3
            luma = (299 * rgb[i] + 587 * rgb[i + 1] + 114 * rgb[i + 2]) / 1000.0
            count += 1
            total += luma
            total_sq += luma * luma
            dark += luma < dark_luma
    mean = total / count
    variance = max(0.0, total_sq / count - mean * mean)
    return {"mean_luma": round(mean, 2), "stddev_luma": round(math.sqrt(variance), 2),
            "dark_fraction": round(dark / count, 4), "samples": count}


def classify(image, dark_luma: int = DEFAULTS["dark_luma"], dark_fraction: float = DEFAULTS["dark_fraction"],
             flat_stddev: float = DEFAULTS["flat_stddev"], sample_step: int = DEFAULTS["sample_step"]) -> dict:
    """Heuristic loading-screen classification (see module docstring)."""
    stats = luma_stats(image, sample_step, dark_luma)
    reasons = []
    if stats["dark_fraction"] >= dark_fraction:
        reasons.append(f"dark_fraction {stats['dark_fraction']:.3f} >= {dark_fraction}")
    if stats["stddev_luma"] < flat_stddev:
        reasons.append(f"stddev_luma {stats['stddev_luma']:.2f} < {flat_stddev}")
    return {**stats, "classification": "loading" if reasons else "gameplay", "reasons": reasons}


# ------------------------------------------------------------- thumbnails

def write_rgb_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def thumb_size(width: int, height: int, max_width: int) -> tuple[int, int]:
    if width <= max_width:
        return width, height
    return max_width, max(1, round(height * max_width / width))


def downsample_rgb(image, out_w: int, out_h: int) -> bytes:
    """Nearest-neighbour RGB downsample (stdlib path)."""
    src = image.rgb
    out = bytearray()
    for y in range(out_h):
        row = (y * image.height // out_h) * image.width * 3
        for x in range(out_w):
            i = row + (x * image.width // out_w) * 3
            out += src[i:i + 3]
    return bytes(out)


def downsample_mask(mask: bytes, width: int, height: int, out_w: int, out_h: int) -> bytes:
    """Max-pool a grey mask so a single differing pixel stays visible."""
    out = bytearray(out_w * out_h)
    for y in range(out_h):
        y0, y1 = y * height // out_h, max(y * height // out_h + 1, (y + 1) * height // out_h)
        rows = [mask[r * width:(r + 1) * width] for r in range(y0, y1)]
        for x in range(out_w):
            x0, x1 = x * width // out_w, max(x * width // out_w + 1, (x + 1) * width // out_w)
            out[y * out_w + x] = max(max(row[x0:x1]) for row in rows)
    return bytes(out)


def write_thumbnail(image, path: Path, max_width: int, use_pillow: bool = True) -> None:
    out_w, out_h = thumb_size(image.width, image.height, max_width)
    if use_pillow and _PilImage is not None:
        pil = _PilImage.frombytes("RGB", (image.width, image.height), image.rgb)
        pil.resize((out_w, out_h), _PilImage.LANCZOS).save(path, "PNG")
        return
    write_rgb_png(path, out_w, out_h, downsample_rgb(image, out_w, out_h))


def write_mask_thumbnail(comparison, path: Path, max_width: int) -> None:
    out_w, out_h = thumb_size(comparison.width, comparison.height, max_width)
    small = downsample_mask(comparison.mask, comparison.width, comparison.height, out_w, out_h)
    images.write_grey_png(path, out_w, out_h, small)


# ------------------------------------------------------------ A/B alternate

def parse_ab_output(stdout: str) -> dict:
    pairs = [{"guest": int(m[1]), "native": int(m[2]), "differing_percent": float(m[3]), "max_diff": int(m[4]),
              "psnr_db": m[5], "limit_percent": float(m[6]), "passed": m[7] == "PASS"}
             for m in AB_PAIR.finditer(stdout)]
    result: dict = {"pairs": pairs}
    if (m := AB_BASELINE.search(stdout)):
        result["baseline"] = {"step": int(m[1]), "pairs": int(m[2]), "median_percent": float(m[3]),
                              "max_percent": float(m[4])}
    if (m := AB_SUMMARY.search(stdout)):
        result["summary"] = {"pairs": int(m[1]), "failed": int(m[2]), "passed": m[3] == "PASS"}
    return result


def run_ab_alternate(captures_dir: Path, log: Path, prefix: str, extra: list[str]) -> dict:
    argv = [sys.executable, str(TOOLS / "compare-renderer-ab-captures.py"), str(captures_dir), str(log),
            "--prefix", prefix, *extra]
    proc = subprocess.run(argv, capture_output=True, text=True)
    report = parse_ab_output(proc.stdout)
    report.update({"exit_code": proc.returncode, "passed": proc.returncode == 0,
                   "stdout": proc.stdout, "stderr": proc.stderr.strip() or None})
    return report


def ab_sides(log: Path | None) -> dict[int, bool]:
    if not log or not log.is_file():
        return {}
    ab = _load("compare_renderer_ab_captures", "compare-renderer-ab-captures.py")
    try:
        return ab.parse_log(log.read_text(encoding="utf-8", errors="replace"))
    except ab.AbError:
        return {}


# ------------------------------------------------------------------ pairs

def _psnr(value: float):
    return "inf" if math.isinf(value) else round(value, 2)


def process_pair(baseline_dir: Path, candidate_dir: Path, out_dir: Path, prefix: str = "cap",
                 threshold: int = 2, limit: float = 0.5, thumb_width: int = 320, use_pillow: bool = True,
                 candidate_log: Path | None = None, ab_alternate: bool = False, ab_args: list[str] | None = None,
                 classify_args: dict | None = None, label: str = "") -> dict:
    classify_args = classify_args or {}
    out_dir.mkdir(parents=True, exist_ok=True)
    thumbs = out_dir / "thumbs"
    thumbs.mkdir(exist_ok=True)
    base = find_captures(baseline_dir, prefix)
    cand = find_captures(candidate_dir, prefix)
    sides = ab_sides(candidate_log) if ab_alternate else {}
    frames = []
    errors = []

    def load(role: str, frame: int, path: Path):
        try:
            image = images.read_image(path)
        except (OSError, images.ImageError, zlib.error) as error:
            errors.append(f"{role} frame {frame}: {error}")
            return None, None
        info = {"path": str(path), "width": image.width, "height": image.height, **classify(image, **classify_args)}
        thumb = thumbs / f"{role}.{frame}.png"
        write_thumbnail(image, thumb, thumb_width, use_pillow)
        info["thumbnail"] = thumb.relative_to(out_dir).as_posix()
        return image, info

    for frame in sorted(set(base) | set(cand)):
        entry: dict = {"frame": frame}
        a = b = None
        for role, found in (("baseline", base), ("candidate", cand)):
            if frame in found:
                img, info = load(role, frame, found[frame])
                if info:
                    entry[role] = info
                else:
                    entry[role + "_error"] = True
                if role == "baseline":
                    a = img
                else:
                    b = img
        if frame in cand:
            if frame in sides:
                entry["candidate_side"] = "native" if sides[frame] else "guest"
        if a is not None and b is not None:
            try:
                c = images.compare(a, b, threshold)
                mask = thumbs / f"diff.{frame}.png"
                write_mask_thumbnail(c, mask, thumb_width)
                entry["compare"] = {"differing_percent": round(c.percent, 4), "max_diff": c.max_diff,
                                    "psnr_db": _psnr(c.psnr), "threshold": threshold, "limit_percent": limit,
                                    "passed": c.percent <= limit, "mask_thumbnail": mask.relative_to(out_dir).as_posix()}
            except images.ImageError as error:
                entry["compare"] = {"error": str(error), "passed": False}
            loading = (entry["baseline"]["classification"], entry["candidate"]["classification"])
            entry["loading_mismatch"] = loading[0] != loading[1]
        frames.append(entry)

    matched = [f for f in frames if "compare" in f]
    compared = [f["compare"] for f in matched if "error" not in f["compare"]]

    def loading_frames(role: str) -> list[int]:
        return [f["frame"] for f in frames if role in f and f[role]["classification"] == "loading"]

    report = {
        "label": label, "prefix": prefix, "baseline_dir": str(baseline_dir), "candidate_dir": str(candidate_dir),
        "heuristic": "loading = dark_fraction >= {dark_fraction} (luma < {dark_luma}) or stddev_luma < {flat_stddev}"
                     .format(**{**DEFAULTS, **classify_args}),
        "baseline_frames": sorted(base), "candidate_frames": sorted(cand),
        "baseline_only": sorted(set(base) - set(cand)), "candidate_only": sorted(set(cand) - set(base)),
        "matched": len(matched),
        "compare_failed": [f["frame"] for f in matched if not f["compare"]["passed"]],
        "max_differing_percent": max((c["differing_percent"] for c in compared), default=None),
        "baseline_loading": loading_frames("baseline"), "candidate_loading": loading_frames("candidate"),
        "loading_mismatch": [f["frame"] for f in matched if f["loading_mismatch"]],
        "frames": frames, "errors": errors,
    }
    if ab_alternate:
        if candidate_log and candidate_log.is_file() and cand:
            report["ab_alternate"] = run_ab_alternate(candidate_dir, candidate_log, prefix, ab_args or [])
        else:
            report["ab_alternate"] = {"passed": False, "exit_code": None,
                                      "stderr": "no candidate captures or log for the A/B alternate check"}
    (out_dir / "postprocess.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


# ------------------------------------------------------------------ sheet

STYLE = """
:root{color-scheme:light dark;--bg:#fafafa;--fg:#1d1d1f;--mute:#666;--card:#fff;--line:#ddd;--bad:#b3261e;--warn:#9a6700;--ok:#1a7f37}
@media (prefers-color-scheme:dark){:root{--bg:#161618;--fg:#e8e8ea;--mute:#9a9aa0;--card:#1f1f22;--line:#333;--bad:#ff8a80;--warn:#e3b341;--ok:#56d364}}
body{background:var(--bg);color:var(--fg);font:14px/1.4 system-ui,sans-serif;margin:16px}
h1{font-size:20px}h2{font-size:16px;margin-top:28px}.mute{color:var(--mute)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:flex-start;background:var(--card);border:1px solid var(--line);
border-radius:6px;padding:8px;margin:8px 0}
figure{margin:0;max-width:100%}figure img{display:block;max-width:100%;height:auto;image-rendering:auto}
figcaption{font-size:12px;color:var(--mute)}.meta{min-width:180px;font-size:13px}
.loading{color:var(--warn);font-weight:600}.bad{color:var(--bad);font-weight:600}.ok{color:var(--ok)}
"""


def _figure(pair_dir: str, src: str | None, caption: str) -> str:
    if not src:
        return f'<figure><figcaption>{html.escape(caption)}</figcaption><p class="mute">no capture</p></figure>'
    return (f'<figure><img loading="lazy" src="{html.escape(pair_dir + "/" + src)}" alt="{html.escape(caption)}">'
            f"<figcaption>{html.escape(caption)}</figcaption></figure>")


def _side_caption(role: str, info: dict | None, side: str | None = None) -> str:
    if not info:
        return role
    text = f"{role}{' (' + side + ')' if side else ''}: {info['classification']} " \
           f"mean {info['mean_luma']} sd {info['stddev_luma']} dark {info['dark_fraction']}"
    return text


def render_sheet(directory: Path) -> Path:
    reports = []
    for path in sorted(directory.glob("pair-*/postprocess.json"),
                       key=lambda p: int(re.sub(r"\D", "", p.parent.name) or 0)):
        reports.append((path.parent.name, json.loads(path.read_text(encoding="utf-8"))))
    parts = [f"<!doctype html><html lang=en><head><meta charset=utf-8>"
             f"<meta name=viewport content='width=device-width,initial-scale=1'>"
             f"<title>Renderer A/B captures</title><style>{STYLE}</style></head><body>",
             f"<h1>Renderer A/B captures</h1><p class=mute>{html.escape(str(directory))}. Frames are indexed "
             f"(3D) output frames. Loading classification is a heuristic: "
             f"{html.escape(reports[0][1]['heuristic'] if reports else '')}.</p>"]
    if not reports:
        parts.append("<p>No pair captures were found.</p>")
    for name, report in reports:
        mismatch = report.get("loading_mismatch", [])
        parts.append(f"<h2>{html.escape(name)} {html.escape(report.get('label') or '')}</h2><p>"
                     f"matched {report['matched']} frames; max differing "
                     f"{report['max_differing_percent'] if report['max_differing_percent'] is not None else 'n/a'}%; "
                     f"loading baseline {report['baseline_loading'] or 'none'}, "
                     f"candidate {report['candidate_loading'] or 'none'}"
                     + (f"; <span class=bad>loading/gameplay mismatch at {mismatch}</span>" if mismatch else "")
                     + (f"; baseline only {report['baseline_only']}" if report['baseline_only'] else "")
                     + (f"; candidate only {report['candidate_only']}" if report['candidate_only'] else "")
                     + "</p>")
        if (ab := report.get("ab_alternate")) is not None:
            summary = ab.get("summary") or {}
            verdict = "PASS" if ab.get("passed") else "FAIL"
            parts.append(f"<p>A/B alternate (candidate): <span class={'ok' if ab.get('passed') else 'bad'}>"
                         f"{verdict}</span> pairs {summary.get('pairs', 'n/a')}, failed "
                         f"{summary.get('failed', 'n/a')}{' - ' + html.escape(ab['stderr']) if ab.get('stderr') else ''}</p>")
        for frame in report["frames"]:
            base, cand, comp = frame.get("baseline"), frame.get("candidate"), frame.get("compare")
            meta = [f"<b>frame {frame['frame']}</b>"]
            if comp and "error" not in comp:
                cls = "ok" if comp["passed"] else "bad"
                meta.append(f"<span class={cls}>{comp['differing_percent']}% differing</span>")
                meta.append(f"max diff {comp['max_diff']}, PSNR {comp['psnr_db']} dB")
            elif comp:
                meta.append(f"<span class=bad>{html.escape(comp['error'])}</span>")
            for role, info in (("baseline", base), ("candidate", cand)):
                if info and info["classification"] == "loading":
                    meta.append(f"<span class=loading>{role} looks like loading</span>")
            if frame.get("loading_mismatch"):
                meta.append("<span class=bad>loading vs gameplay: comparison not meaningful</span>")
            parts.append("<div class=row><div class=meta>" + "<br>".join(meta) + "</div>"
                         + _figure(name, base and base["thumbnail"], _side_caption("baseline", base))
                         + _figure(name, cand and cand["thumbnail"],
                                   _side_caption("candidate", cand, frame.get("candidate_side")))
                         + (_figure(name, comp.get("mask_thumbnail"), "diff mask (white = over threshold)")
                            if comp and comp.get("mask_thumbnail") else "")
                         + "</div>")
    parts.append("</body></html>")
    out = directory / "contact-sheet.html"
    out.write_text("\n".join(parts), encoding="utf-8")
    return out


# ------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    pair = sub.add_parser("pair", help="compare one baseline/candidate pair of capture directories")
    pair.add_argument("--baseline-dir", type=Path, required=True)
    pair.add_argument("--candidate-dir", type=Path, required=True)
    pair.add_argument("--out", type=Path, required=True, help="pair output directory (thumbs, postprocess.json)")
    pair.add_argument("--prefix", default="cap", help="capture file-name prefix (default cap)")
    pair.add_argument("--label", default="")
    pair.add_argument("--threshold", type=int, default=2)
    pair.add_argument("--limit", type=float, default=0.5)
    pair.add_argument("--thumb-width", type=int, default=320)
    pair.add_argument("--no-pillow", action="store_true", help="use the stdlib thumbnail path even with Pillow")
    pair.add_argument("--candidate-log", type=Path)
    pair.add_argument("--ab-alternate", action="store_true", help="also run compare-renderer-ab-captures.py")
    pair.add_argument("--ab-arg", action="append", default=[], help="extra argument for the A/B check")
    pair.add_argument("--dark-luma", type=int, default=DEFAULTS["dark_luma"])
    pair.add_argument("--dark-fraction", type=float, default=DEFAULTS["dark_fraction"])
    pair.add_argument("--flat-stddev", type=float, default=DEFAULTS["flat_stddev"])
    sheet = sub.add_parser("sheet", help="write <dir>/contact-sheet.html from pair-*/postprocess.json")
    sheet.add_argument("directory", type=Path)
    args = parser.parse_args(argv)
    if args.command == "sheet":
        print(render_sheet(args.directory))
        return 0
    if not 0 <= args.threshold <= 255:
        parser.error("--threshold must be within 0..255")
    report = process_pair(args.baseline_dir, args.candidate_dir, args.out, args.prefix, args.threshold, args.limit,
                          args.thumb_width, not args.no_pillow, args.candidate_log, args.ab_alternate, args.ab_arg,
                          {"dark_luma": args.dark_luma, "dark_fraction": args.dark_fraction,
                           "flat_stddev": args.flat_stddev}, args.label)
    print(json.dumps(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
