import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from renderer_replacement_plan import closure,mask,waves,disposition,symbol

class ReplacementPlanTests(unittest.TestCase):
    def test_comments_and_strings_do_not_verify_calls(self):
        text='// __imp__sub_82000000(x);\n"sub_82000001(x)"; /* other() */\n__imp__sub_82000002(x);'
        clean=mask(text)
        self.assertEqual(clean.count('\n'),text.count('\n'))
        self.assertNotIn('82000000',clean);self.assertNotIn('82000001',clean)
        self.assertIn('__imp__sub_82000002(x)',clean)

    def test_boundary_cut_does_not_expand_unrelated_closure(self):
        graph={'a':{'b','helper'},'helper':{'a','external'},'b':{'unrelated'},'unrelated':set()}
        self.assertEqual(closure({'a'},graph,{'a','b'}),
                         dict(functions=['a','helper'],boundary_cuts=['b'],external_targets=['external']))
        self.assertIn('unrelated',closure({'a','b'},graph,{'a','b'})['functions'])

    def test_cycle_is_rejected_and_waves_not_insertion_order(self):
        self.assertEqual(waves({'c':['b'],'b':['a'],'a':[]}),{'a':0,'b':1,'c':2})
        with self.assertRaises(ValueError):waves({'a':['b'],'b':['a']})

    def test_observation_fallback_and_producer_not_automatic_removal(self):
        self.assertIn('audit-only',disposition('audit oracle'))
        self.assertIn('compatibility',disposition('native-host disabled fallback'))
        self.assertIn('producer/interface',disposition('camera producer forwarding'))
        self.assertIn('platform',disposition('platform synchronization import'))

    def test_adapter_or_import_name_is_not_fabricated_guest_function(self):
        self.assertEqual(symbol('__imp__sub_821be8d0'),'sub_821BE8D0')
        self.assertIsNone(symbol('__imp__edf_native_indexed_cpu_tail'))
        self.assertIsNone(symbol('__imp__KeSetEvent'))

if __name__=='__main__':unittest.main()
