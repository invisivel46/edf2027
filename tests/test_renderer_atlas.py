import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from renderer_atlas import cfg, components, decompiler_globals, page

class AtlasTests(unittest.TestCase):
    def test_calls_not_cfg_branches_and_external_exit_preserved(self):
        instructions=[dict(address='1000',call=True,flows=['9000'],fallthrough='1004'),
                      dict(address='1004',call=False,flows=['1000','8000'],fallthrough='1008')]
        self.assertEqual(cfg(instructions),[('1000','1004','fallthrough',True),
            ('1004','1000','branch',True),('1004','1008','fallthrough',False),('1004','8000','branch',False)])

    def test_dependency_cycles_not_conflated_with_reachability(self):
        edges=[('a','b'),('b','a'),('b','c'),('c','d'),('d','c'),('e','z')]
        expected=[['a','b'],['c','d'],['e']]
        self.assertEqual(components(set('abcde'),edges),expected)
        self.assertEqual(components(set('edcba'),list(reversed(edges))),expected)

    def test_long_graph_does_not_require_python_recursion(self):
        nodes=[str(i) for i in range(10000)]
        self.assertEqual(len(components(nodes,list(zip(nodes,nodes[1:])))),10000)

    def test_globals_preserve_provenance_and_exclude_locals_and_functions(self):
        code='fRam82017120 + iRam8257bfb4 + DAT_82001000 + FUN_820A0000 + param_1 + fStack_30 + fRam82017120'
        self.assertEqual(decompiler_globals(code),[('DAT_82001000','82001000'),('fRam82017120','82017120'),('iRam8257bfb4','8257BFB4')])

    def test_page_title_is_escaped(self):
        self.assertIn('&lt;script&gt;',page('<script>','body'))

if __name__=='__main__':unittest.main()
