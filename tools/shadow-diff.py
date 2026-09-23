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
  --max-pixels is how many pixels may exceed the tolerance.
- Draw lists: each draw's structural key (pipeline identity, shaders, topology, counts,
  texture slots and sizes; instanced draws expanded to one per instance) aligns
  the guest list against the native one in order (difflib); unaligned draws
  with the same key are paired as "reordered"; the rest are "missing" (guest
  only) or "extra" (native only). Paired draws are compared field by field:
  constants (per stage/slot hash), world matrix (--float-tolerance), viewport,
  blend factor, and texture and target identities, which must correspond
  one-to-one between the sides (the shadow's guest side draws into its own
  targets and samples its own scene resolve, so raw identities differ).
  Geometry (buffer identities, transient vertex hashes) is compared with
  --strict-geometry only: native passes keep their own vertex buffers. An
  expanded instanced draw has no world to compare (its worlds are in the
  instance stream); its constants are still compared as recorded.
- Differences are grouped by material (vertex:pixel shader ids) and by class
  (the recording label: native pass or guest callback).

An allow list (--allow FILE, JSON) excuses known differences:

  {"pixels": {"tolerance": 2, "max_pixels": 64},
   "draws": [{"kind": "missing", "material": "*:00000000000000ab", "label": "guest.overlays",
              "field": "*", "max": 10, "reason": "..."}]}

