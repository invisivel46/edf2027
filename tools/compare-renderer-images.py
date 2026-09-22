#!/usr/bin/env python3
"""Compare two renderer images (guest-drawn vs native-drawn) for acceptance.

Reads 24/32-bit uncompressed BMP (top-down or bottom-up, as written by the
native capture cvars) and 8-bit non-interlaced PNG (grey, RGB, RGBA, grey+alpha,
as written by tools/capture-game-window.ps1). Standard library only.

Reports the percentage of pixels whose largest per-channel difference exceeds
--threshold, the maximum channel difference, PSNR over RGB, and optionally
writes a diff-mask PNG (white = over threshold). Exits 1 when the differing
percentage exceeds --limit, 2 on unreadable or mismatched input.
Alpha is ignored: captures are opaque presentation images.
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


class ImageError(Exception):
    pass


@dataclass
class Image:
    width: int
    height: int
    rgb: bytes  # tightly packed, top-down, 3 bytes per pixel


def read_bmp(data: bytes) -> Image:
    if len(data) < 54 or data[:2] != b"BM":
        raise ImageError("not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    if header < 40:
        raise ImageError("unsupported BMP header")
    width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if bits not in (24, 32) or compression not in (0, 3) or planes != 1:
        raise ImageError(f"unsupported BMP format: {bits} bpp, compression {compression}")
    if compression == 3 and bits != 32:
        raise ImageError("bitfield BMP must be 32 bpp")
    top_down = height < 0
    height = abs(height)
    if width <= 0 or height <= 0:
        raise ImageError("empty BMP")
    step = bits // 8
    pitch = (width * step + 3) & ~3
    if offset + pitch * height > len(data):
        raise ImageError("truncated BMP")
    out = bytearray(width * height * 3)
    for y in range(height):
        src_y = y if top_down else height - 1 - y
        row = data[offset + src_y * pitch: offset + src_y * pitch + width * step]
        dst = y * width * 3
        # BGR(A) -> RGB
        out[dst + 0: dst + width * 3: 3] = row[2::step]
        out[dst + 1: dst + width * 3: 3] = row[1::step]
        out[dst + 2: dst + width * 3: 3] = row[0::step]
    return Image(width, height, bytes(out))


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(data: bytes) -> Image:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ImageError("not a PNG")
    pos = 8
    idat = bytearray()
    width = height = depth = color = interlace = None
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8: pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if width is None:
        raise ImageError("PNG has no IHDR")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
    if depth != 8 or channels is None or interlace != 0:
        raise ImageError(f"unsupported PNG: depth {depth}, color type {color}, interlace {interlace}")
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    if len(raw) < (stride + 1) * height:
        raise ImageError("truncated PNG")
    prev = bytearray(stride)
    out = bytearray(width * height * 3)
    for y in range(height):
        base = y * (stride + 1)
        ftype = raw[base]
        line = bytearray(raw[base + 1: base + 1 + stride])
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                upleft = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 255
        elif ftype != 0:
            raise ImageError(f"bad PNG filter {ftype}")
        dst = y * width * 3
        if channels >= 3:
            for c in range(3):
                out[dst + c: dst + width * 3: 3] = line[c::channels]
        else:
            grey = line[0::channels]
            for c in range(3):
                out[dst + c: dst + width * 3: 3] = grey
        prev = line
    return Image(width, height, bytes(out))


def read_image(path: Path) -> Image:
    data = path.read_bytes()
    if data[:2] == b"BM":
        return read_bmp(data)
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return read_png(data)
    raise ImageError(f"{path}: neither BMP nor PNG")


def write_grey_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + pixels[y * width:(y + 1) * width] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


@dataclass
class Comparison:
    width: int
    height: int
    differing: int
    max_diff: int
    psnr: float  # inf when identical
    mask: bytes

    @property
    def percent(self) -> float:
        return 100.0 * self.differing / (self.width * self.height)


def compare(a: Image, b: Image, threshold: int) -> Comparison:
    if (a.width, a.height) != (b.width, b.height):
        raise ImageError(f"size mismatch: {a.width}x{a.height} vs {b.width}x{b.height}")
    pixels = a.width * a.height
    mask = bytearray(pixels)
    differing = max_diff = 0
    squared = 0
    pa, pb = a.rgb, b.rgb
    for p in range(pixels):
        i = p * 3
        d0 = abs(pa[i] - pb[i])
        d1 = abs(pa[i + 1] - pb[i + 1])
        d2 = abs(pa[i + 2] - pb[i + 2])
        m = max(d0, d1, d2)
        if m:
            squared += d0 * d0 + d1 * d1 + d2 * d2
            if m > max_diff:
                max_diff = m
            if m > threshold:
                differing += 1
                mask[p] = 255
    mse = squared / (pixels * 3)
    psnr = math.inf if mse == 0 else 10.0 * math.log10(255.0 * 255.0 / mse)
    return Comparison(a.width, a.height, differing, max_diff, psnr, bytes(mask))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference", type=Path, help="guest-drawn image (BMP/PNG)")
    parser.add_argument("candidate", type=Path, help="native-drawn image (BMP/PNG)")
    parser.add_argument("--threshold", type=int, default=2,
                        help="per-channel difference (0-255) a pixel must exceed to count (default 2)")
    parser.add_argument("--limit", type=float, default=0.5,
                        help="maximum percentage of differing pixels before failing (default 0.5)")
    parser.add_argument("--mask", type=Path, help="write a greyscale diff-mask PNG here")
    args = parser.parse_args(argv)
    if not 0 <= args.threshold <= 255:
        parser.error("--threshold must be within 0..255")
    try:
        result = compare(read_image(args.reference), read_image(args.candidate), args.threshold)
    except (OSError, ImageError, zlib.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if args.mask:
        write_grey_png(args.mask, result.width, result.height, result.mask)
    passed = result.percent <= args.limit
    psnr = "inf" if math.isinf(result.psnr) else f"{result.psnr:.2f}"
    print(f"size={result.width}x{result.height} differing={result.differing} ({result.percent:.4f}%) "
          f"threshold={args.threshold} max_diff={result.max_diff} psnr_db={psnr} "
          f"limit={args.limit}% result={'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
