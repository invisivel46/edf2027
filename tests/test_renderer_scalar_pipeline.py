import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from renderer_dispatch import digest, file_hash, write_json
from renderer_scalar_pipeline import cached, exceptions
from renderer_scalar_acceptance import apply_bundle
from renderer_scalar_analysis import sha


class CacheTests(unittest.TestCase):
    def test_cache_hits_and_input_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            value,hit=cached(root,digest({'body':'one'}),lambda:{'value':1})
            self.assertFalse(hit)
            self.assertEqual(cached(root,digest({'body':'one'}),lambda:self.fail('cache miss')),(value,True))
            self.assertFalse(cached(root,digest({'body':'two'}),lambda:{'value':2})[1])

    def test_cache_corruption_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);cached(root,'key',lambda:{'passed':False})
            p=root/'key.json';data=json.loads(p.read_text());data['value']['passed']=True;write_json(p,data)
            with self.assertRaisesRegex(ValueError,'Corrupt scalar cache'):cached(root,'key',lambda:None)

    def test_exception_bundles_ignore_site_but_preserve_operation(self):
        rows=[dict(function='a',status='unsupported',reason='82100000: opcode C0010000 unsupported instruction'),
              dict(function='b',status='unsupported',reason='82100004: opcode C0230004 unsupported instruction')]
        groups=exceptions(rows)
        self.assertEqual(len(groups),1);self.assertEqual(groups[0]['functions'],['a','b'])


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.policy=dict(minimum_invocations=2,minimum_negative_controls=3,inputs={
            'tools/renderer_scalar_effects.py':'engine','tests/renderer_scalar_pipeline_tests.cpp':'oracle',
            'tools/renderer_scalar_pipeline.py':'pipeline','tests/fixtures/renderer-scalar-boundaries.json':'boundary'})
        write_json(self.root/'policy.json',self.policy)
        records=[];rows=[];bodies=[]
        for i in range(170):
            name=f'sub_{0x82000000+i*4:08X}';body=f'DEFINE_REX_FUNC({name}) {{\nreturn;\n}}'
            records.append(dict(function=name,source='bodies.cpp',body_sha256=sha(body.encode())))
            bodies.append(body);rows.append(dict(function=name,status='local_contract' if i<2 else 'unsupported'))
        (self.root/'bodies.cpp').write_text('\n'.join(bodies))
        write_json(self.root/'manifest.json',dict(functions=records))
        self.analysis=dict(functions=rows,engine_sha256='engine',boundaries_sha256='boundary',
                           manifest_sha256=file_hash(self.root/'manifest.json'))
        (self.root/'compiler').write_text('compiler');(self.root/'header').write_text('header')
        self.validation=dict(passed=True,oracle_sha256='oracle',pipeline_sha256='pipeline',functions=[
            dict(function=r['function'],passed=True,invocations=2,negative_controls=3,contract_sha256=digest(r),
                 body_sha=records[i]['body_sha256'])
            for i,r in enumerate(rows[:2])],runtime=dict(headers={str(self.root/'header'):file_hash(self.root/'header')},
            compiler=str(self.root/'compiler'),compiler_sha256=file_hash(self.root/'compiler')))
        self.entities={'function:'+r['function']:dict(kind='function',status='untriaged',evidence=[]) for r in rows}
        self.bundle=dict(version=1,disposition='partial',parent_complete=False,policy='policy.json',
                         policy_sha256=file_hash(self.root/'policy.json'),analysis='analysis.json',
                         validation='validation.json',manifest='manifest.json',functions=[r['function'] for r in rows[:2]])
        self.save()

    def save(self):
        write_json(self.root/'analysis.json',self.analysis);write_json(self.root/'validation.json',self.validation)
        self.bundle['artifacts']={n:file_hash(self.root/n) for n in ('analysis.json','validation.json','manifest.json')}
        self.bundle['id']=digest(dict(analysis=self.analysis,validation=self.validation,policy=self.bundle['policy_sha256']))
        write_json(self.root/'bundle.json',self.bundle)

    def apply(self):
        with patch('renderer_scalar_acceptance.verify_policy',return_value=self.policy):
            return apply_bundle(self.root,'bundle.json',self.entities,lambda n:self.root/n)

    def test_partial_only_and_idempotent(self):
        self.assertEqual(self.apply()['net_new_partial'],2)
        self.assertEqual(self.apply()['net_new_partial'],0)
        self.assertEqual(sum(e['status']=='untriaged' for e in self.entities.values()),168)

    def test_missing_case_or_failed_execution_rejected(self):
        self.validation['functions'].pop();self.save()
        with self.assertRaisesRegex(ValueError,'execution set mismatch'):self.apply()

    def test_mismatched_contract_rejected(self):
        self.validation['functions'][0]['contract_sha256']='different';self.save()
        with self.assertRaisesRegex(ValueError,'contract mismatch'):self.apply()

    def test_unreviewed_producer_cannot_supply_pass(self):
        self.validation['oracle_sha256']='other';self.save()
        with self.assertRaisesRegex(ValueError,'producer'):self.apply()

    def test_mismatched_compiled_body_rejected(self):
        self.validation['functions'][0]['body_sha']='different';self.save()
        with self.assertRaisesRegex(ValueError,'body mismatch'):self.apply()

    def test_changed_body_and_runtime_rejected(self):
        (self.root/'bodies.cpp').write_text('changed')
        with self.assertRaises(ValueError):self.apply()
        (self.root/'header').write_text('changed')
        with self.assertRaisesRegex(ValueError,'runtime header'):self.apply()

    def test_incomplete_corpus_rejected(self):
        self.analysis['functions'].pop();self.save()
        with self.assertRaisesRegex(ValueError,'accounting incomplete'):self.apply()


if __name__=='__main__':unittest.main()
