import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import renderer_scalar_effects as effects
import renderer_scalar_analysis as frozen


class ScalarEffectEngineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = frozen.load_manifest()
        cls.boundaries = effects.load_boundaries()
        cls.by_name = {r["function"]: r for r in cls.manifest["functions"]}

    def synthetic(self, words, name="sub_82001000"):
        return {"function": name, "partition": "development", "body_sha256": "0" * 64,
                "instructions": [{"address": f"{int(name[4:],16)+4*i:08X}", "word": f"{w:08X}"}
                                 for i, w in enumerate(words)]}

    def boundary(self, record):
        return {"decompile_completed": True, "instructions": copy.deepcopy(record["instructions"])}

    def test_full_frozen_accounting_and_specific_reasons(self):
        result = effects.analyze(self.manifest, self.boundaries)
        self.assertEqual(len(result), 170)
        self.assertEqual(sum(r["status"] == "local_contract" for r in result) +
                         sum(r["status"] == "unsupported" for r in result), 170)
        for row in result:
            if row["status"] == "unsupported" and "opcode" in row["reason"]:
                self.assertRegex(row["reason"], r"^[0-9A-F]{8}: opcode [0-9A-F]{8}")

    def test_decode_supported_set_and_rejects_record_forms(self):
        self.assertEqual(effects.decode(0x8063FFFC)["displacement"], -4)
        self.assertEqual(effects.decode(0xE863FFF8)["displacement"], -8)
        for word, op in [(0x38600001, "addi"), (0x606B0040, "ori"),
                         (0x646B0040, "oris"), (0x7D6B5B78, "or")]:
            self.assertEqual(effects.decode(word)["operation"], op)
        self.assertTrue(effects.decode((21 << 26) | 1)["record_condition"])
        self.assertFalse(effects.decode(0x48000001)["supported"])

    def test_narrow_loads_signed_offsets_aliasing_and_receiver_guards(self):
        for opcode, width, displacement in [(34, 8, -1), (40, 16, -2)]:
            word = (opcode << 26) | (5 << 21) | (3 << 16) | (displacement & 0xffff)
            decoded = effects.decode(word)
            self.assertEqual((decoded['width'], decoded['displacement']), (width, displacement))
            record = self.synthetic([0x9083FFFC, word, 0x4E800020])
            row = effects.analyze_record(record, self.boundary(record))
            self.assertEqual(row['status'], 'local_contract')
            read = row['contract']['memory_reads_ordered'][0]
            self.assertEqual((read['width'], read['visible_prior_writes']), (width, 1))
            # A narrow load replacing r3 cannot precede another memory operation.
            receiver_load = (opcode << 26) | (3 << 21) | (3 << 16)
            record = self.synthetic([receiver_load, word, 0x4E800020])
            self.assertIn('receiver r3 changes', effects.analyze_record(record, self.boundary(record))['reason'])
            record = self.synthetic([(opcode << 26) | (5 << 21) | (4 << 16), 0x4E800020])
            self.assertIn('base is not receiver', effects.analyze_record(record, self.boundary(record))['reason'])
        record = self.synthetic([0xA0A30001, 0x4E800020])
        self.assertIn('unaligned', effects.analyze_record(record, self.boundary(record))['reason'])
        for opcode in (35, 41, 42, 43):  # update and signed forms remain unsupported
            self.assertFalse(effects.decode(opcode << 26)['supported'])

    def test_narrow_load_batch_has_exactly_thirteen_new_contracts(self):
        result = effects.analyze(self.manifest, self.boundaries)
        accepted = [r for r in result if r['status'] == 'local_contract']
        self.assertEqual(len(accepted), 127)
        self.assertEqual(sum(any(f['operation'] in ('lbz', 'lhz') for f in r['facts'])
                             for r in accepted), 13)

    def test_ssa_load_observes_prior_write_and_high_dest_preserved(self):
        # stw r4,0(r3); lwz r5,0(r3); rlwimi r5,r4,8,0,7; blr
        words = [0x90830000, 0x80A30000,
                 (20 << 26) | (4 << 21) | (5 << 16) | (8 << 11) | (0 << 6) | (7 << 1),
                 0x4E800020]
        record = self.synthetic(words)
        row = effects.analyze_record(record, self.boundary(record))
        self.assertEqual(row["status"], "local_contract")
        read = row["contract"]["memory_reads_ordered"][0]
        self.assertEqual(read["visible_prior_writes"], 1)
        expr = row["contract"]["register_expressions"]["5"]
        self.assertEqual(expr["op"], "rlwimi")
        self.assertIn("preserved_destination", expr)

    def test_ppc64_wrapping_rlwinm_mask_duplicates_rotated_word(self):
        expr = {"op": "rlwinm", "source": {"op": "constant", "value": 0x12345678, "width": 64},
                "shift": 0, "mask_begin": 28, "mask_end": 23, "width": 64}
        rendered = effects._cpp_expr(expr)
        self.assertIn("UINT64_C(0xFFFFFFFFFFFFFF0F)", rendered)
        self.assertIn("<< 32", rendered)
        word = (20 << 26) | (4 << 21) | (5 << 16) | (28 << 6) | (23 << 1)
        record = self.synthetic([word, 0x4E800020])
        self.assertIn("wrapping rlwimi", effects.analyze_record(record, self.boundary(record))["reason"])

    def test_receiver_boundary_alignment_cr_and_branch_fail_closed(self):
        cases = [
            ([0x80840000, 0x4E800020], "base is not receiver"),
            ([0x80830002, 0x4E800020], "unaligned"),
            ([(21 << 26) | (4 << 21) | (5 << 16) | 1, 0x4E800020], "condition-register"),
            ([0x48000001, 0x4E800020], "unsupported instruction"),
            ([0x80630000, 0x80830004, 0x4E800020], "receiver r3 changes"),
        ]
        for words, reason in cases:
            with self.subTest(reason=reason):
                record = self.synthetic(words)
                self.assertIn(reason, effects.analyze_record(record, self.boundary(record))["reason"])
        record = self.synthetic([0x80630000, 0x4E800020])
        bad = self.boundary(record); bad["instructions"].append({"address": "82001008", "word": "60000000"})
        self.assertIn("boundary differs", effects.analyze_record(record, bad)["reason"])

    def test_emitter_contains_original_contract_words_and_identity(self):
        results = effects.analyze(self.manifest, self.boundaries)
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "fixture.inc"
            count = effects.emit_cpp(self.manifest, results, target)
            text = target.read_text()
            self.assertGreater(count, 0)
            self.assertEqual(text.count("static void contract_sub_"), count)
            self.assertEqual(text.count(effects.RUN_ID), count)
            self.assertIn("EngineTraceStore", text)
            first = next(r for r in results if r["status"] == "local_contract")
            record = self.by_name[first["function"]]
            source = (ROOT / record["source"]).read_text()
            self.assertIn(frozen.generated_body(source, record["function"]), text)


if __name__ == "__main__":
    unittest.main()
