import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from renderer_scalar_analysis import (MANIFEST, analyze, classify, decode, emit_cpp,
                                      generated_body, load_manifest, validate_ghidra, validate_sources)


class ScalarRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = load_manifest()
        cls.masked = next(r for r in cls.manifest['functions'] if r['function'] == 'sub_82134ED8')

    def mutate(self, index, transform):
        record = copy.deepcopy(self.masked)
        record['instructions'][index]['word'] = f"{transform(int(record['instructions'][index]['word'], 16)):08X}"
        return classify(record)

    def test_entire_corpus_accounted_for(self):
        result = analyze(self.manifest)
        self.assertEqual(len(result), 170)
        self.assertEqual(sum(len(r['facts']) for r in result), 1443)
        self.assertEqual(sum(r.get('rule') == 'masked-load-return-v1' for r in result), 40)
        self.assertEqual(sum(r.get('rule') == 'load-return-v1' for r in result), 19)
        self.assertEqual(sum(r['status'] == 'unsupported' for r in result), 111)
        self.assertEqual(sum(r['partition'] == 'holdout' and 'rule' in r for r in result), 13)

    def test_wrong_load_width_base_and_register_rejected(self):
        for transform in (lambda w: (w & 0x03ffffff) | (34 << 26),
                          lambda w: (w & 0x03ffffff) | (40 << 26),
                          lambda w: (w & 0x03ffffff) | (58 << 26),
                          lambda w: w & ~(31 << 16),
                          lambda w: (w & ~(31 << 16)) | (4 << 16),
                          lambda w: (w & ~(31 << 21)) | (12 << 21),
                          lambda w: w | 1):
            with self.subTest(transform=transform):
                self.assertEqual(self.mutate(0, transform)['status'], 'unsupported')

    def test_condition_update_and_broken_dataflow_rejected(self):
        for transform in (lambda w: w | 1, lambda w: w ^ (1 << 21), lambda w: w ^ (1 << 16)):
            self.assertEqual(self.mutate(1, transform)['status'], 'unsupported')

    def test_return_variants_and_calls_rejected(self):
        for word in (0x4e800021, 0x4d820020, 0x4e800420, 0x48000001, 0x60000000):
            self.assertEqual(self.mutate(2, lambda w: word)['status'], 'unsupported')

    def test_offset_and_mask_change_contract(self):
        original = classify(self.masked)['contract']
        self.assertNotEqual(self.mutate(0, lambda w: w + 4)['contract'], original)
        self.assertNotEqual(self.mutate(1, lambda w: w ^ (1 << 6))['contract'], original)
        self.assertNotEqual(self.mutate(1, lambda w: w ^ (1 << 11))['contract'], original)

    def test_added_store_and_incomplete_body_rejected(self):
        record = copy.deepcopy(self.masked)
        record['instructions'].insert(2, dict(address='82134EE0', word='90640000'))
        self.assertEqual(classify(record)['status'], 'unsupported')
        record = copy.deepcopy(self.masked)
        record['instructions'][1]['address'] = '82134EE4'
        self.assertEqual(classify(record)['status'], 'unsupported')
        record['instructions'] = record['instructions'][:-1]
        self.assertEqual(classify(record)['status'], 'unsupported')

    def test_signed_displacement_and_wrap_mask_fields(self):
        self.assertEqual(decode(0x8063fffc)['displacement'], -4)
        word = (21 << 26) | (11 << 21) | (3 << 16) | (7 << 11) | (29 << 6) | (3 << 1)
        self.assertEqual(decode(word)['mask_begin'], 29)
        self.assertEqual(decode(word)['mask_end'], 3)

    def test_original_body_freshness_and_exact_extraction(self):
        sources = validate_sources(self.manifest)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'fixture.inc'
            self.assertEqual(emit_cpp(self.manifest, path), 59)
            self.assertIn(generated_body(sources[self.masked['source']], self.masked['function']), path.read_text())
        changed = copy.deepcopy(self.manifest)
        changed['functions'][0]['body_sha256'] = 'changed'
        with self.assertRaisesRegex(ValueError, 'Changed original body'):
            validate_sources(changed)

    def test_independent_ghidra_boundary_disagreement_rejected(self):
        record = self.masked
        fake = dict(language='PowerPC:BE:64:64-32addr', functions=[dict(function=record['function'],
            instructions=[dict(address=i['address'], bytes=i['word']) for i in record['instructions']])])
        manifest = dict(function_count=1, functions=[record])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'facts.json'
            path.write_text(json.dumps(fake))
            self.assertEqual(len(validate_ghidra(manifest, path)), 1)
            fake['functions'][0]['instructions'].append(dict(address='82134EE4', bytes='60000000'))
            path.write_text(json.dumps(fake))
            with self.assertRaisesRegex(ValueError, 'accepted getter'):
                validate_ghidra(manifest, path)
            fake['functions'][0]['instructions'].pop()
            fake['functions'][0]['instructions'].pop()
            path.write_text(json.dumps(fake))
            with self.assertRaisesRegex(ValueError, 'body mismatch'):
                validate_ghidra(manifest, path)

    def test_unsupported_shared_tail_is_preserved_and_quarantined(self):
        record = next(r for r in self.manifest['functions'] if r['function'] == 'sub_82137978')
        fake = dict(language='PowerPC:BE:64:64-32addr', functions=[dict(function=record['function'],
            decompile_completed=True, instructions=[dict(address=i['address'], bytes=i['word'])
            for i in record['instructions']] + [dict(address='821370E0', bytes='60000000')])])
        manifest = dict(function_count=1, functions=[record])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'facts.json'
            path.write_text(json.dumps(fake))
            result = analyze(manifest, validate_ghidra(manifest, path))[0]
            self.assertEqual(result['status'], 'unsupported')
            self.assertFalse(result['boundary_matches_inventory'])
            self.assertEqual(result['additional_body_addresses'], ['821370E0'])


if __name__ == '__main__':
    unittest.main()
