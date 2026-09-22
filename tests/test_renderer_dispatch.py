"""Structural acceptance tests for the coordinator-owned renderer dispatch ledger."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import renderer_dispatch as dispatch  # noqa: E402

_offline_spec = importlib.util.spec_from_file_location('renderer_offline_gate', TOOLS / 'validate-renderer-offline.py')
_offline_gate = importlib.util.module_from_spec(_offline_spec)
_offline_spec.loader.exec_module(_offline_gate)
OFFLINE_TESTS = _offline_gate.CPU + _offline_gate.RENDER


def run(*args, cwd):
    subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class DispatchFixture(unittest.TestCase):
    """A small real Git repository; dispatch sees the same content identities it sees in production."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / 'root'
        self.root.mkdir()
        run('git', 'init', cwd=self.root)
        run('git', 'config', 'user.email', 'dispatch@example.invalid', cwd=self.root)
        run('git', 'config', 'user.name', 'Dispatch tests', cwd=self.root)
        self.write('src/input.txt', 'pinned input\n')
        self.write('src/owned.txt', 'before\n')
        self.write('tests/outside.txt', 'outside\n')
        self.write('docs/renderer-coverage.json', json.dumps({'tasks': [
            {'id': 'parent', 'dependencies': []}, {'id': 'dependent-parent', 'dependencies': ['parent']}
        ]}))
        run('git', 'add', '.', cwd=self.root)
        run('git', 'commit', '-m', 'baseline', cwd=self.root)
        self.clone_number = 0
        self.write('knowledge/evidence/ev-proof.md', 'proof\n')
        self.write('knowledge/journal/session-proof.md', 'session\n')

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
        return path

    def clone(self, name='worker'):
        self.clone_number += 1
        destination = self.root.parent / (name + '-' + str(self.clone_number))
        run('git', 'clone', str(self.root), str(destination), cwd=self.root.parent)
        return destination

    def slice(self, ident='implementation', *, kind='implementation', parent=None,
              depends_on=None, allowed_files=None, required_evidence=None, tally_ids=None):
        if allowed_files is None:
            allowed_files = ['src/owned.txt'] if kind == 'implementation' else []
        result = {
            'id': ident, 'parent': parent, 'kind': kind, 'model': 'gpt-5.6-sol', 'effort': 'medium',
            'scope': 'bounded test scope', 'excludes': 'nothing else', 'depends_on': depends_on or [],
            'required_evidence': required_evidence or [], 'allowed_files': allowed_files,
            'inputs': ['src/input.txt'], 'cases': [{'id': 'acceptance', 'description': 'works'}],
            'steps': ['perform bounded work'], 'stop_conditions': ['stop safely'],
            'checks': {'unit': ['python', '-c', 'pass']},
        }
        if tally_ids is not None:
            result['tally_ids'] = tally_ids
        return result

    def tally_ref(self, entities=None):
        path = self.write('knowledge/renderer-tally.json', json.dumps({
            'snapshot_id': 'snapshot-test',
            'entities': entities or {'entity-a': {'kind': 'system'}, 'entity-b': {'kind': 'system'}},
        }))
        return {'path': 'knowledge/renderer-tally.json', 'sha256': self.sha(path)}

    def ledger(self, *slices, tally=None):
        spec = {'coordinator': 'coordinator', 'slices': list(slices)}
        if tally is not None:
            spec['tally'] = tally
        return dispatch.initialize(self.root, spec)

    def ready_assign(self, ledger, key='implementation', owner='worker', workspace=None):
        workspace = workspace or self.clone(key + '-worker')
        if workspace != self.root:
            for name in ledger['baseline']['inputs']:
                target = workspace / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(self.root / name, target)
        dispatch.readiness(ledger, key)
        return workspace, dispatch.assign(ledger, key, owner, workspace)

    @staticmethod
    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    def result(self, ledger, key, workspace, *, parent_complete=False, evidence=True,
               cases=True, artifacts=True, offline=True):
        item = ledger['slices'][key]
        attempt = item['attempts'][-1]
        output = Path(attempt['output'])
        artifact_path = workspace / output / 'case.log'
        artifact_path.parent.mkdir(parents=True, exist_ok=True)
        artifact_path.write_text('case passed\n', encoding='utf-8')
        refs = []
        if artifacts:
            refs.append({'path': str(output / 'case.log').replace('\\', '/'), 'sha256': self.sha(artifact_path)})
        report_ref = None
        source_id = dispatch.digest(dispatch.source_manifest(workspace))
        if item['spec']['kind'] == 'implementation' and offline:
            report_path = workspace / output / 'offline.json'
            report = {'passed': True, 'suite': 'all', 'source_id_start': source_id,
                      'source_id_end': source_id, 'commands': [{'exit_code': 0}],
                      'selected': OFFLINE_TESTS,
                      'tests': [{'name': name, 'status': 'run'} for name in OFFLINE_TESTS]}
            report_path.write_text(json.dumps(report), encoding='utf-8')
            report_ref = {'path': str(output / 'offline.json').replace('\\', '/'), 'sha256': self.sha(report_path)}
            refs.append(report_ref)
        return {
            'slice_id': key, 'attempt_id': attempt['id'], 'owner': attempt['owner'],
            'packet_id': attempt['packet_id'], 'baseline_id': ledger['baseline']['id'],
            'disposition': 'slice_complete', 'parent_complete': parent_complete, 'source_id': source_id,
            'changed_files': sorted(p for p, value in dispatch.source_manifest(workspace).items()
                                    if ledger['baseline']['source'].get(p) != value) +
                             sorted(p for p in ledger['baseline']['source']
                                    if p not in dispatch.source_manifest(workspace)),
            'evidence_ids': ['ev-proof'] if evidence else [], 'epistemic_session': 'session-proof',
            'artifacts': refs, 'cases': ({'acceptance': {'status': 'passed', 'details': 'observed',
                                                            'artifact': refs[0]['path']}} if cases and refs else {}),
            'offline_report': report_ref,
            'checks': {'unit': {'command': ['python', '-c', 'pass'], 'exit_code': 0,
                                'log': refs[0] if refs else {'path': 'out/no.log', 'sha256': '0'}}},
        }

    def submit_and_review(self, ledger, key, workspace, result=None):
        result = result or self.result(ledger, key, workspace)
        dispatch.submit(ledger, key, result)
        attempt = ledger['slices'][key]['attempts'][-1]
        review_path = workspace / attempt['output'] / 'review.json'
        review_path.write_text(json.dumps({'reviewer': 'reviewer', 'result_id': attempt['result_id'],
                                            'packet_id': attempt['packet_id'], 'decision': 'accept',
                                            'contract_checked': True, 'oracle_checked': True,
                                            'details': 'independently checked'}), encoding='utf-8')
        review_ref = {'path': str(Path(attempt['output']) / 'review.json').replace('\\', '/'),
                      'sha256': self.sha(review_path)}
        dispatch.review(ledger, key, 'reviewer', review_ref)
        return result


