"""Tests for tools/shadow-diff.py with synthetic shadow frames."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
_spec = importlib.util.spec_from_file_location('shadow_diff', TOOLS / 'shadow-diff.py')
sd = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = sd  # dataclasses resolve annotations through sys.modules
_spec.loader.exec_module(sd)

# One line exactly as tests/native_shadow_render_tests.cpp expects SerializeNativeDrawRecord to write it.
CPP_LINE = (
    '{"i":4,"label":"guest.world:820077d8 \\"q\\"","pipeline":"0000000000001234","vs":"000000000000000a",'
    '"ps":"000000000000000b","layout":"000000000000000c","topology":1,"kind":"indexed","count":36,"first":6,'
    '"base":-2,"instances":1,"first_instance":0,"textures":[{"stage":1,"slot":2,"id":"0000000000000abc","w":64,"h":32}],'
    '"constants":[{"stage":0,"slot":1,"bytes":96,"hash":"000000000000feed"}],"constant_hash":"0000000000000099",'
    '"world":[1.5,0,0,0,0,"nan",0,0,0,0,"inf",0,0,0,0,0],"targets":["0000000000000010"],"depth":"0000000000000020",'
    '"viewport":[0,0,1280,720,0,1],"blend":null,"geometry":"0=b:1+0/32"}')


def bmp(pixels, width, height):
    """pixels: top-down list of (r, g, b); 24-bit bottom-up BMP as the game writes."""
    import struct
    pitch = (width * 3 + 3) & ~3
    body = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray()
        for r, g, b in pixels[y * width:(y + 1) * width]:
            row += bytes((b, g, r))
        body += row + b'\0' * (pitch - len(row))
    header = struct.pack('<2sIHHI', b'BM', 54 + len(body), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(body), 0, 0, 0, 0)
    return header + info + bytes(body)


def draw(i, label='native.models', pipeline='00000000000000p1', vs=1, ps=2, count=36, kind='indexed', instances=1,
         textures=None, constants=None, world=None, targets=('t0',), depth='d0', viewport=(0, 0, 4, 2, 0, 1)):
    return {'i': i, 'label': label, 'pipeline': pipeline, 'vs': f'{vs:016x}', 'ps': f'{ps:016x}', 'layout': '0' * 16,
            'topology': 0, 'kind': kind, 'count': count, 'first': 0, 'base': 0, 'instances': instances,
            'first_instance': 0,
            'textures': textures if textures is not None else [{'stage': 1, 'slot': 0, 'id': 'tex-a', 'w': 64, 'h': 64}],
            'constants': constants if constants is not None else [{'stage': 0, 'slot': 0, 'bytes': 64, 'hash': 'c1'}],
            'constant_hash': 'x', 'world': world, 'targets': list(targets), 'depth': depth, 'viewport': list(viewport),
            'blend': None, 'geometry': ''}


class Frame:
    """A shadow frame on disk: <dir>/shadow.<F>.*"""

    def __init__(self, root: Path, frame=7):
        self.stem = root / f'shadow.{frame}'
        self.frame = frame
        self.native_pixels = [(10, 20, 30)] * 8
        self.guest_pixels = [(10, 20, 30)] * 8
        self.native = [draw(0, 'native.sky', vs=3, ps=4), draw(1), draw(2, 'native.post', vs=5, ps=6, count=3, kind='draw')]
        self.guest = [draw(0, 'guest.view_begin', vs=3, ps=4, targets=('g0',), depth='gd'),
                      draw(1, 'guest.world:82001234', targets=('g0',), depth='gd'),
                      draw(2, 'guest.finish', vs=5, ps=6, count=3, kind='draw', targets=('g0',), depth='gd')]
        self.meta = {'format': 'edf-shadow-frame', 'version': 1, 'frame': frame, 'views': 1, 'tick_frame': True,
                     'motion': {'tick': 1}, 'held': {'tone': 1, 'lifetime': 0}, 'restored_words': 3,
                     'guest': {'error': None}}

    def write(self):
        Path(f'{self.stem}.native.bmp').write_bytes(bmp(self.native_pixels, 4, 2))
        Path(f'{self.stem}.guest.bmp').write_bytes(bmp(self.guest_pixels, 4, 2))
        for side, draws in (('native', self.native), ('guest', self.guest)):
            header = {'format': 'edf-shadow-draws', 'version': 1, 'side': side, 'frame': self.frame, 'draws': len(draws)}
            text = json.dumps(header) + '\n' + ''.join(json.dumps(d) + '\n' for d in draws)
            Path(f'{self.stem}.{side}.draws.jsonl').write_text(text, encoding='utf-8')
        Path(f'{self.stem}.shadow.json').write_text(json.dumps(self.meta), encoding='utf-8')
        return Path(f'{self.stem}.shadow.json')


def run(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = sd.main([str(a) for a in args])
    return code, out.getvalue()


class ShadowDiffTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        self.frame = Frame(self.root)

    def tearDown(self):
        self._dir.cleanup()

    def report(self, *extra):
        path = self.frame.write()
        out = self.root / 'report.json'
        code, text = run(path, '--json', out, *extra)
        return code, json.loads(out.read_text(encoding='utf-8')), text

    def test_identical_frame_passes(self):
        code, report, text = self.report()
        self.assertEqual(code, 0, text)
        frame = report['frames'][0]
        self.assertEqual(frame['image']['exact_differing'], 0)
        self.assertEqual(frame['draws']['matched'], 3)
        self.assertEqual(frame['draws']['differences'], [])
        self.assertIn('0 difference(s)', text)

    def test_pixels_exact_and_tolerance(self):
        self.frame.guest_pixels = [(10, 20, 30)] * 7 + [(11, 20, 30)]
        code, report, _ = self.report()
        self.assertEqual(code, 1)
        image = report['frames'][0]['image']
        self.assertEqual((image['exact_differing'], image['over_tolerance'], image['max_channel_diff']), (1, 1, 1))
        self.assertEqual(image['bbox'], [3, 1, 3, 1])
        code, report, _ = self.report('--pixel-tolerance', '1')
        self.assertEqual(code, 0)
        self.assertEqual(report['frames'][0]['image']['exact_differing'], 1)  # Still reported exactly.
        self.frame.guest_pixels = [(10, 20, 30)] * 6 + [(14, 20, 30)] * 2
        code, _, _ = self.report('--pixel-tolerance', '1', '--max-pixels', '2')
        self.assertEqual(code, 0)
        code, _, _ = self.report('--pixel-tolerance', '1', '--max-pixels', '1')
        self.assertEqual(code, 1)

    def test_diff_mask(self):
        self.frame.guest_pixels = [(0, 0, 0)] + [(10, 20, 30)] * 7
        code, _, _ = self.report('--diff-mask')
        self.assertEqual(code, 1)
        self.assertTrue(Path(f'{self.frame.stem}.diff.png').read_bytes().startswith(b'\x89PNG'))

    def test_missing_and_extra_grouped(self):
        self.frame.guest.insert(2, draw(9, 'guest.overlays', vs=7, ps=8, count=6, targets=('g0',), depth='gd'))
        self.frame.native.append(draw(3, 'native.transparent', vs=9, ps=10, count=12))
        code, report, text = self.report()
        self.assertEqual(code, 1)
        draws = report['frames'][0]['draws']
        self.assertEqual(draws['counts'], {'missing': 1, 'extra': 1})
        missing = [d for d in draws['differences'] if d['kind'] == 'missing'][0]
        self.assertEqual((missing['guest'], missing['label'], missing['material']),
                         (9, 'guest.overlays', f'{7:016x}:{8:016x}'))
        extra = [d for d in draws['differences'] if d['kind'] == 'extra'][0]
        self.assertEqual((extra['native'], extra['label']), (3, 'native.transparent'))
        self.assertEqual(draws['groups']['material'][f'{7:016x}:{8:016x}'], {'missing': 1})
        self.assertEqual(draws['groups']['label']['native.transparent'], {'extra': 1})
        self.assertIn('by material', text)

    def test_differing_fields(self):
        self.frame.native[1]['constants'] = [{'stage': 0, 'slot': 0, 'bytes': 64, 'hash': 'c2'},
                                             {'stage': 1, 'slot': 3, 'bytes': 16, 'hash': 'p'}]
        self.frame.native[1]['world'] = [1.0] * 16
        self.frame.guest[1]['world'] = [1.0] * 15 + [1.0000001]
        code, report, _ = self.report()
        self.assertEqual(code, 1)
        differing = report['frames'][0]['draws']['differences']
        self.assertEqual(len(differing), 1)
        self.assertEqual(differing[0]['kind'], 'differing')
        self.assertEqual(differing[0]['fields'], ['constants.vs0', 'constants.ps3', 'world'])
        # The float tolerance mode excuses the matrix, not the constants.
        _, report, _ = self.report('--float-tolerance', '1e-5')
        self.assertEqual(report['frames'][0]['draws']['differences'][0]['fields'], ['constants.vs0', 'constants.ps3'])

    def test_targets_and_textures_correspond_one_to_one(self):
        # Guest draws into its own targets and samples its own resolve: fine
        # while the mapping stays one-to-one.
        for guest in self.frame.guest:
            guest['textures'] = [{'stage': 1, 'slot': 0, 'id': 'guest-scene', 'w': 64, 'h': 64}]
        code, _, text = self.report()
        self.assertEqual(code, 0, text)
        # One native draw samples a different texture than the others: broken mapping.
        self.frame.native[2]['textures'] = [{'stage': 1, 'slot': 0, 'id': 'tex-b', 'w': 64, 'h': 64}]
        code, report, _ = self.report()
        self.assertEqual(code, 1)
        self.assertEqual(report['frames'][0]['draws']['differences'][0]['fields'], ['texture.1.0'])
        # And a target that splits.
        self.frame.native[2]['textures'] = copy.deepcopy(self.frame.native[1]['textures'])
        self.frame.native[2]['targets'] = ['t1']
        code, report, _ = self.report()
        self.assertEqual(report['frames'][0]['draws']['differences'][0]['fields'], ['targets'])

    def test_reordered(self):
        self.frame.native[0], self.frame.native[1] = self.frame.native[1], self.frame.native[0]
        code, report, _ = self.report()
        self.assertEqual(code, 1)
        self.assertEqual(report['frames'][0]['draws']['counts'], {'reordered': 1})
        code, _, _ = self.report('--ignore-order')
        self.assertEqual(code, 0)

    def test_instanced_draw_expands(self):
        # The guest draws a mesh three times, the native pass instances it.
        guest_mesh = [draw(i, 'guest.buckets', targets=('g0',), depth='gd', world=[float(i)] * 16) for i in range(3)]
        self.frame.guest = [self.frame.guest[0]] + guest_mesh + [self.frame.guest[2]]
        self.frame.native[1] = draw(1, kind='instanced', instances=3)
        code, report, text = self.report()
        self.assertEqual(code, 0, text)
        self.assertEqual(report['frames'][0]['draws']['guest'], 5)
        code, _, _ = self.report('--no-expand-instances')
        self.assertEqual(code, 1)

    def test_geometry_only_when_strict(self):
        self.frame.native[1]['geometry'] = '0=b:aaaa+0/32'
        self.frame.guest[1]['geometry'] = '0=b:bbbb+0/32'
        self.assertEqual(self.report()[0], 0)
        code, report, _ = self.report('--strict-geometry')
        self.assertEqual(code, 1)
        self.assertEqual(report['frames'][0]['draws']['differences'][0]['fields'], ['geometry'])

    def test_allow_list(self):
        self.frame.guest.insert(2, draw(9, 'guest.overlays', vs=7, ps=8, count=6, targets=('g0',), depth='gd'))
        self.frame.guest.insert(2, draw(10, 'guest.overlays', vs=7, ps=8, count=9, targets=('g0',), depth='gd'))
        self.frame.native[1]['constants'] = [{'stage': 0, 'slot': 0, 'bytes': 64, 'hash': 'c2'}]
        self.frame.guest_pixels = [(10, 20, 30)] * 7 + [(12, 20, 30)]
        allow = self.root / 'allow.json'
        allow.write_text(json.dumps({
            'pixels': {'tolerance': 2},
            'draws': [{'kind': 'missing', 'label': 'guest.overlays', 'max': 1, 'reason': 'one overlay'},
                      {'kind': 'differing', 'material': f'{1:016x}:*', 'field': 'constants.vs*', 'reason': 'vs constants'},
                      {'kind': 'extra', 'reason': 'never used'}]}), encoding='utf-8')
        code, report, text = self.report('--allow', allow)
        self.assertEqual(code, 1, text)  # The second overlay exceeds the rule's max.
        frame = report['frames'][0]
        self.assertTrue(frame['image']['pass'])
        self.assertEqual(frame['draws']['unallowed'], {'missing': 1})
        self.assertEqual(report['unused_rules'], ['never used'])
        allowed = [d['allowed_by'] for d in frame['draws']['differences'] if d['allowed_by']]
        self.assertEqual(sorted(allowed), ['one overlay', 'vs constants'])
        # A field glob that does not cover every differing field does not excuse it.
        allow.write_text(json.dumps({'draws': [{'kind': 'differing', 'field': 'world'}]}), encoding='utf-8')
        self.frame.guest_pixels = self.frame.native_pixels
        self.frame.guest = [g for g in self.frame.guest if g['label'] != 'guest.overlays']
        code, _, _ = self.report('--allow', allow)
        self.assertEqual(code, 1)

    def test_guest_error_and_missing_image(self):
        self.frame.meta['guest'] = {'error': 'the guest post left no valid output'}
        path = self.frame.write()
        Path(f'{self.frame.stem}.guest.bmp').unlink()
        code, text = run(path)
        self.assertEqual(code, 1)
        self.assertIn('guest side failed', text)
        self.assertIn('missing image', text)

    def test_directory_and_stem_inputs(self):
        self.frame.write()
        second = Frame(self.root, frame=19)
        second.native.pop()
        second.write()
        code, text = run(self.root)
        self.assertEqual(code, 1)
        self.assertIn('2 frame(s)', text)
        code, text = run(self.frame.stem)  # The bare stem.
        self.assertEqual(code, 0, text)
        code, text = run(f'{self.frame.stem}.native.bmp')
        self.assertEqual(code, 0, text)

    def test_bad_input(self):
        path = self.frame.write()
        Path(f'{self.frame.stem}.native.draws.jsonl').write_text('{"format":"other"}\n', encoding='utf-8')
        code, text = run(path)
        self.assertEqual(code, 2)
        self.assertIn('not a shadow draw list', text)
        code, _ = run(self.root / 'nothing-here')
        self.assertEqual(code, 2)
        empty = self.root / 'empty'
        empty.mkdir()
        self.assertEqual(run(empty)[0], 2)

    def test_reads_the_cpp_serialization(self):
        path = self.root / 'cpp.native.draws.jsonl'
        header = '{"format":"edf-shadow-draws","version":1,"side":"native","frame":12,"draws":1}'
        path.write_text(header + '\n' + CPP_LINE + '\n', encoding='utf-8')
        header, draws = sd.load_draws(path)
        self.assertEqual((header['side'], header['frame'], len(draws)), ('native', 12, 1))
        d = draws[0]
        self.assertEqual(d['label'], 'guest.world:820077d8 "q"')
        self.assertEqual(sd.material(d), f'{0xa:016x}:{0xb:016x}')
        self.assertEqual(d['world'][5], 'nan')
        # Compared with itself it matches, NaN included (as the same string).
        result = sd.diff_draws(draws, copy.deepcopy(draws), sd.Settings())
        self.assertEqual((result.matched, result.differences), (1, []))

    def test_world_instanced_vertex_id_is_one_material(self):
        base = draw(0)
        instanced = dict(base, vs=f'{(1 << 63) | 1:016x}')
        self.assertEqual(sd.material(base), sd.material(instanced))


if __name__ == '__main__':
    unittest.main()
