#!/usr/bin/env python3
"""Compare the two sides of a shadow render frame (edf_native_shadow_render).

A shadow frame is one simulation state rendered twice in the same render helper
call: natively (the full frame) and by the guest helper path, each into its own
targets, both captured before the HUD. The game writes, per frame F:

  <prefix>.F.native.bmp / <prefix>.F.guest.bmp               pre-HUD output images
  <prefix>.F.native.draws.jsonl / <prefix>.F.guest.draws.jsonl  draw lists
  <prefix>.F.shadow.json                                      metadata

This tool compares them:

- Images, per pixel: exact (any channel differs) and with --pixel-tolerance
  (largest channel difference above it), for known float-order differences.
  --max-pixels is how many pixels may exceed the tolerance. The report also
  has the signed mean difference per channel (native - guest): a global tone
  or exposure difference shows as a nonzero mean over most of the frame, a
  geometry difference as a few pixels far over the tolerance.
- Draw lists: each draw's structural key (vertex:pixel material, topology, kind
  and counts; instanced draws expanded to one per instance) aligns the guest
  list against the native one. The key deliberately leaves out everything the
  two paths always have differently for the same draw:
    * the pipeline identity: it hashes the raw render state words, and the
      guest path builds its pipelines from the live guest registers (with don't
      care bits, e.g. a depth function under a disabled depth test) where the
      native passes build them from owned state (kNativeOpaqueCopyState for
      the post), so every pipeline differs even where the GPU state is equal;
    * the input layout of a world-instanced alternate (it adds the instance
      stream);
    * texture sizes: a slot the shader does not read keeps whatever the
      previous pass bound (native: the shadow cascades; guest: its own);
    * first index and base vertex: native passes keep their own buffers.
  Draws are paired first, where the capture kept constant bytes, on the key
  plus the first palette bone (c4-c6: a skinned draw of one mesh on another
  object) and then plus g_mWorld (c0-c3: a rigid one), then on the key plus
  texture sizes and world matrix (so equal meshes pair with their own
  instance), then on the key alone; each stage aligns in order (difflib) and
  then pairs what is left by key in list order. A pair is "reordered" when it
  is out of order WITHIN its native pass (a longest-increasing-subsequence
  over the pass's guest indices). The native passes group draws by kind where
  the guest callbacks interleave them; which pass each list reaches first is
  compared once per frame: a pass the native frame starts in another order is
  one "pass_order" difference (label: the guest's order, pass: the native's;
  "blended" when a draw of a moved pass blends), not one per draw
  (--strict-order checks the whole list instead). What stays unpaired is
  "missing" (guest only) or "extra" (native only).
- Paired draws are compared field by field: constants (per stage/slot hash),
  world matrix (--float-tolerance), viewport, blend factor, scissor, texture
  sizes, texture and target identities (which must correspond one-to-one: the
  guest side draws into its own targets and samples its own scene resolve;
  "texture.S.N.unbound" is a slot bound on one side only),
  sampler identities (the backend deduplicates samplers on their description,
  so equal ids are equal states; compared directly), and the decoded render
  state ("state.blend", "state.mask", "state.depth", "state.raster",
  canonicalised: blend factors are ignored with blending off, the depth
  function and write with the depth test off) and target formats ("format").
  Captures without the stamped state (before the sampler/state recording)
  cannot say which part of a pipeline differs: their pipeline identity is
  compared with --strict-pipeline only, and counted in the summary. The input
  layout is compared between draws of the same instancing variant. A
  transient vertex upload (immediate geometry: the wires' strips, a post
  quad) is the draw's content and its hash is compared
  ("geometry.transient.N"; with the capture's vertex bytes, which words
  differ). Other geometry (buffer identities, the whole geometry text, first
  index, base vertex) is compared with --strict-geometry only. An expanded instanced draw has no world
  of its own: its worlds are in the instance stream, which the tap records as
  the FNV-1a hash of its transient bytes; the worlds of the draws paired with
  its instances, concatenated in instance order, must hash to it (reported per
  frame as instance_streams; "world.instances" on each pair where they do
  not). Where the capture kept constant bytes, a vertex slot-0 image in the
  Common.fx layout (COMMON_FX_REGISTERS) is compared per variable:
  "constants.vs0.<variable>", with "g_mWorld.instanced" (a world-instanced
  alternate, which does not read them) and "g_mWorldArray.tail" (past the
  native palette) named apart. A "reflected" draw (the
  D3D12 backend reflected its pipeline's shaders) lists only the texture,
  sampler and constant slots those shaders read; an unreflected one lists
  every bound slot, including ones an earlier draw left bound.
- Differences are grouped by material (vertex:pixel shader ids), by class (the
  recording label: guest callback for guest draws, native pass for native
  ones), by native pass, and by differing field; the class map says which
  native pass each guest callback's draws went to.

An allow list (--allow FILE, JSON) excuses known differences:

  {"pixels": {"tolerance": 2, "max_pixels": 64},
   "draws": [{"kind": "missing", "material": "*:00000000000000ab", "label": "guest.overlays",
              "pass": "*", "field": "*", "max": 10, "reason": "..."}]}

A draw difference is allowed when a rule matches its kind ("missing", "extra",
"differing", "reordered", "pass_order" or "*"), material, label and pass
(fnmatch globs, default "*"; label is the guest callback where there is a
guest draw, pass the native pass where there is a native one), "reflected"
(true/false: whether the draws list only the slots their shaders read;
omitted: either), "constant_bytes" (true/false: whether both draws carry their
constants' bytes; omitted: either), and for "differing", "reordered" and
"pass_order" the fields: "field" is a glob or a list of globs, and every
field must match one of them (a reordered draw or pass order has the field
"blended" when it blends, none when it is opaque). "max" caps how many a rule
excuses per run; "reason", "class" and "evidence" are for the reader. Exit 0
when nothing is left beyond the allow list, 1 when something is, 2 on
unreadable input.
Standard library only.
"""
from __future__ import annotations

import argparse
import bisect
import difflib
import fnmatch
import importlib.util
import json
import math
import struct
import sys
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
INSTANCED_VERTEX_BIT = 1 << 63  # desc.vertex_id|1<<63: a world-instanced alternate's vertex shader.


