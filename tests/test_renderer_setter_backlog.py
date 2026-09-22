import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('setter_backlog',ROOT/'tools/rank-renderer-setter-families.py')
backlog=importlib.util.module_from_spec(spec)
spec.loader.exec_module(backlog)


class BacklogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline=json.loads((ROOT/'out/renderer-automation-pilot/accepted/setters-v1/contracts.json').read_text())
        cls.facts=json.loads((ROOT/'out/renderer-automation-pilot/ghidra/facts.json').read_text())

    def test_complete_partition_and_selected_priority(self):
        groups=backlog.rank(self.baseline,self.facts,self.baseline)
        names=[name for group in groups for name in group['functions']]
        self.assertEqual((len(groups),len(names),len(set(names))),(25,63,63))
        self.assertEqual(groups[0]['count'],12)
        self.assertEqual(groups[0]['complexity'],'straight-line integer')
        self.assertEqual(sum(len(g['remaining_functions']) for g in groups),63)
        self.assertTrue(all(g['task']=='R09.effects' and g['completion_test'] and g['dependencies'] for g in groups))

    def test_new_contracts_change_disposition_not_historical_membership(self):
        before=backlog.rank(self.baseline,self.facts,self.baseline)
        current=copy.deepcopy(self.baseline)
        selected=set(before[0]['functions'])
        for row in current['functions']:
            if row['function'] in selected:row['status']='local_contract'
        after=backlog.rank(self.baseline,self.facts,current)
        self.assertEqual([g['functions'] for g in before],[g['functions'] for g in after])
        self.assertEqual(after[0]['disposition'],'local-contract-generated')
        self.assertEqual(sum(len(g['remaining_functions']) for g in after),51)

    def test_incomplete_baseline_rejected(self):
        bad=copy.deepcopy(self.baseline)
        bad['functions']=[r for r in bad['functions'] if r['status']!='unsupported']
        with self.assertRaises(AssertionError):backlog.rank(bad,self.facts,self.baseline)


if __name__=='__main__':unittest.main()