class SourceAndSpecificationTests(DispatchFixture):
    def test_tally_is_optional_for_library_but_pinned_when_present(self):
        old_ledger = self.ledger(self.slice())
        self.assertNotIn('tally', old_ledger)

        tally = self.tally_ref()
        ledger = self.ledger(self.slice(tally_ids=['entity-a']), tally=tally)
        self.assertEqual(tally['sha256'], ledger['baseline']['inputs'][tally['path']])
        self.assertEqual('snapshot-test', ledger['tally']['snapshot']['snapshot_id'])
        self.write(tally['path'], '{}')
        with self.assertRaisesRegex(ValueError, 'Stale input'):
            dispatch.readiness(ledger, 'implementation')

    def test_tally_accepts_generator_metadata(self):
        snapshot = {'version': 1, 'inputs': {}, 'reconciliation_id': 'reconcile-test',
                    'summary': {'total': 1}, 'events': [], 'limitations': ['bounded'],
                    'entities': {'task:R01': {'kind': 'task'}}}
        path = self.write('knowledge/renderer-tally.json', json.dumps(snapshot))
        tally = {'path': 'knowledge/renderer-tally.json', 'sha256': self.sha(path)}
        ledger = self.ledger(self.slice(tally_ids=['task:R01']), tally=tally)
        self.assertEqual(snapshot, ledger['tally']['snapshot'])

    def test_tally_slices_require_nonempty_known_unique_ids(self):
        tally = self.tally_ref()
        for tally_ids, message in ((None, 'Missing tally IDs'), ([], 'Missing tally IDs'),
                                   (['entity-a', 'entity-a'], 'Duplicate tally ID'),
                                   (['unknown'], 'Unknown tally ID')):
            with self.subTest(tally_ids=tally_ids):
                with self.assertRaisesRegex(ValueError, message):
                    self.ledger(self.slice(tally_ids=tally_ids), tally=tally)

    def test_source_manifest_pins_dirty_and_untracked_content(self):
        self.write('src/owned.txt', 'dirty but intentional\n')
        self.write('tests/untracked.txt', 'untracked source\n')
        ledger = self.ledger(self.slice())
        self.assertIn('tests/untracked.txt', ledger['baseline']['source'])
        self.assertEqual(ledger['baseline']['source']['src/owned.txt'],
                         dispatch.file_hash(self.root / 'src/owned.txt'))
        worker = self.clone()
        dispatch.readiness(ledger, 'implementation')
        with self.assertRaisesRegex(ValueError, 'pinned dirty baseline'):
            dispatch.assign(ledger, 'implementation', 'worker', worker)

    def test_source_manifest_records_staged_deletions_as_tombstones(self):
        run('git', 'rm', 'src/owned.txt', cwd=self.root)
        baseline = dispatch.source_manifest(self.root)
        self.assertIn('src/owned.txt', baseline)
        self.assertIsNone(baseline['src/owned.txt'])

        # A checkout begins from HEAD (where the file exists); preparation must be able to
        # reproduce the tombstone by removing it before checking the pinned identity.
        worker = self.clone('staged-deletion')
        self.assertTrue((worker / 'src/owned.txt').is_file())
        (worker / 'src/owned.txt').unlink()
        self.assertEqual(baseline, dispatch.source_manifest(worker))

    def test_paths_traversal_and_case_aliases_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unsafe path'):
            dispatch.relative('../outside.txt')
        with self.assertRaisesRegex(ValueError, 'canonical'):
            dispatch.relative('src\\owned.txt')
        aliased = self.slice(allowed_files=['src/Owned.txt', 'src/owned.txt'])
        with self.assertRaisesRegex(ValueError, 'Case-aliased ownership'):
            self.ledger(aliased)
        traversal = self.slice(allowed_files=['src/../owned.txt'])
        with self.assertRaisesRegex(ValueError, 'Unsafe path'):
            self.ledger(traversal)

    def test_dependency_readiness_and_cycles_require_integrated_contract_evidence(self):
        cycle_a = self.slice('a', kind='investigation', depends_on=['b'])
        cycle_b = self.slice('b', kind='investigation', depends_on=['a'])
        with self.assertRaisesRegex(ValueError, 'Dependency cycle'):
            self.ledger(cycle_a, cycle_b)

        first = self.slice('first', kind='investigation')
        second = self.slice('second', kind='investigation', depends_on=['first'])
        ledger = self.ledger(first, second)
        with self.assertRaisesRegex(ValueError, 'Unintegrated dependency'):
            dispatch.readiness(ledger, 'second')
        ledger['slices']['first']['status'] = 'integrated'
        review_path = self.write('out/dependency-review.json', 'reviewed\n')
        no_evidence = {'evidence_ids': [], 'artifacts': []}
        ledger['slices']['first']['attempts'].append({
            'workspace': str(self.root), 'result': no_evidence, 'result_id': dispatch.digest(no_evidence),
            'review': {'artifact': {'path': 'out/dependency-review.json', 'sha256': self.sha(review_path)}},
            'evidence_hashes': {},
        })
        with self.assertRaisesRegex(ValueError, 'no contract evidence'):
            dispatch.readiness(ledger, 'second')
        ledger['slices']['first']['attempts'][-1]['result']['evidence_ids'] = ['ev-proof']
        ledger['slices']['first']['attempts'][-1]['result_id'] = dispatch.digest(
            ledger['slices']['first']['attempts'][-1]['result'])
        ledger['slices']['first']['attempts'][-1]['evidence_hashes'] = {
            'ev-proof': self.sha(self.root / 'knowledge/evidence/ev-proof.md')}
        dispatch.readiness(ledger, 'second')
        self.assertEqual('ready', ledger['slices']['second']['status'])


