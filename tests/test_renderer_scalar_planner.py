import copy
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import renderer_scalar_planner as planner


def fixture(programs):
    records=[]; boundaries={}
    for i,words in enumerate(programs):
        address=0x82001000+i*0x100
        record=dict(function=f'sub_{address:08X}',partition='development',
                    instructions=[dict(address=f'{address+j*4:08X}',word=f'{w:08X}') for j,w in enumerate(words)])
        records.append(record)
        boundaries[record['function']]=dict(decompile_completed=True,instructions=copy.deepcopy(record['instructions']))
    return dict(functions=records),dict(functions=boundaries)


class PlannerTests(unittest.TestCase):
    def test_every_instruction_scanned_after_first_rejection(self):
        m,b=fixture([[0xC0030000,0xD0030004,0x4e800020]])
        p=planner.plan(m,b)
        self.assertEqual(p['functions'][0]['required_operations'],['lfs','stfs'])
        self.assertEqual(p['candidates'][0]['potential_count'],1)

    def test_same_opcode_different_extended_forms_not_conflated(self):
        self.assertNotEqual(planner.operation_key(0x7D6B5910),planner.operation_key(0x7D6B5038))
        self.assertNotEqual(planner.operation_key(0x7D6B2278),planner.operation_key(0x7D6B2279))
        self.assertEqual(planner.operation_key(0xEDAD0032),planner.operation_key(0xED4A0032))

    def test_condition_update_and_later_control_flow_both_required(self):
        m,b=fixture([[0x54830001,0x4D820020,0x4e800020]])
        operations=planner.plan(m,b)['functions'][0]['required_operations']
        self.assertIn('condition-register',operations)
        self.assertIn('control-flow',operations)
        self.assertIn('op19.xo16.bit00',operations)

    def test_boundary_memory_and_receiver_gates_excluded_from_unlocks(self):
        m,b=fixture([[0xC0010000,0xD0030000,0x4e800020],
                     [0x38600000,0xC0030000,0x4e800020],
                     [0xC0030000,0xD0030000,0x4e800020]])
        b['functions'][m['functions'][2]['function']]['instructions'].pop()
        p=planner.plan(m,b)
        self.assertEqual(len(p['investigations']),3)
        self.assertEqual(p['candidates'],[])
        self.assertIn('gate:receiver-mutation',p['functions'][1]['investigation_gates'])

    def test_ranking_counts_only_fully_covered_sets(self):
        m,b=fixture([[0x3D600001,0x4e800020],
                     [0x3D600001,0xC0030000,0xD0030000,0x4e800020],
                     [0xC0030000,0xD0030000,0x4e800020]])
        p=planner.plan(m,b)
        self.assertEqual([(r['operation_count'],r['potential_count']) for r in p['candidates']],[(1,1),(2,1),(3,3)])

    def test_deterministic_order_and_complete_census(self):
        m,b=fixture([[0x3D600001,0x4e800020],[0xC0030000,0xD0030000,0x4e800020]])
        expected=planner.plan(m,b);m['functions'].reverse()
        self.assertEqual(planner.plan(m,b),expected)
        b['functions'].pop(next(iter(b['functions'])))
        with self.assertRaisesRegex(ValueError,'accounting mismatch'):planner.plan(m,b)

    def test_real_corpus_has_no_acceptance_side_effects_or_missing_rejections(self):
        m=planner.load_manifest();b=planner.effects.load_boundaries()
        p=planner.plan(m,b)
        self.assertEqual(p['counts'],dict(local_contract=127,unsupported=43))
        rejected=[r for r in p['functions'] if r['status']=='unsupported']
        self.assertEqual(len(rejected),43)
        self.assertTrue(all(r['blockers'] for r in rejected))
        self.assertEqual(p['candidates'][0]['potential_functions'],['sub_821358F0'])
        floating=[r for r in rejected if r['required_operations']==['lfs','stfs']]
        self.assertEqual(len(floating),12)
        self.assertTrue(all('gate:memory-base' in r['investigation_gates'] for r in floating))


if __name__=='__main__':unittest.main()