def _load_images():
    spec = importlib.util.spec_from_file_location('compare_renderer_images', TOOLS / 'compare-renderer-images.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules.setdefault(spec.name, module)
    spec.loader.exec_module(module)
    return module


class InputError(Exception):
    pass


# ---------------------------------------------------------------- images ----
@dataclass
class ImageDiff:
    width: int
    height: int
    exact: int          # pixels with any channel different
    over: int           # pixels whose largest channel difference exceeds the tolerance
    max_diff: int
    tolerance: int
    bbox: tuple[int, int, int, int] | None  # x0, y0, x1, y1 of pixels over the tolerance
    mask: bytes = b''
    signed_mean: tuple[float, float, float] = (0.0, 0.0, 0.0)  # native - guest, per channel, over the frame
    histogram: dict = field(default_factory=dict)  # largest channel difference -> pixels (1, 2, 3, 4-8, 9-16, 17+)

    def to_json(self) -> dict:
        return {'width': self.width, 'height': self.height, 'exact_differing': self.exact,
                'over_tolerance': self.over, 'max_channel_diff': self.max_diff, 'tolerance': self.tolerance,
                'bbox': list(self.bbox) if self.bbox else None,
                'signed_mean': [round(v, 4) for v in self.signed_mean], 'histogram': self.histogram}


_BUCKETS = ((1, '1'), (2, '2'), (3, '3'), (8, '4-8'), (16, '9-16'), (255, '17+'))


def diff_images(native, guest, tolerance: int) -> ImageDiff:
    if (native.width, native.height) != (guest.width, guest.height):
        raise InputError(f'image size mismatch: native {native.width}x{native.height}, guest {guest.width}x{guest.height}')
    w, h = native.width, native.height
    a, b = native.rgb, guest.rgb
    mask = bytearray(w * h)
    exact = over = max_diff = 0
    sums = [0, 0, 0]
    histogram = Counter()
    x0 = y0 = None
    x1 = y1 = -1
    if a == b:
        return ImageDiff(w, h, 0, 0, 0, tolerance, None, bytes(mask))
    for y in range(h):
        row = y * w * 3
        if a[row:row + w * 3] == b[row:row + w * 3]:
            continue
        for x in range(w):
            i = row + x * 3
            dr, dg, db = a[i] - b[i], a[i + 1] - b[i + 1], a[i + 2] - b[i + 2]
            m = max(abs(dr), abs(dg), abs(db))
            if not m:
                continue
            sums[0] += dr
            sums[1] += dg
            sums[2] += db
            exact += 1
            max_diff = max(max_diff, m)
            for limit, name in _BUCKETS:
                if m <= limit:
                    histogram[name] += 1
                    break
            if m > tolerance:
                over += 1
                mask[y * w + x] = 255
                x0 = x if x0 is None else min(x0, x)
                y0 = y if y0 is None else min(y0, y)
                x1 = max(x1, x)
                y1 = max(y1, y)
    bbox = (x0, y0, x1, y1) if x0 is not None else None
    total = w * h
    return ImageDiff(w, h, exact, over, max_diff, tolerance, bbox, bytes(mask),
                     tuple(s / total for s in sums), {name: histogram[name] for _, name in _BUCKETS if histogram[name]})


# ----------------------------------------------------------------- draws ----
def load_draws(path: Path) -> tuple[dict, list[dict]]:
    try:
        lines = path.read_text(encoding='utf-8').splitlines()
    except OSError as error:
        raise InputError(f'{path}: {error}') from error
    if not lines:
        raise InputError(f'{path}: empty draw list')
    try:
        header = json.loads(lines[0])
        draws = [json.loads(line) for line in lines[1:] if line.strip()]
    except json.JSONDecodeError as error:
        raise InputError(f'{path}: {error}') from error
    if header.get('format') != 'edf-shadow-draws':
        raise InputError(f'{path}: not a shadow draw list')
    if header.get('draws') is not None and header['draws'] != len(draws):
        raise InputError(f'{path}: header says {header["draws"]} draws, file has {len(draws)}')
    return header, draws


def _hex(value) -> int:
    return int(value, 16) if isinstance(value, str) else int(value)


def material(draw: dict) -> str:
    vs = _hex(draw.get('vs', '0')) & ~INSTANCED_VERTEX_BIT
    return f'{vs:016x}:{_hex(draw.get("ps", "0")):016x}'


def instanced_variant(draw: dict) -> bool:
    return bool(_hex(draw.get('vs', '0')) & INSTANCED_VERTEX_BIT)


INSTANCE_STREAM_SLOT, INSTANCE_WORLD_BYTES = 15, 64  # The world-instanced alternates' EDFINSTANCE0-3 stream.


def instance_stream(draw: dict) -> tuple[str, int] | None:
    """(hash, bytes) of an instanced draw's transient instance stream (the
    tap's "15=t:<FNV-1a 64 of the bytes>:<bytes>/<stride>"), when it holds
    exactly one 64-byte world per instance from the first."""
    for part in (draw.get('geometry') or '').split():
        slot, _, value = part.partition('=')
        if slot != str(INSTANCE_STREAM_SLOT) or not value.startswith('t:'):
            continue
        try:
            digest, extent = value[2:].split(':')
            size, stride = (int(v) for v in extent.split('/'))
        except ValueError:
            return None
        if stride == INSTANCE_WORLD_BYTES and not draw.get('first_instance') and \
                size == int(draw.get('instances', 1)) * INSTANCE_WORLD_BYTES:
            return digest, size
    return None


def transient_streams(draw: dict) -> dict[int, tuple[str, int, int]]:
    """The draw's transient vertex uploads but the instance stream: slot ->
    (hash, bytes, stride), from the tap's "N=t:<hash>:<bytes>/<stride>"."""
    out = {}
    for part in (draw.get('geometry') or '').split():
        slot, _, value = part.partition('=')
        if not slot.isdigit() or int(slot) == INSTANCE_STREAM_SLOT or not value.startswith('t:'):
            continue
        try:
            digest, extent = value[2:].split(':')
            size, stride = (int(v) for v in extent.split('/'))
        except ValueError:
            continue
        out[int(slot)] = (digest, size, stride)
    return out


def transient_bytes(draw: dict, slot: int) -> bytes | None:
    """A transient upload's bytes, where the capture kept them (the tap's
    "vertices", edf_native_shadow_render_constants)."""
    for entry in draw.get('vertices') or []:
        if entry.get('slot') == slot and entry.get('data') is not None:
            return bytes.fromhex(entry['data'])
    return None


def _ordered(bits: int) -> int:
    return bits if bits < 0x80000000 else -(bits & 0x7fffffff)


def vertex_differences(guest: bytes, native: bytes, stride: int) -> dict:
    """Which 32-bit words of two vertex uploads differ, read as host floats:
    how many, in how many vertices, the largest absolute and ulp difference,
    and the first (vertex, byte offset in the vertex)."""
    if len(guest) != len(native):
        return {'registers': f'sizes {len(guest)}/{len(native)}', 'max_abs': math.inf}
    words = vertices = 0
    largest, ulps, first = 0.0, 0, None
    seen_vertex = -1
    for at in range(0, len(guest) - len(guest) % 4, 4):
        if guest[at:at + 4] == native[at:at + 4]:
            continue
        words += 1
        vertex = at // stride if stride else 0
        if vertex != seen_vertex:
            vertices += 1
            seen_vertex = vertex
        if first is None:
            first = (vertex, at - vertex * stride if stride else at)
        a, b = struct.unpack_from('<f', guest, at)[0], struct.unpack_from('<f', native, at)[0]
        if math.isfinite(a) and math.isfinite(b):
            largest = max(largest, abs(a - b))
        ua, ub = struct.unpack_from('<I', guest, at)[0], struct.unpack_from('<I', native, at)[0]
        ulps = max(ulps, abs(_ordered(ua) - _ordered(ub)))
    return {'registers': f'{words} words in {vertices} vertices, first vertex {first[0]} +{first[1]}, max {ulps} ulp'
            if first else 'none', 'max_abs': largest}


def fnv1a64(data: bytes) -> int:
    """NativeShadowHash."""
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def world_bytes(world) -> bytes | None:
    """A recorded world as the 64 host bytes an instance stream holds; None
    when it is not 16 finite numbers."""
    if not world or len(world) != 16 or any(isinstance(v, str) for v in world):
        return None
    return struct.pack('<16f', *world)


def verify_instance_streams(pairs: list[tuple[int, int]], guest: list[dict], native: list[dict]) -> dict:
    """The instance streams of each side's expanded instanced draws against the
    worlds of the draws the other side paired with its instances: the worlds
    concatenated in instance order must hash to the recorded stream. Returns
    {(side, list index): True (verified) / False (differs)} for every expanded
    draw of a stream whose instances are all paired with draws of known world;
    streams that cannot be checked are left out (their worlds stay unknown)."""
    streams: dict[tuple, dict[int, tuple[int, dict]]] = defaultdict(dict)
    for g, n in pairs:
        for side, mine, index, other in (('native', native[n], n, guest[g]), ('guest', guest[g], g, native[n])):
            if mine.get('_stream'):
                streams[(side,) + mine['_stream']][mine['_instance']] = (index, other)
    verdict = {}
    for (side, _, count, digest, size), members in streams.items():
        if len(members) != count:
            continue
        data = [world_bytes(members[k][1].get('world')) for k in range(count)]
        if any(d is None for d in data):
            continue
        ok = f'{fnv1a64(b"".join(data)):016x}' == digest
        for index, _ in members.values():
            verdict[(side, index)] = ok
    return verdict


def expand(draws: list[dict], expand_instances: bool) -> list[dict]:
    """Instanced draws as one draw per instance (a native pass may instance
    what the guest draws one by one); indexed kinds become one kind."""
    out = []
    for draw in draws:
        count = int(draw.get('instances', 1)) if expand_instances else 1
        stream = instance_stream(draw) if count > 1 else None
        for instance in range(max(count, 1)):
            copy = dict(draw)
            if expand_instances:
                copy['instances'] = 1
                copy['first_instance'] = 0
                if copy.get('kind') == 'instanced':
                    copy['kind'] = 'indexed'
                    if count > 1:
                        # Per-instance worlds live in the instance stream, which
                        # the list does not record: not compared for these.
                        copy['world'] = None
                        copy['_world_unknown'] = True
                        if stream:
                            copy['_stream'] = (draw.get('i'), count) + stream
                copy['_instance'] = instance
            out.append(copy)
    return out


def structural_key(draw: dict) -> tuple:
    """What a draw IS on both paths: material, topology, kind and counts."""
    return (material(draw), draw.get('topology'), draw.get('kind'), draw.get('count'), draw.get('instances', 1))


def _rounded(values):
    if not values:
        return None
    return tuple(round(v, 3) if isinstance(v, (int, float)) else v for v in values)


def _texture_name(texture: dict, shared: set[str]) -> str:
    # A texture both lists use is one object: its id. A side's own (the
    # shadow's scene resolve) can only be named by its size.
    return texture['id'] if texture['id'] in shared else f'{texture["w"]}x{texture["h"]}'


def fine_key(draw: dict, shared: set[str] = frozenset()) -> tuple:
    """The structural key plus what tells two draws of one mesh apart: every
    texture and the world matrix."""
    textures = tuple(sorted((t['stage'], t['slot'], _texture_name(t, shared)) for t in draw.get('textures', [])))
    return structural_key(draw) + (textures, _rounded(draw.get('world')))


def vertex_image(draw: dict) -> bytes | None:
    """The vertex stage's slot-0 constant bytes, where the capture kept them
    (edf_native_shadow_render_constants)."""
    for constant in draw.get('constants', []):
        if constant['stage'] == 0 and constant['slot'] == 0 and constant.get('data'):
            return bytes.fromhex(constant['data'])
    return None


# What tells two draws of one mesh on two objects apart where no world is
# recorded, in the Common.fx layout (COMMON_FX_REGISTERS): a skinned draw's
# first palette bone (c4-c6), a rigid draw's g_mWorld (c0-c3). Each is live and
# bit-exact on both paths for the draws that read it; the other holds whatever
# the path left there (a skinned shader does not read g_mWorld, a rigid one
# not the palette), so each is a pairing stage of its own.
BONE_REGISTERS, WORLD_REGISTERS = (4, 7), (0, 4)
_PALETTE_FILL = (bytes(16), struct.pack('<4f', 0.0, 0.0, 0.0, 1.0))


def _registers_key(draw: dict, registers: tuple[int, int], live_palette: bool) -> tuple:
    """The structural key plus a register window of the vertex image. A draw
    without recorded constant bytes gets a key nothing else has, and so does an
    instanced draw (its image is shared by its instances, and a world-instanced
    alternate does not read its g_mWorld) and, for the palette window, a draw
    whose first bone is only fill (zero or 0,0,0,1: no palette uploaded)."""
    image = vertex_image(draw)
    first, end = registers
    if image is None or len(image) < COMMON_FX_MINIMUM_BYTES or draw.get('_world_unknown') or instanced_variant(draw):
        return ('unkeyed', id(draw))
    window = image[first * 16:end * 16]
    if live_palette and all(window[r * 16:r * 16 + 16] in _PALETTE_FILL for r in range(end - first)):
        return ('unkeyed', id(draw))
    return structural_key(draw) + (window,)


def bone_key(draw: dict) -> tuple:
    return _registers_key(draw, BONE_REGISTERS, True)


def world_register_key(draw: dict) -> tuple:
    return _registers_key(draw, WORLD_REGISTERS, False)


def medium_key(draw: dict, shared: set[str] = frozenset()) -> tuple:
    """The structural key plus the first texture and the world matrix: what
    still tells meshes apart where a later slot holds a stale binding."""
    first = min(((t['stage'], t['slot'], _texture_name(t, shared)) for t in draw.get('textures', [])), default=None)
    return structural_key(draw) + (first, _rounded(draw.get('world')))


def canonical_state(decoded: str) -> dict[str, str]:
    """The decoded state text (NativeShadowStateText) as parts, with what the
    GPU ignores dropped: blend factors with blending off, the depth function
    and write with the depth test off."""
    if not decoded or decoded == 'undecodable':
        return {'state': decoded or 'none'}
    parts = dict(item.split('=', 1) for item in decoded.split() if '=' in item)
    blend = parts.get('blend', '')
    if blend.startswith('0:'):
        parts['blend'] = 'off'
    depth = parts.get('depth', '')
    if depth.startswith('0/'):
        parts['depth'] = 'off'
    return parts


class Correspondence:
    """Identities that must map one-to-one between the sides (targets, textures).

    An identity that appears in both lists (`shared`: a game texture, a post
    target both routes draw into) is one object both sides use, so it must be
    the same on both; only a side's own objects (the shadow's scene color and
    its resolve against the native ones) map through the correspondence. That
    keeps one stale binding from poisoning the map for the real ones."""

    def __init__(self, shared: set[str] | None = None):
        self.forward: dict[str, str] = {}
        self.backward: dict[str, str] = {}
        self.shared = shared or set()

    def check(self, guest: str, native: str) -> bool:
        if guest in self.shared or native in self.shared:
            return guest == native
        if guest in self.forward or native in self.backward:
            return self.forward.get(guest) == native and self.backward.get(native) == guest
        self.forward[guest] = native
        self.backward[native] = guest
        return True


def _floats_equal(a, b, tolerance: float) -> bool:
    if a is None or b is None:
        return a is None and b is None
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if isinstance(x, str) or isinstance(y, str):
            if x != y:
                return False
            continue
        if tolerance <= 0:
            if x != y:
                return False
        elif not math.isclose(x, y, rel_tol=tolerance, abs_tol=tolerance):
            return False
    return True


def constant_registers(guest: str, native: str) -> dict:
    """Which float4 registers of two constant images (hex, as the tap writes
    them with edf_native_shadow_render_constants) differ, and by how much."""
    a, b = bytes.fromhex(guest), bytes.fromhex(native)
    registers = []
    largest = 0.0
    for r in range(max(len(a), len(b)) // 16 + (1 if max(len(a), len(b)) % 16 else 0)):
        x, y = a[r * 16:r * 16 + 16], b[r * 16:r * 16 + 16]
        if x == y:
            continue
        registers.append(r)
        if len(x) == len(y) == 16:
            for i in range(0, 16, 4):
                u, v = struct.unpack_from('<f', x, i)[0], struct.unpack_from('<f', y, i)[0]
                if math.isfinite(u) and math.isfinite(v):
                    largest = max(largest, abs(u - v))
    runs = []
    for r in registers:
        if runs and runs[-1][1] == r - 1:
            runs[-1][1] = r
        else:
            runs.append([r, r])
    return {'registers': ','.join(f'c{s}' if s == e else f'c{s}-c{e}' for s, e in runs), 'max_abs': largest}


# The vertex $Globals prefix every model and world shader shares: each .dxsl
# includes Common.fx, whose globals come first, and D3DCompile (vs_5_0, as
# CompileNativeShader) lays them out identically in all 59 vertex entries with
# a constant image of 3744 bytes or more (fxc /T vs_5_0 over the disc's Shader/
# sources: g_mWorld at offset 0, g_mWorldArray (float4x3[68]) at 64, g_mView at
# 3328 ...). Smaller images (Utility's older [32]-bone layouts, the post's
# quad shaders) have other layouts and are not classified.
COMMON_FX_REGISTERS = ((0, 4, 'g_mWorld'), (4, 208, 'g_mWorldArray'), (208, 212, 'g_mView'),
                       (212, 216, 'g_mViewTranspose'), (216, 220, 'g_mViewProjection'), (220, 224, 'g_mProjection'))
COMMON_FX_MINIMUM_BYTES = 224 * 16  # c224 on: lights, fog, shadow, material and per-object globals (g_Time, g_Highlight).
_ZERO_REGISTER = bytes(16)


def classify_vertex_registers(guest: bytes, native: bytes, instanced: bool) -> dict[str, list[int]]:
    """The differing registers of a Common.fx vertex image by variable, as
    field suffixes:
      g_mWorld.instanced   g_mWorld where a draw is a world-instanced alternate:
                           that shader reads its world from the instance stream
                           (AddNativeWorldInstancing rewrites every g_mWorld use
                           to edf_instance_world and keeps the declaration only
                           for the layout), so the registers are unread;
      g_mWorldArray.tail   palette registers past the native palette: zero on
                           the native side (BindNativeFullFrameModelPalette
                           writes the bones over zeroed registers) and after
                           its last nonzero register. The guest writes the same
                           count (821A1738: min(pose bones, descriptor+16)), so
                           past it its scratch holds the (0,0,0,1) fill or an
                           earlier object's bones; a vertex indexes only its
                           mesh's bones;
      <name>               any other difference in a named variable;
      material             c224 on (lights, fog, material, per-object globals)."""
    count = max(len(guest), len(native)) // 16
    last_live = 3
    for r in range(4, min(208, len(native) // 16)):
        if native[r * 16:r * 16 + 16] != _ZERO_REGISTER:
            last_live = r
    out: dict[str, list[int]] = {}
    for r in range(count):
        if guest[r * 16:r * 16 + 16] == native[r * 16:r * 16 + 16]:
            continue
        name = 'material'
        for first, end, variable in COMMON_FX_REGISTERS:
            if first <= r < end:
                name = variable
                break
        if name == 'g_mWorld' and instanced:
            name = 'g_mWorld.instanced'
        elif name == 'g_mWorldArray' and r > last_live and native[r * 16:r * 16 + 16] == _ZERO_REGISTER:
            name = 'g_mWorldArray.tail'
        out.setdefault(name, []).append(r)
    return out


def _register_runs(registers: list[int]) -> str:
    runs: list[list[int]] = []
    for r in registers:
        if runs and runs[-1][1] == r - 1:
            runs[-1][1] = r
        else:
            runs.append([r, r])
    return ','.join(f'c{s}' if s == e else f'c{s}-c{e}' for s, e in runs)


def compare_fields(guest: dict, native: dict, settings: 'Settings', textures: Correspondence,
                   targets: Correspondence, details: dict | None = None) -> list[str]:
    fields = []
    g_constants = {(c['stage'], c['slot']): c for c in guest.get('constants', [])}
    n_constants = {(c['stage'], c['slot']): c for c in native.get('constants', [])}
    for slot in sorted(set(g_constants) | set(n_constants)):
        g, n = g_constants.get(slot), n_constants.get(slot)
        if g is None or n is None or g['hash'] != n['hash'] or g['bytes'] != n['bytes']:
            name = f'constants.{"vs" if slot[0] == 0 else "ps" if slot[0] == 1 else "cs"}{slot[1]}'
            if g is not None and n is not None and g.get('data') and n.get('data'):
                a, b = bytes.fromhex(g['data']), bytes.fromhex(n['data'])
                if slot == (0, 0) and len(a) == len(b) >= COMMON_FX_MINIMUM_BYTES:
                    # Name the variables that differ (the capture kept the bytes).
                    groups = classify_vertex_registers(a, b, instanced_variant(guest) or instanced_variant(native))
                    for variable, registers in groups.items():
                        fields.append(f'{name}.{variable}')
                        if details is not None:
                            details[f'{name}.{variable}'] = {'registers': _register_runs(registers),
                                                             'max_abs': constant_registers(
                                                                 b''.join(a[r * 16:r * 16 + 16] for r in registers).hex(),
                                                                 b''.join(b[r * 16:r * 16 + 16] for r in registers).hex())['max_abs']}
                    if not groups:  # Same bytes, different hash: cannot happen for one hash function.
                        fields.append(name)
                    continue
                if details is not None:
                    details[name] = constant_registers(g['data'], n['data'])
            fields.append(name)
    if not (guest.get('_world_unknown') or native.get('_world_unknown')) and \
            not _floats_equal(guest.get('world'), native.get('world'), settings.float_tolerance):
        fields.append('world')
    if not _floats_equal(guest.get('viewport'), native.get('viewport'), settings.float_tolerance):
        fields.append('viewport')
    if not _floats_equal(guest.get('blend'), native.get('blend'), settings.float_tolerance):
        fields.append('blend')
    if 'scissor' in guest and 'scissor' in native and guest['scissor'] != native['scissor']:
        fields.append('scissor')
    g_textures = {(t['stage'], t['slot']): t for t in guest.get('textures', [])}
    n_textures = {(t['stage'], t['slot']): t for t in native.get('textures', [])}
    for slot in sorted(set(g_textures) | set(n_textures)):
        g, n = g_textures.get(slot), n_textures.get(slot)
        if g is None or n is None:
            # Bound on one side only. Where both lists are reflected (only the
            # slots the shaders read) that is a missing input; otherwise it
            # can be a slot an earlier draw left bound on one path.
            reflected = guest.get('reflected') and native.get('reflected')
            fields.append(f'texture.{slot[0]}.{slot[1]}.{"missing" if reflected else "unbound"}')
            continue
        if (g['w'], g['h']) != (n['w'], n['h']):
            fields.append(f'texture.{slot[0]}.{slot[1]}.size')
        elif not textures.check(g['id'], n['id']):
            fields.append(f'texture.{slot[0]}.{slot[1]}')
    if 'samplers' in guest and 'samplers' in native:
        g_samplers = {(s['stage'], s['slot']): s['id'] for s in guest['samplers']}
        n_samplers = {(s['stage'], s['slot']): s['id'] for s in native['samplers']}
        for slot in sorted(set(g_samplers) | set(n_samplers)):
            if g_samplers.get(slot) != n_samplers.get(slot):
                fields.append(f'sampler.{slot[0]}.{slot[1]}')
    g_targets = list(guest.get('targets', [])) + [guest.get('depth')]
    n_targets = list(native.get('targets', [])) + [native.get('depth')]
    if len(g_targets) != len(n_targets) or not all(targets.check(str(g), str(n)) for g, n in zip(g_targets, n_targets)):
        fields.append('targets')
    if 'decoded' in guest and 'decoded' in native:
        g_state, n_state = canonical_state(guest['decoded']), canonical_state(native['decoded'])
        for part in sorted(set(g_state) | set(n_state)):
            if g_state.get(part) != n_state.get(part):
                fields.append(f'state.{part}')
        if guest.get('format') != native.get('format'):
            fields.append('format')
    elif settings.strict_pipeline and guest.get('pipeline') != native.get('pipeline'):
        fields.append('pipeline')
    if instanced_variant(guest) == instanced_variant(native) and guest.get('layout') != native.get('layout'):
        fields.append('layout')
    # Immediate geometry: a transient vertex upload is the draw's content (both
    # paths convert the guest's vertices the same way), so its hash is
    # compared; slot 15, the instance stream, only through the instance check
    # (a draw that does not instance leaves an earlier stream bound there).
    g_transient, n_transient = transient_streams(guest), transient_streams(native)
    for slot in sorted(set(g_transient) & set(n_transient)):
        if g_transient[slot] != n_transient[slot]:
            name = f'geometry.transient.{slot}'
            fields.append(name)
            a, b = transient_bytes(guest, slot), transient_bytes(native, slot)
            if details is not None and a is not None and b is not None:
                details[name] = vertex_differences(a, b, g_transient[slot][2])
    if settings.strict_geometry:
        if guest.get('geometry') != native.get('geometry'):
            fields.append('geometry')
        if (guest.get('first'), guest.get('base')) != (native.get('first'), native.get('base')):
            fields.append('range')
    return fields


@dataclass
class Difference:
    kind: str       # missing | extra | differing | reordered
    material: str
    label: str      # the guest callback where there is a guest draw, else the native pass
    fields: list[str] = field(default_factory=list)
    guest_index: int | None = None
    native_index: int | None = None
    allowed_by: str | None = None
    native_label: str = ''  # the native pass where there is a native draw, else the guest callback
    details: dict = field(default_factory=dict)  # per field, where the capture says more (constant registers)
    reflected: bool = False  # both draws list only the slots their shaders read
    constant_bytes: bool = False  # both draws carry their constants' bytes (edf_native_shadow_render_constants)

    def to_json(self) -> dict:
        out = {'kind': self.kind, 'material': self.material, 'label': self.label, 'pass': self.native_label,
               'fields': self.fields, 'guest': self.guest_index, 'native': self.native_index,
               'allowed_by': self.allowed_by, 'reflected': self.reflected, 'constant_bytes': self.constant_bytes}
        if self.details:
            out['details'] = self.details
        return out


@dataclass
class Settings:
    pixel_tolerance: int = 0
    max_pixels: int = 0
    float_tolerance: float = 0.0
    expand_instances: bool = True
    strict_geometry: bool = False
    check_order: bool = True
    strict_order: bool = False
    strict_pipeline: bool = False


@dataclass
class DrawDiff:
    guest_draws: int
    native_draws: int
    matched: int
    differences: list[Difference]
    classes: dict = field(default_factory=dict)      # guest label -> {native label: pairs}
    pass_order: dict = field(default_factory=dict)   # the pass sequences in each list's order
    pipeline_identity_differs: int = 0               # pairs whose raw pipeline identity differs
    instance_streams: dict = field(default_factory=dict)  # expanded instances whose stream was checked: verified/differs
    pairs: list = field(default_factory=list)        # (guest draw, native draw) as paired, expanded; not reported

    def groups(self) -> dict:
        by_material: dict[str, Counter] = defaultdict(Counter)
        by_label: dict[str, Counter] = defaultdict(Counter)
        by_pass: dict[str, Counter] = defaultdict(Counter)
        by_field: Counter = Counter()
        for difference in self.differences:
            by_material[difference.material][difference.kind] += 1
            by_label[difference.label][difference.kind] += 1
            by_pass[difference.native_label][difference.kind] += 1
            by_field.update(difference.fields)
        return {'material': {k: dict(v) for k, v in sorted(by_material.items())},
                'label': {k: dict(v) for k, v in sorted(by_label.items())},
                'pass': {k: dict(v) for k, v in sorted(by_pass.items())},
                'field': dict(by_field.most_common())}


def _pair(g_keys: list, n_keys: list, guest: list[int], native: list[int]):
    """Pairs guest and native indices with equal keys: in order where the
    sequences align, then by key in list order. Returns (pairs, guest left,
    native left)."""
    matcher = difflib.SequenceMatcher(None, [g_keys[i] for i in guest], [n_keys[i] for i in native], autojunk=False)
    pairs: list[tuple[int, int]] = []
    left_guest: list[int] = []
    left_native: list[int] = []
    for tag, g0, g1, n0, n1 in matcher.get_opcodes():
        if tag == 'equal':
            pairs.extend((guest[g0 + k], native[n0 + k]) for k in range(g1 - g0))
        else:
            left_guest.extend(guest[g0:g1])
            left_native.extend(native[n0:n1])
    waiting: dict[tuple, deque] = defaultdict(deque)
    for n in left_native:
        waiting[n_keys[n]].append(n)
    still_guest = []
    for g in left_guest:
        queue = waiting.get(g_keys[g])
        if queue:
            pairs.append((g, queue.popleft()))
        else:
            still_guest.append(g)
    still_native = sorted(n for queue in waiting.values() for n in queue)
    return pairs, still_guest, still_native


def _increasing(values: list[int]) -> set[int]:
    """Positions of one longest strictly increasing subsequence of values."""
    tails: list[int] = []       # smallest tail value of an increasing run of each length
    tail_at: list[int] = []     # its position
    previous = [-1] * len(values)
    for position, value in enumerate(values):
        k = bisect.bisect_left(tails, value)
        if k == len(tails):
            tails.append(value)
            tail_at.append(position)
        else:
            tails[k] = value
            tail_at[k] = position
        previous[position] = tail_at[k - 1] if k else -1
    keep = set()
    position = tail_at[-1] if tail_at else -1
    while position >= 0:
        keep.add(position)
        position = previous[position]
    return keep


def _first_order(runs: list[list]) -> list[str]:
    seen: list[str] = []
    for label, _ in runs:
        if label not in seen:
            seen.append(label)
    return seen


def _runs(labels: list[str]) -> list[list]:
    runs: list[list] = []
    for label in labels:
        if runs and runs[-1][0] == label:
            runs[-1][1] += 1
        else:
            runs.append([label, 1])
    return runs


def diff_draws(guest_draws: list[dict], native_draws: list[dict], settings: Settings) -> DrawDiff:
    guest = expand(guest_draws, settings.expand_instances)
    native = expand(native_draws, settings.expand_instances)
    def ids(draws: list[dict], name: str) -> set[str]:
        if name == 'textures':
            return {t['id'] for d in draws for t in d.get('textures', [])}
        return {str(t) for d in draws for t in list(d.get('targets', [])) + [d.get('depth')]}
    shared_textures = ids(guest, 'textures') & ids(native, 'textures')
    # Pair on the most specific key first, then on less, each stage taking
    # what the one before left.
    pairs: list[tuple[int, int]] = []
    still_guest, still_native = list(range(len(guest))), list(range(len(native)))
    for key in (bone_key, world_register_key, lambda d: fine_key(d, shared_textures),
                lambda d: medium_key(d, shared_textures), structural_key):
        more, still_guest, still_native = _pair([key(d) for d in guest], [key(d) for d in native], still_guest, still_native)
        pairs.extend(more)
    pairs.sort()
    # Order: within each native pass (or across the list with --strict-order).
    in_order: set[tuple[int, int]] = set()
    groups: dict[str, list[tuple[int, int]]] = defaultdict(list)
    for g, n in pairs:
        groups['' if settings.strict_order else native[n].get('label', '')].append((g, n))
    for members in groups.values():
        members.sort(key=lambda pair: pair[1])
        keep = _increasing([g for g, _ in members])
        in_order.update(members[k] for k in keep)
    textures = Correspondence(shared_textures)
    targets = Correspondence(ids(guest, 'targets') & ids(native, 'targets') - {'0000000000000000', 'None'})
    streams = verify_instance_streams(pairs, guest, native)
    differences: list[Difference] = []
    matched = identity_differs = 0
    classes: dict[str, Counter] = defaultdict(Counter)
    for g, n in pairs:
        classes[guest[g].get('label', '')][native[n].get('label', '')] += 1
        if guest[g].get('pipeline') != native[n].get('pipeline'):
            identity_differs += 1
        details: dict = {}
        fields = compare_fields(guest[g], native[n], settings, textures, targets, details)
        if False in (streams.get(('native', n)), streams.get(('guest', g))):
            # The instance stream is not the paired draws' worlds in instance order.
            fields.append('world.instances')
        label, native_label = guest[g].get('label', ''), native[n].get('label', '')
        ordered = (g, n) in in_order
        reflected = bool(guest[g].get('reflected') and native[n].get('reflected'))
        if fields:
            with_bytes = all(c.get('data') for d in (guest[g], native[n]) for c in d.get('constants', []))
            differences.append(Difference('differing', material(guest[g]), label, fields, guest[g].get('i'),
                                          native[n].get('i'), native_label=native_label, details=details,
                                          reflected=reflected, constant_bytes=with_bytes))
        if not ordered and settings.check_order:
            # Order matters for a blending draw; for an opaque depth-tested one
            # only at exact depth ties. "blended" says which, where the capture
            # has the decoded state.
            blended = any(canonical_state(d['decoded']).get('blend', 'off') != 'off'
                          for d in (guest[g], native[n]) if 'decoded' in d)
            differences.append(Difference('reordered', material(guest[g]), label, ['blended'] if blended else [],
                                          guest[g].get('i'), native[n].get('i'), native_label=native_label,
                                          reflected=reflected))
        if not fields and (ordered or not settings.check_order):
            matched += 1
    for g in still_guest:
        label = guest[g].get('label', '')
        differences.append(Difference('missing', material(guest[g]), label, [], guest[g].get('i'), None, native_label=label,
                                      reflected=bool(guest[g].get('reflected'))))
    for n in still_native:
        label = native[n].get('label', '')
        differences.append(Difference('extra', material(native[n]), label, [], None, native[n].get('i'), native_label=label,
                                      reflected=bool(native[n].get('reflected'))))
    # The pass sequence in each list's order, by the native pass each draw went to.
    native_of_guest = {g: native[n].get('label', '') for g, n in pairs}
    order = {'guest': _runs([native_of_guest.get(g, guest[g].get('label', '')) for g in range(len(guest))]),
             'native': _runs([d.get('label', '') for d in native])}
    # The passes in the order each list first reaches them (an interleaving of
    # the guest's is not a difference, only which pass starts first): a pass
    # the native frame draws before one the guest draws it after is a
    # "pass_order" difference, "blended" when a draw of a pass whose place
    # differs blends (it then blends over, and hides behind it, other content).
    if settings.check_order and not settings.strict_order:
        g_first, n_first = _first_order(order['guest']), _first_order(order['native'])
        both = set(g_first) & set(n_first)
        g_first, n_first = [p for p in g_first if p in both], [p for p in n_first if p in both]
        if g_first != n_first:
            moved = {p for p in both if g_first.index(p) != n_first.index(p)}
            blended = any(canonical_state(d['decoded']).get('blend', 'off') != 'off'
                          for d in native if d.get('label', '') in moved and 'decoded' in d)
            differences.append(Difference('pass_order', '*', ' > '.join(g_first), ['blended'] if blended else [],
                                          native_label=' > '.join(n_first)))
    result = DrawDiff(len(guest), len(native), matched, differences,
                      {k: dict(v) for k, v in sorted(classes.items())}, order, identity_differs)
    result.instance_streams = dict(Counter('verified' if ok else 'differs' for ok in streams.values()))
    result.pairs = [(guest[g], native[n]) for g, n in pairs]
    return result


# ------------------------------------------------------------- allow list ---
@dataclass
class Rule:
    kind: str = '*'
    material: str = '*'
    label: str = '*'
    field: str | list[str] = '*'
    max: int | None = None
    reason: str = ''
    used: int = 0
    pass_: str = '*'
    reflected: bool | None = None  # None: any; False: only differences between unreflected draws
    constant_bytes: bool | None = None  # None: any; False: only differences between draws without constant bytes

    def name(self) -> str:
        return self.reason or f'{self.kind} {self.material} {self.label} {self.pass_} {self.field}'

    def matches(self, difference: Difference) -> bool:
        if self.max is not None and self.used >= self.max:
            return False
        if not (self.kind == '*' or self.kind == difference.kind):
            return False
        if not fnmatch.fnmatchcase(difference.material, self.material) or not fnmatch.fnmatchcase(difference.label, self.label):
            return False
        if not fnmatch.fnmatchcase(difference.native_label, self.pass_):
            return False
        if self.reflected is not None and difference.reflected != self.reflected:
            return False
        if self.constant_bytes is not None and difference.constant_bytes != self.constant_bytes:
            return False
        if difference.kind in ('differing', 'reordered', 'pass_order'):
            globs = [self.field] if isinstance(self.field, str) else list(self.field)
            return all(any(fnmatch.fnmatchcase(f, glob) for glob in globs) for f in difference.fields)
        return True


def load_allow(path: Path | None) -> tuple[dict, list[Rule]]:
    if path is None:
        return {}, []
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as error:
        raise InputError(f'{path}: {error}') from error
    rules = []
    for entry in data.get('draws', []):
        unknown = set(entry) - {'kind', 'material', 'label', 'pass', 'field', 'max', 'reason', 'evidence', 'reflected',
                                'class', 'constant_bytes'}
        if unknown:
            raise InputError(f'{path}: unknown rule keys {sorted(unknown)}')
        entry = {k: v for k, v in entry.items() if k not in ('evidence', 'class')}
        if 'pass' in entry:
            entry['pass_'] = entry.pop('pass')
        rules.append(Rule(**entry))
    return data.get('pixels', {}), rules


def apply_allow(differences: list[Difference], rules: list[Rule]) -> list[Difference]:
    remaining = []
    for difference in differences:
        for rule in rules:
            if rule.matches(difference):
                rule.used += 1
                difference.allowed_by = rule.name()
                break
        else:
            remaining.append(difference)
    return remaining


# ------------------------------------------------------------------ frames --
@dataclass
class FrameFiles:
    stem: str
    native_image: Path
    guest_image: Path
    native_draws: Path
    guest_draws: Path
    meta: Path | None


def frame_files(path: Path) -> FrameFiles:
    name = path.name
    for suffix in ('.shadow.json', '.native.bmp', '.guest.bmp', '.native.draws.jsonl', '.guest.draws.jsonl'):
        if name.endswith(suffix):
            name = name[:-len(suffix)]
            break
    stem = path.with_name(name)
    meta = Path(f'{stem}.shadow.json')
    return FrameFiles(str(stem), Path(f'{stem}.native.bmp'), Path(f'{stem}.guest.bmp'),
                      Path(f'{stem}.native.draws.jsonl'), Path(f'{stem}.guest.draws.jsonl'), meta if meta.exists() else None)


def find_frames(path: Path) -> list[FrameFiles]:
    if path.is_dir():
        frames = [frame_files(meta) for meta in sorted(path.glob('*.shadow.json'))]
        if not frames:
            raise InputError(f'{path}: no *.shadow.json frames')
        return frames
    return [frame_files(path)]


def compare_frame(files: FrameFiles, settings: Settings, rules: list[Rule], images_module, diff_mask: Path | None) -> dict:
    report: dict = {'stem': files.stem}
    unallowed = 0
    if files.meta:
        meta = json.loads(files.meta.read_text(encoding='utf-8'))
        report['meta'] = {k: meta.get(k) for k in ('frame', 'views', 'motion', 'tick_frame', 'held', 'restored_words',
                                                   'samplers_restored')}
        if meta.get('guest', {}).get('error'):
            report['guest_error'] = meta['guest']['error']
            unallowed += 1
    # Images.
    if not files.guest_image.exists() or not files.native_image.exists():
        missing = [str(p) for p in (files.native_image, files.guest_image) if not p.exists()]
        report['image'] = {'error': f'missing image(s): {", ".join(missing)}'}
        unallowed += 1
    else:
        try:
            native = images_module.read_image(files.native_image)
            guest = images_module.read_image(files.guest_image)
        except images_module.ImageError as error:
            raise InputError(f'{files.stem}: {error}') from error
        image = diff_images(native, guest, settings.pixel_tolerance)
        report['image'] = image.to_json()
        report['image']['max_pixels'] = settings.max_pixels
        report['image']['pass'] = image.over <= settings.max_pixels
        if image.over > settings.max_pixels:
            unallowed += 1
        if diff_mask is not None and image.over:
            images_module.write_grey_png(diff_mask, image.width, image.height, image.mask)
            report['image']['mask'] = str(diff_mask)
    # The pre-post scene images, when the capture has them: diagnostic only
    # (HDR clamped to 0..1), never counted against the frame.
    scene = (meta.get('scene') or {}) if files.meta else {}
    if scene.get('native') and scene.get('guest'):
        root = Path(files.stem).parent
        try:
            s_native = images_module.read_image(root / scene['native'])
            s_guest = images_module.read_image(root / scene['guest'])
            report['scene_image'] = diff_images(s_native, s_guest, settings.pixel_tolerance).to_json()
        except (images_module.ImageError, OSError, InputError) as error:
            report['scene_image'] = {'error': str(error)}
    elif scene.get('error'):
        report['scene_image'] = {'error': scene['error']}
    # Draw lists.
    _, guest_draws = load_draws(files.guest_draws)
    _, native_draws = load_draws(files.native_draws)
    draws = diff_draws(guest_draws, native_draws, settings)
    remaining = apply_allow(draws.differences, rules)
    unallowed += len(remaining)
    stamped = all('decoded' in d for d in guest_draws + native_draws if d.get('pipeline'))
    report['draws'] = {'guest': draws.guest_draws, 'native': draws.native_draws, 'matched': draws.matched,
                       'counts': dict(Counter(d.kind for d in draws.differences)),
                       'unallowed': dict(Counter(d.kind for d in remaining)),
                       'pipeline_identity_differs': draws.pipeline_identity_differs,
                       'instance_streams': draws.instance_streams,
                       'state_recorded': stamped,
                       'classes': draws.classes, 'pass_order': draws.pass_order,
                       'groups': draws.groups(),
                       'differences': [d.to_json() for d in draws.differences]}
    report['unallowed'] = unallowed
    return report


def print_report(report: dict, limit: int) -> None:
    print(f'== {report["stem"]}')
    meta = report.get('meta')
    if meta:
        print(f'   frame={meta.get("frame")} views={meta.get("views")} tick_frame={meta.get("tick_frame")} '
              f'motion={meta.get("motion")} held={meta.get("held")} restored_words={meta.get("restored_words")}'
              f' samplers_restored={meta.get("samplers_restored")}')
    if report.get('guest_error'):
        print(f'   guest side failed: {report["guest_error"]}')
    image = report['image']
    if 'error' in image:
        print(f'   image: {image["error"]}')
    else:
        verdict = 'ok' if image['pass'] else 'DIFFERENT'
        print(f'   image {image["width"]}x{image["height"]}: exact_differing={image["exact_differing"]} '
              f'over_tolerance({image["tolerance"]})={image["over_tolerance"]} max_channel_diff={image["max_channel_diff"]} '
              f'bbox={image["bbox"]} signed_mean={image.get("signed_mean")} histogram={image.get("histogram")} -> {verdict}')
    scene = report.get('scene_image')
    if scene:
        if 'error' in scene:
            print(f'   pre-post scene: {scene["error"]}')
        else:
            print(f'   pre-post scene (HDR clamped): exact_differing={scene["exact_differing"]} '
                  f'max_channel_diff={scene["max_channel_diff"]} bbox={scene["bbox"]} signed_mean={scene["signed_mean"]} '
                  f'histogram={scene["histogram"]}')
    draws = report['draws']
    print(f'   draws guest={draws["guest"]} native={draws["native"]} matched={draws["matched"]} '
          f'differences={draws["counts"] or "{}"} unallowed={draws["unallowed"] or "{}"}')
    if not draws['state_recorded']:
        print(f'   pipeline state not recorded in this capture: {draws["pipeline_identity_differs"]} paired draws have '
              f'different raw pipeline identities (not compared; --strict-pipeline compares them)')
    if draws.get('instance_streams'):
        print(f'   instance streams (expanded instances checked against the paired worlds): {draws["instance_streams"]}')
    print(f'   classes (guest callback -> native pass): {draws["classes"]}')
    order = draws['pass_order']
    print(f'   pass order: guest {[f"{k}x{n}" for k, n in order["guest"]]}')
    print(f'               native {[f"{k}x{n}" for k, n in order["native"]]}')
    groups = draws['groups']
    for title, key in (('by material (vs:ps)', 'material'), ('by class (label)', 'label'), ('by native pass', 'pass')):
        if groups[key]:
            print(f'   {title}:')
            for name, counts in list(groups[key].items())[:limit]:
                print(f'     {name} {counts}')
    if groups['field']:
        print(f'   differing fields: {groups["field"]}')
    shown = 0
    for difference in draws['differences']:
        if difference['allowed_by'] or shown >= limit:
            continue
        shown += 1
        detail = ''.join(f' [{k}: {v["registers"]} max_abs={v["max_abs"]:.3g}]' for k, v in difference.get('details', {}).items())
        print(f'     {difference["kind"]:9} guest#{difference["guest"]} native#{difference["native"]} '
              f'{difference["material"]} {difference["label"]} -> {difference["pass"]} {",".join(difference["fields"])}{detail}')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('frames', nargs='+', type=Path,
                        help='a shadow frame (<prefix>.F.shadow.json, any of its files, or the <prefix>.F stem) or a directory of them')
    parser.add_argument('--pixel-tolerance', type=int, default=None, help='largest channel difference a pixel may have (default 0: exact)')
    parser.add_argument('--max-pixels', type=int, default=None, help='pixels allowed over the tolerance (default 0)')
    parser.add_argument('--float-tolerance', type=float, default=0.0,
                        help='relative/absolute tolerance for world matrices, viewports and blend factors (default 0: exact)')
    parser.add_argument('--no-expand-instances', action='store_true', help='compare instanced draws as recorded')
    parser.add_argument('--strict-geometry', action='store_true',
                        help='also compare vertex/index buffer identities, transient vertex hashes, first index and base vertex')
    parser.add_argument('--strict-pipeline', action='store_true',
                        help='compare raw pipeline identities where the capture has no decoded state')
    parser.add_argument('--ignore-order', action='store_true', help='do not report draws that match only out of order')
    parser.add_argument('--strict-order', action='store_true',
                        help='check the order across the whole list, not within each native pass')
    parser.add_argument('--allow', type=Path, help='allow list (JSON; see the module docstring)')
    parser.add_argument('--json', type=Path, help='write the full report here')
    parser.add_argument('--diff-mask', action='store_true', help='write <stem>.diff.png (white = over tolerance)')
    parser.add_argument('--limit', type=int, default=20, help='differences and groups printed per frame')
    args = parser.parse_args(argv)
    try:
        pixels, rules = load_allow(args.allow)
        settings = Settings(
            pixel_tolerance=args.pixel_tolerance if args.pixel_tolerance is not None else int(pixels.get('tolerance', 0)),
            max_pixels=args.max_pixels if args.max_pixels is not None else int(pixels.get('max_pixels', 0)),
            float_tolerance=args.float_tolerance, expand_instances=not args.no_expand_instances,
            strict_geometry=args.strict_geometry, check_order=not args.ignore_order, strict_order=args.strict_order,
            strict_pipeline=args.strict_pipeline)
        images_module = _load_images()
        reports = []
        for path in args.frames:
            for files in find_frames(path):
                mask = Path(f'{files.stem}.diff.png') if args.diff_mask else None
                reports.append(compare_frame(files, settings, rules, images_module, mask))
    except (InputError, OSError, json.JSONDecodeError) as error:
        print(f'shadow-diff: {error}', file=sys.stderr)
        return 2
    for report in reports:
        print_report(report, args.limit)
    unallowed = sum(r['unallowed'] for r in reports)
    unused = [rule.name() for rule in rules if not rule.used]
    if unused:
        print(f'allow list rules that matched nothing: {unused}')
    if rules:
        print('allow list use: ' + ', '.join(f'{rule.name()!r}={rule.used}' for rule in rules))
    print(f'{len(reports)} frame(s), {unallowed} difference(s) beyond the allow list')
    if args.json:
        args.json.write_text(json.dumps({'settings': settings.__dict__, 'frames': reports, 'unallowed': unallowed,
                                         'unused_rules': unused,
                                         'rule_use': {rule.name(): rule.used for rule in rules}}, indent=2), encoding='utf-8')
    return 1 if unallowed else 0


if __name__ == '__main__':
    sys.exit(main())