class AssignmentAndResultTests(DispatchFixture):
    def tally_result(self):
        tally = self.tally_ref()
        ledger = self.ledger(self.slice(tally_ids=['entity-a', 'entity-b']), tally=tally)
        worker, _ = self.ready_assign(ledger)
        result = self.result(ledger, 'implementation', worker)
        result['tally_updates'] = [
            {'id': 'entity-a', 'disposition': 'resolved', 'evidence_ids': ['ev-proof'], 'details': 'proved'},
            {'id': 'entity-b', 'disposition': 'unchanged', 'evidence_ids': [], 'details': 'inspected'},
        ]
        result['discoveries'] = []
        return ledger, worker, result

    def test_tally_result_accepts_exact_updates_and_new_discoveries(self):
        ledger, _, result = self.tally_result()
        result['discoveries'] = [{'id': 'entity-new', 'details': 'new gap', 'evidence_ids': ['ev-proof']}]
        dispatch.validate_result(ledger, 'implementation', result)

    def test_tally_mutation_is_rejected_at_result_and_integration(self):
        ledger, worker, result = self.tally_result()
        (worker / ledger['tally']['ref']['path']).write_text('{}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale input'):
            dispatch.validate_result(ledger, 'implementation', result)

        ledger, worker, result = self.tally_result()
        self.submit_and_review(ledger, 'implementation', worker, result)
        self.write(ledger['tally']['ref']['path'], '{}')
        with self.assertRaisesRegex(ValueError, 'Stale input'):
            dispatch.integrate(ledger, 'implementation')

    def test_tally_result_rejects_inexact_updates_and_unbound_resolution_evidence(self):
        ledger, _, result = self.tally_result()
        result['tally_updates'].pop()
        with self.assertRaisesRegex(ValueError, 'exactly assigned'):
            dispatch.validate_result(ledger, 'implementation', result)

        ledger, _, result = self.tally_result()
        result['tally_updates'][0]['evidence_ids'] = ['ev-other']
        with self.assertRaisesRegex(ValueError, 'result evidence'):
            dispatch.validate_result(ledger, 'implementation', result)

    def test_tally_result_rejects_existing_and_duplicate_discoveries(self):
        for discoveries, message in ((
                [{'id': 'entity-a', 'details': 'existing', 'evidence_ids': []}], 'already exists'), (
                [{'id': 'entity-new', 'details': 'first', 'evidence_ids': []},
                 {'id': 'entity-new', 'details': 'second', 'evidence_ids': []}], 'Duplicate discovery')):
            with self.subTest(message=message):
                ledger, _, result = self.tally_result()
                result['discoveries'] = discoveries
                with self.assertRaisesRegex(ValueError, message):
                    dispatch.validate_result(ledger, 'implementation', result)

    def test_implementation_requires_isolated_matching_checkout(self):
        ledger = self.ledger(self.slice())
        dispatch.readiness(ledger, 'implementation')
        with self.assertRaisesRegex(ValueError, 'isolated checkout'):
            dispatch.assign(ledger, 'implementation', 'worker', self.root)
        worker = self.clone()
        attempt = dispatch.assign(ledger, 'implementation', 'worker', worker)
        self.assertEqual(worker.resolve(), Path(attempt['workspace']))

    def test_duplicate_owner_and_concurrent_write_ownership_conflicts_are_rejected(self):
        first = self.slice('first')
        second = self.slice('second', allowed_files=['src/owned.txt'])
        ledger = self.ledger(first, second)
        worker_one, _ = self.ready_assign(ledger, 'first', 'worker-one')
        worker_two = self.clone('second-worker')
        dispatch.readiness(ledger, 'second')
        with self.assertRaisesRegex(ValueError, 'ownership overlap'):
            dispatch.assign(ledger, 'second', 'worker-two', worker_two)
        ledger = self.ledger(self.slice('first'), self.slice('second', allowed_files=['src/another.txt']))
        self.write('src/another.txt', 'exists\n')
        # Recreate the ledger after changing tracked source so both checkouts share its baseline.
        run('git', 'add', 'src/another.txt', cwd=self.root)
        run('git', 'commit', '-m', 'add second owned file', cwd=self.root)
        ledger = self.ledger(self.slice('first'), self.slice('second', allowed_files=['src/another.txt']))
        self.ready_assign(ledger, 'first', 'same-owner')
        dispatch.readiness(ledger, 'second')
        with self.assertRaisesRegex(ValueError, 'Owner already'):
            dispatch.assign(ledger, 'second', 'same-owner', self.clone('another-worker'))

    def test_result_rejects_missing_evidence_artifacts_and_cases(self):
        for label in ('evidence', 'artifacts', 'cases'):
            with self.subTest(label=label):
                ledger = self.ledger(self.slice())
                worker, _ = self.ready_assign(ledger)
                result = self.result(ledger, 'implementation', worker, evidence=label != 'evidence',
                                     cases=label != 'cases')
                if label == 'artifacts':
                    result['artifacts'] = []
                with self.assertRaises(ValueError):
                    dispatch.validate_result(ledger, 'implementation', result)

    def test_result_rejects_out_of_scope_change_and_parent_completion(self):
        ledger = self.ledger(self.slice())
        worker, _ = self.ready_assign(ledger)
        (worker / 'tests/outside.txt').write_text('mutated\n', encoding='utf-8')
        result = self.result(ledger, 'implementation', worker)
        with self.assertRaisesRegex(ValueError, 'Out-of-scope'):
            dispatch.validate_result(ledger, 'implementation', result)

        ledger = self.ledger(self.slice())
        worker, _ = self.ready_assign(ledger)
        result = self.result(ledger, 'implementation', worker, parent_complete=True)
        with self.assertRaisesRegex(ValueError, 'parent'):
            dispatch.validate_result(ledger, 'implementation', result)

    def test_offline_report_rejects_wrong_suite_and_stale_source(self):
        for mutation, message in ((lambda report: report.update(suite='partial'), 'Full offline'),
                                  (lambda report: report.update(source_id_end='stale'), 'Offline report does not bind')):
            with self.subTest(message=message):
                ledger = self.ledger(self.slice())
                worker, _ = self.ready_assign(ledger)
                result = self.result(ledger, 'implementation', worker)
                path = worker / result['offline_report']['path']
                report = json.loads(path.read_text(encoding='utf-8'))
                mutation(report)
                path.write_text(json.dumps(report), encoding='utf-8')
                result['offline_report']['sha256'] = self.sha(path)
                with self.assertRaisesRegex(ValueError, message):
                    dispatch.validate_result(ledger, 'implementation', result)