A draw difference is allowed when a rule matches its kind ("missing", "extra",
"differing", "reordered" or "*"), material and label (fnmatch globs, default
"*"), and for "differing" the field; "max" caps how many a rule excuses.
Exit 0 when nothing is left beyond the allow list, 1 when something is, 2 on
unreadable input. Standard library only.
"""
from __future__ import annotations

import argparse
import difflib
import fnmatch
import importlib.util
import json
import math
import sys
from collections import Counter, defaultdict
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

    def to_json(self) -> dict:
        return {'width': self.width, 'height': self.height, 'exact_differing': self.exact,
                'over_tolerance': self.over, 'max_channel_diff': self.max_diff, 'tolerance': self.tolerance,
                'bbox': list(self.bbox) if self.bbox else None}


def diff_images(native, guest, tolerance: int) -> ImageDiff:
    if (native.width, native.height) != (guest.width, guest.height):
        raise InputError(f'image size mismatch: native {native.width}x{native.height}, guest {guest.width}x{guest.height}')
    w, h = native.width, native.height
    a, b = native.rgb, guest.rgb
    mask = bytearray(w * h)
    exact = over = max_diff = 0
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
            m = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
            if not m:
                continue
            exact += 1
            max_diff = max(max_diff, m)
            if m > tolerance:
                over += 1
                mask[y * w + x] = 255
                x0 = x if x0 is None else min(x0, x)
                y0 = y if y0 is None else min(y0, y)
                x1 = max(x1, x)
                y1 = max(y1, y)
    bbox = (x0, y0, x1, y1) if x0 is not None else None
    return ImageDiff(w, h, exact, over, max_diff, tolerance, bbox, bytes(mask))


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


def expand(draws: list[dict], expand_instances: bool) -> list[dict]:
    """Instanced draws as one draw per instance (a native pass may instance
    what the guest draws one by one); indexed kinds become one kind."""
    out = []
    for draw in draws:
        count = int(draw.get('instances', 1)) if expand_instances else 1
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
                copy['_instance'] = instance
            out.append(copy)
    return out


def structural_key(draw: dict) -> tuple:
    textures = tuple(sorted((t['stage'], t['slot'], t['w'], t['h']) for t in draw.get('textures', [])))
    return (draw.get('pipeline'), material(draw), draw.get('topology'), draw.get('kind'), draw.get('count'), draw.get('first'),
            draw.get('base'), draw.get('instances', 1), textures)


class Correspondence:
    """Identities that must map one-to-one between the sides (targets, textures)."""

    def __init__(self):
        self.forward: dict[str, str] = {}
        self.backward: dict[str, str] = {}

    def check(self, guest: str, native: str) -> bool:
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


def compare_fields(guest: dict, native: dict, settings: 'Settings', textures: Correspondence,
                   targets: Correspondence) -> list[str]:
    fields = []
    g_constants = {(c['stage'], c['slot']): c for c in guest.get('constants', [])}
    n_constants = {(c['stage'], c['slot']): c for c in native.get('constants', [])}
    for slot in sorted(set(g_constants) | set(n_constants)):
        g, n = g_constants.get(slot), n_constants.get(slot)
        if g is None or n is None or g['hash'] != n['hash'] or g['bytes'] != n['bytes']:
            fields.append(f'constants.{"vs" if slot[0] == 0 else "ps" if slot[0] == 1 else "cs"}{slot[1]}')
    if not (guest.get('_world_unknown') or native.get('_world_unknown')) and             not _floats_equal(guest.get('world'), native.get('world'), settings.float_tolerance):
        fields.append('world')
    if not _floats_equal(guest.get('viewport'), native.get('viewport'), settings.float_tolerance):
        fields.append('viewport')
    if not _floats_equal(guest.get('blend'), native.get('blend'), settings.float_tolerance):
        fields.append('blend')
    g_textures = {(t['stage'], t['slot']): t['id'] for t in guest.get('textures', [])}
    n_textures = {(t['stage'], t['slot']): t['id'] for t in native.get('textures', [])}
    for slot in sorted(set(g_textures) | set(n_textures)):
        if slot not in g_textures or slot not in n_textures or not textures.check(g_textures[slot], n_textures[slot]):
            fields.append(f'texture.{slot[0]}.{slot[1]}')
    g_targets = list(guest.get('targets', [])) + [guest.get('depth')]
    n_targets = list(native.get('targets', [])) + [native.get('depth')]
    if len(g_targets) != len(n_targets) or not all(targets.check(str(g), str(n)) for g, n in zip(g_targets, n_targets)):
        fields.append('targets')
    if settings.strict_geometry and guest.get('geometry') != native.get('geometry'):
        fields.append('geometry')
    return fields


@dataclass
class Difference:
    kind: str       # missing | extra | differing | reordered
    material: str
    label: str
    fields: list[str] = field(default_factory=list)
    guest_index: int | None = None
    native_index: int | None = None
    allowed_by: str | None = None

    def to_json(self) -> dict:
        return {'kind': self.kind, 'material': self.material, 'label': self.label, 'fields': self.fields,
                'guest': self.guest_index, 'native': self.native_index, 'allowed_by': self.allowed_by}


@dataclass
class Settings:
    pixel_tolerance: int = 0
    max_pixels: int = 0
    float_tolerance: float = 0.0
    expand_instances: bool = True
    strict_geometry: bool = False
    check_order: bool = True


@dataclass
class DrawDiff:
    guest_draws: int
    native_draws: int
    matched: int
    differences: list[Difference]

    def groups(self) -> dict:
        by_material: dict[str, Counter] = defaultdict(Counter)
        by_label: dict[str, Counter] = defaultdict(Counter)
        for difference in self.differences:
            by_material[difference.material][difference.kind] += 1
            by_label[difference.label][difference.kind] += 1
        return {'material': {k: dict(v) for k, v in sorted(by_material.items())},
                'label': {k: dict(v) for k, v in sorted(by_label.items())}}


def diff_draws(guest_draws: list[dict], native_draws: list[dict], settings: Settings) -> DrawDiff:
    guest = expand(guest_draws, settings.expand_instances)
    native = expand(native_draws, settings.expand_instances)
    g_keys = [structural_key(d) for d in guest]
    n_keys = [structural_key(d) for d in native]
    textures, targets = Correspondence(), Correspondence()
    differences: list[Difference] = []
    matched = 0
    pairs: list[tuple[int, int, bool]] = []  # guest index, native index, in order
    unmatched_guest: list[int] = []
    unmatched_native: list[int] = []
    matcher = difflib.SequenceMatcher(None, g_keys, n_keys, autojunk=False)
    for tag, g0, g1, n0, n1 in matcher.get_opcodes():
        if tag == 'equal':
            pairs.extend((g0 + k, n0 + k, True) for k in range(g1 - g0))
        else:
            unmatched_guest.extend(range(g0, g1))
            unmatched_native.extend(range(n0, n1))
    # Same structure, different position: paired in list order.
    waiting: dict[tuple, list[int]] = defaultdict(list)
    for n in unmatched_native:
        waiting[n_keys[n]].append(n)
    still_guest = []
    for g in unmatched_guest:
        queue = waiting.get(g_keys[g])
        if queue:
            pairs.append((g, queue.pop(0), False))
        else:
            still_guest.append(g)
    still_native = sorted(n for queue in waiting.values() for n in queue)
    for g, n, in_order in sorted(pairs):
        fields = compare_fields(guest[g], native[n], settings, textures, targets)
        label = guest[g].get('label', '')
        if fields:
            differences.append(Difference('differing', material(guest[g]), label, fields, guest[g].get('i'), native[n].get('i')))
        if not in_order and settings.check_order:
            differences.append(Difference('reordered', material(guest[g]), label, [], guest[g].get('i'), native[n].get('i')))
        if not fields and (in_order or not settings.check_order):
            matched += 1
    for g in still_guest:
        differences.append(Difference('missing', material(guest[g]), guest[g].get('label', ''), [], guest[g].get('i'), None))
    for n in still_native:
        differences.append(Difference('extra', material(native[n]), native[n].get('label', ''), [], None, native[n].get('i')))
    return DrawDiff(len(guest), len(native), matched, differences)


# ------------------------------------------------------------- allow list ---
@dataclass
class Rule:
    kind: str = '*'
    material: str = '*'
    label: str = '*'
    field: str = '*'
    max: int | None = None
    reason: str = ''
    used: int = 0

    def name(self) -> str:
        return self.reason or f'{self.kind} {self.material} {self.label} {self.field}'

    def matches(self, difference: Difference) -> bool:
        if self.max is not None and self.used >= self.max:
            return False
        if not (self.kind == '*' or self.kind == difference.kind):
            return False
        if not fnmatch.fnmatchcase(difference.material, self.material) or not fnmatch.fnmatchcase(difference.label, self.label):
            return False
        if difference.kind == 'differing' and self.field != '*':
            return all(fnmatch.fnmatchcase(f, self.field) for f in difference.fields)
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
        unknown = set(entry) - {'kind', 'material', 'label', 'field', 'max', 'reason'}
        if unknown:
            raise InputError(f'{path}: unknown rule keys {sorted(unknown)}')
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
        report['meta'] = {k: meta.get(k) for k in ('frame', 'views', 'motion', 'tick_frame', 'held', 'restored_words')}
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
    # Draw lists.
    _, guest_draws = load_draws(files.guest_draws)
    _, native_draws = load_draws(files.native_draws)
    draws = diff_draws(guest_draws, native_draws, settings)
    remaining = apply_allow(draws.differences, rules)
    unallowed += len(remaining)
    report['draws'] = {'guest': draws.guest_draws, 'native': draws.native_draws, 'matched': draws.matched,
                       'counts': dict(Counter(d.kind for d in draws.differences)),
                       'unallowed': dict(Counter(d.kind for d in remaining)),
                       'groups': draws.groups(),
                       'differences': [d.to_json() for d in draws.differences]}
    report['unallowed'] = unallowed
    return report


def print_report(report: dict, limit: int) -> None:
    print(f'== {report["stem"]}')
    meta = report.get('meta')
    if meta:
        print(f'   frame={meta.get("frame")} views={meta.get("views")} tick_frame={meta.get("tick_frame")} '
              f'motion={meta.get("motion")} held={meta.get("held")} restored_words={meta.get("restored_words")}')
    if report.get('guest_error'):
        print(f'   guest side failed: {report["guest_error"]}')
    image = report['image']
    if 'error' in image:
        print(f'   image: {image["error"]}')
    else:
        verdict = 'ok' if image['pass'] else 'DIFFERENT'
        print(f'   image {image["width"]}x{image["height"]}: exact_differing={image["exact_differing"]} '
              f'over_tolerance({image["tolerance"]})={image["over_tolerance"]} max_channel_diff={image["max_channel_diff"]} '
              f'bbox={image["bbox"]} -> {verdict}')
    draws = report['draws']
    print(f'   draws guest={draws["guest"]} native={draws["native"]} matched={draws["matched"]} '
          f'differences={draws["counts"] or "{}"} unallowed={draws["unallowed"] or "{}"}')
    groups = draws['groups']['material']
    if groups:
        print('   by material (vs:ps):')
        for name, counts in list(groups.items())[:limit]:
            print(f'     {name} {counts}')
    labels = draws['groups']['label']
    if labels:
        print('   by class (label):')
        for name, counts in list(labels.items())[:limit]:
            print(f'     {name} {counts}')
    shown = 0
    for difference in draws['differences']:
        if difference['allowed_by'] or shown >= limit:
            continue
        shown += 1
        print(f'     {difference["kind"]:9} guest#{difference["guest"]} native#{difference["native"]} '
              f'{difference["material"]} {difference["label"]} {",".join(difference["fields"])}')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('frames', nargs='+', type=Path,
                        help='a shadow frame (<prefix>.F.shadow.json, any of its files, or the <prefix>.F stem) or a directory of them')
    parser.add_argument('--pixel-tolerance', type=int, default=None, help='largest channel difference a pixel may have (default 0: exact)')
    parser.add_argument('--max-pixels', type=int, default=None, help='pixels allowed over the tolerance (default 0)')
    parser.add_argument('--float-tolerance', type=float, default=0.0,
                        help='relative/absolute tolerance for world matrices, viewports and blend factors (default 0: exact)')
    parser.add_argument('--no-expand-instances', action='store_true', help='compare instanced draws as recorded')
    parser.add_argument('--strict-geometry', action='store_true', help='also compare vertex/index buffer identities and transient vertex hashes')
    parser.add_argument('--ignore-order', action='store_true', help='do not report draws that match only out of order')
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
            strict_geometry=args.strict_geometry, check_order=not args.ignore_order)
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
    print(f'{len(reports)} frame(s), {unallowed} difference(s) beyond the allow list')
    if args.json:
        args.json.write_text(json.dumps({'settings': settings.__dict__, 'frames': reports, 'unallowed': unallowed,
                                         'unused_rules': unused}, indent=2), encoding='utf-8')
    return 1 if unallowed else 0


if __name__ == '__main__':
    sys.exit(main())
