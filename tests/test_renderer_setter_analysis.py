import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from renderer_scalar_analysis import validate_ghidra, validate_sources
from renderer_setter_analysis import (CANDIDATES, MANIFEST, analyze, classify, decode,
                                      emit_cpp, load_inputs, validate_candidates,
                                      validate_frozen_attestation, load_family_v2,
                                      validate_family_v2)


class SetterRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.frozen, cls.records = load_inputs()
        cls.by_name = {r['function']: r for r in cls.records}
        cls.masked = cls.by_name['sub_82134EB8']
        cls.byte = cls.by_name['sub_82135830']
        cls.direct = cls.by_name['sub_82135BE8']
        cls.family = cls.by_name['sub_82135D30']

    def mutate(self, record, index, transform):
        changed = copy.deepcopy(record)
        word = int(changed['instructions'][index]['word'], 16)
        changed['instructions'][index]['word'] = f'{transform(word) & 0xffffffff:08X}'
        return classify(changed)

    def test_pre_rule_candidate_freeze_and_complete_accounting(self):
        self.assertEqual(self.frozen['candidate_count'], 87)
        self.assertEqual(self.frozen['excluded_getter_count'], 59)
        self.assertEqual(len(self.records), 87)
        results = analyze(self.records)
        self.assertEqual(sum(r['status'] == 'unsupported' for r in results), 51)
        self.assertEqual(sum(r.get('rule') == 'masked-store-u32-dirty-v1' for r in results), 15)
        self.assertEqual(sum(r.get('rule') == 'direct-store-u8-dirty-v1' for r in results), 7)
        self.assertEqual(sum(r.get('rule') == 'direct-store-u32-v1' for r in results), 2)
        self.assertEqual(sum(r.get('rule') == 'packed-nibble-store-dirty-v2' for r in results), 12)
        self.assertEqual(sum(r['partition'] == 'holdout' and r['status'] == 'local_contract'
                             for r in results), 9)
        self.assertEqual(self.frozen['raw_pcode_store_count'], 96)
        self.assertEqual(self.frozen['raw_store_without_natural_store_count'], 9)

    def test_exact_width_endian_signed_offsets_and_ordered_alias_model(self):
        masked = classify(self.masked)['contract']
        self.assertEqual([(w['width'], w['endian']) for w in masked['writes_ordered']],
                         [(32, 'big'), (64, 'big')])
        self.assertEqual(masked['value_source_register'], masked['merge_destination'])
        self.assertIn(masked['merge_source'], (4, 11))
        self.assertIn('listed order', masked['alias_model'])
        self.assertEqual(decode(0x9083fffc)['displacement'], -4)
        self.assertEqual(decode(0xF963fff8)['displacement'], -8)
        self.assertEqual(classify(self.byte)['contract']['value_width'], 8)

    def test_alternate_store_width_base_source_and_alignment_rejected(self):
        transforms = (lambda w: (w & 0x03ffffff) | (38 << 26),
                      lambda w: (w & 0x03ffffff) | (44 << 26),
                      lambda w: w ^ (1 << 16), lambda w: w ^ (1 << 21),
                      lambda w: w + 2)
        for transform in transforms:
            with self.subTest(transform=transform):
                self.assertEqual(self.mutate(self.direct, 0, transform)['status'], 'unsupported')

    def test_masked_dataflow_and_condition_effect_mutations_rejected(self):
        for transform in (lambda w: w | 1, lambda w: w ^ (1 << 16),
                          lambda w: w ^ (1 << 21)):
            self.assertEqual(self.mutate(self.masked, 1, transform)['status'], 'unsupported')
        self.assertEqual(self.mutate(self.masked, 0, lambda w: w ^ (1 << 21))['status'],
                         'unsupported')
        self.assertEqual(self.mutate(self.masked, 2, lambda w: w ^ (1 << 21))['status'],
                         'unsupported')

    def test_dirty_tail_register_width_offset_and_immediate_rejected(self):
        cases = ((3, lambda w: w ^ (1 << 21)), (3, lambda w: w ^ (1 << 16)),
                 (3, lambda w: (w & 0x03ffffff) | (32 << 26)),
                 (4, lambda w: w ^ (1 << 21)), (4, lambda w: w & 0xffff0000),
                 (5, lambda w: w ^ (1 << 21)), (5, lambda w: w + 4),
                 (5, lambda w: w | 1))
        for index, transform in cases:
            with self.subTest(index=index, transform=transform):
                self.assertEqual(self.mutate(self.masked, index, transform)['status'], 'unsupported')

    def test_calls_return_variants_extra_and_missing_instructions_rejected(self):
        for word in (0x4e800021, 0x4d820020, 0x48000001, 0x60000000):
            self.assertEqual(self.mutate(self.direct, 1, lambda unused, word=word: word)['status'],
                             'unsupported')
        changed = copy.deepcopy(self.direct)
        changed['instructions'].insert(1, dict(address='82135BEC', word='90640000'))
        self.assertEqual(classify(changed)['status'], 'unsupported')
        changed = copy.deepcopy(self.masked)
        changed['instructions'] = changed['instructions'][:-1]
        self.assertEqual(classify(changed)['status'], 'unsupported')
        changed = copy.deepcopy(self.masked)
        changed['instructions'][2]['address'] = '82134EC8'
        self.assertEqual(classify(changed)['status'], 'unsupported')

    def test_mask_and_offsets_change_contract_without_escaping_rule(self):
        original = classify(self.masked)['contract']
        self.assertNotEqual(self.mutate(self.masked, 1, lambda w: w ^ (1 << 6))['contract'], original)
        self.assertNotEqual(self.mutate(self.masked, 1, lambda w: w ^ (1 << 11))['contract'], original)
        self.assertNotEqual(self.mutate(self.masked, 4, lambda w: w ^ 1)['contract'], original)

    def test_source_and_natural_ghidra_freshness(self):
        validate_sources(self.manifest)
        validate_frozen_attestation(self.frozen, self.records)
        validate_family_v2(load_family_v2(), self.records)

    def test_family_exact_decode_contract_and_clobbers(self):
        result = classify(self.family)
        self.assertEqual(result['rule'], 'packed-nibble-store-dirty-v2')
        contract = result['contract']
        self.assertEqual(contract['changed_registers'], [10, 11, 12])
        self.assertEqual([(w['displacement'], w['width']) for w in contract['writes_ordered']],
                         [(10412, 32), (16, 64)])
        self.assertEqual((contract['first_shift'], contract['first_mb'], contract['first_me']),
                         (4, 0, 27))
        self.assertEqual((contract['second_shift'], contract['second_mb'], contract['second_me']),
                         (0, 28, 23))
        self.assertEqual((contract['li_immediate'], contract['dirty_shift'],
                          contract['dirty_mask_end'], contract['dirty_or_mask']),
                         (1, 45, 63, 1 << 45))
        self.assertEqual(decode('798C6FE6')['operation'], 'rotate_left_clear_right_u64')
        self.assertEqual(decode('798C6FE6')['shift'], 45)
        self.assertEqual(decode('798C6FE6')['mask_end'], 63)
        self.assertEqual(decode('7D4B5B78')['operation'], 'or_register')

    def test_family_opcode_dataflow_width_and_cr_mutations_rejected(self):
        cases = ((0, lambda w: w ^ (1 << 21)), (1, lambda w: w | 1),
                 (1, lambda w: w ^ (1 << 16)), (2, lambda w: w ^ (1 << 16)),
                 (2, lambda w: w + 1), (3, lambda w: w ^ (1 << 21)),
                 (4, lambda w: w | 1), (4, lambda w: w ^ (1 << 16)),
                 (4, lambda w: w ^ (1 << 11)),
                 (5, lambda w: w | 1), (5, lambda w: w ^ (1 << 11)),
                 (6, lambda w: (w & 0x03ffffff) | (38 << 26)),
                 (7, lambda w: (w & 0x03ffffff) | (32 << 26)),
                 (8, lambda w: w ^ (1 << 11)), (9, lambda w: w + 4))
        for index, transform in cases:
            with self.subTest(index=index):
                self.assertEqual(self.mutate(self.family, index, transform)['status'], 'unsupported')

    def test_emit_unchanged_bodies_and_exact_setter_schema(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'setter.inc'
            self.assertEqual(emit_cpp(self.records, output), 36)
            text = output.read_text()
            self.assertIn('DEFINE_REX_FUNC(sub_82134EB8)', text)
            self.assertIn('&__imp__sub_82134EB8, 1,', text)
            self.assertIn('&__imp__sub_82135830, 2,', text)
            self.assertIn('&__imp__sub_82135BE8, 3,', text)
            self.assertIn('&__imp__sub_82135D30, 4,', text)
            self.assertEqual(text.count('&__imp__'), 36)

    def test_manifest_tampering_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = copy.deepcopy(self.frozen)
            bad['functions'] = bad['functions'][:-1]
            path = Path(tmp) / 'bad.json'
            path.write_text(json.dumps(bad))
            with self.assertRaisesRegex(ValueError, 'Incomplete setter candidate freeze'):
                load_inputs(MANIFEST, path)


if __name__ == '__main__':
    unittest.main()