class ReviewIntegrationAndLockTests(DispatchFixture):
    def test_self_review_and_changed_artifact_after_review_are_rejected(self):
        ledger = self.ledger(self.slice())
        worker, _ = self.ready_assign(ledger)
        result = self.result(ledger, 'implementation', worker)
        dispatch.submit(ledger, 'implementation', result)
        with self.assertRaisesRegex(ValueError, 'Independent reviewer'):
            dispatch.review(ledger, 'implementation', 'worker', result['artifacts'][0])

        ledger = self.ledger(self.slice())
        worker, _ = self.ready_assign(ledger)
        result = self.submit_and_review(ledger, 'implementation', worker)
        (worker / result['artifacts'][0]['path']).write_text('altered after review\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Missing/changed artifact'):
            dispatch.integrate(ledger, 'implementation')

    def test_complete_research_submit_review_and_integrate(self):
        ledger = self.ledger(self.slice('research', kind='investigation'))
        dispatch.readiness(ledger, 'research')
        attempt = dispatch.assign(ledger, 'research', 'researcher', self.root)
        result = self.result(ledger, 'research', self.root)
        dispatch.submit(ledger, 'research', result)
        review_path = self.root / attempt['output'] / 'review.json'
        review_path.write_text(json.dumps({'reviewer': 'reviewer', 'result_id': ledger['slices']['research']['attempts'][-1]['result_id'],
                                            'packet_id': attempt['packet_id'], 'decision': 'accept', 'contract_checked': True,
                                            'oracle_checked': True, 'details': 'checked'}), encoding='utf-8')
        dispatch.review(ledger, 'research', 'reviewer',
                        {'path': str(Path(attempt['output']) / 'review.json').replace('\\', '/'), 'sha256': self.sha(review_path)})
        dispatch.integrate(ledger, 'research')
        self.assertEqual('integrated', ledger['slices']['research']['status'])

    def test_implementation_integrates_only_with_fresh_root_report(self):
        ledger = self.ledger(self.slice())
        worker, _ = self.ready_assign(ledger)
        (worker / 'src/owned.txt').write_text('implemented\n', encoding='utf-8')
        result = self.submit_and_review(ledger, 'implementation', worker)
        (self.root / 'src/owned.txt').write_text('implemented\n', encoding='utf-8')
        source_id = dispatch.digest(dispatch.source_manifest(self.root))
        report_path = self.root / 'out/root-offline.json'
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps({'passed': True, 'suite': 'wrong', 'source_id_start': source_id,
                                           'source_id_end': source_id, 'commands': [{'exit_code': 0}],
                                           'selected': OFFLINE_TESTS,
                                           'tests': [{'name': name, 'status': 'run'} for name in OFFLINE_TESTS]}), encoding='utf-8')
        wrong = {'path': 'out/root-offline.json', 'sha256': self.sha(report_path)}
        with self.assertRaisesRegex(ValueError, 'Full offline gate'):
            dispatch.integrate(ledger, 'implementation', wrong)
        report_path.write_text(json.dumps({'passed': True, 'suite': 'all', 'source_id_start': source_id,
                                           'source_id_end': source_id, 'commands': [{'exit_code': 0}],
                                           'selected': OFFLINE_TESTS,
                                           'tests': [{'name': name, 'status': 'run'} for name in OFFLINE_TESTS]}), encoding='utf-8')
        fresh = {'path': 'out/root-offline.json', 'sha256': self.sha(report_path)}
        dispatch.integrate(ledger, 'implementation', fresh)
        self.assertEqual('integrated', ledger['slices']['implementation']['status'])
        previous = copy.deepcopy(ledger['baseline'])
        dispatch.advance(ledger, 'accepted implementation applies to the refreshed source baseline')
        self.assertEqual(previous, ledger['baseline_history'][-1]['baseline'])
        self.assertEqual(dispatch.source_manifest(self.root), ledger['baseline']['source'])

    def test_advance_rejects_in_flight_assignment(self):
        ledger = self.ledger(self.slice())
        self.ready_assign(ledger)
        with self.assertRaisesRegex(ValueError, 'active assignments'):
            dispatch.advance(ledger, 'cannot move a baseline while a worker is active')

    def test_ledger_lock_rejects_existing_lock_and_cleans_up(self):
        ledger_path = self.root / 'out/ledger.json'
        lock = Path(str(ledger_path) + '.lock')
        lock.parent.mkdir(parents=True, exist_ok=True)
        lock.write_text('other coordinator\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'ledger is locked'):
            with dispatch.locked(ledger_path):
                self.fail('existing lock must not be acquired')
        lock.unlink()
        with dispatch.locked(ledger_path):
            self.assertTrue(lock.exists())
        self.assertFalse(lock.exists())


if __name__ == '__main__':
    unittest.main()
