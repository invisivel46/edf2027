"""Accounting regressions: completion must not be inferred from inventory evidence."""
import copy
import sys
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from renderer_dispatch import read_json, file_hash
from renderer_tally import accounting, boundary_id, build


def historical_config():
    config = read_json(ROOT / 'docs/renderer-tally-reconciliation.json')
    config.pop('accepted_scalar_pipeline', None)
    return config


class TallyTests(unittest.TestCase):
    def test_current_automated_acceptance_preserves_open_obligations(self):
        snapshot = build(ROOT, read_json(ROOT / 'docs/renderer-tally-reconciliation.json'))
        summary = snapshot['summary']
        self.assertEqual(summary['automated_scalar_sets'][0]['accepted'], 127)
        self.assertEqual(summary['automated_scalar_sets'][0]['net_new_partial'], 43)
        self.assertEqual(summary['functions']['partial'], 511)
        self.assertEqual(summary['functions']['contract_understood'], 0)
        self.assertEqual(summary['obligation_accounting']['remaining'], 108048)
        self.assertTrue(all(e['status'] != 'complete' for e in snapshot['entities'].values()
                            if e['kind'] == 'task'))

    def test_identity_survives_csv_reordering_but_distinguishes_targets(self):
        row = dict(source='callbacks.csv', task='R01.callbacks', row='2', locator='site')
        target = dict(site='821A5158', target='821BE8D0')
        other = dict(row, row='99')
        self.assertEqual(boundary_id(row, target), boundary_id(other, target))
        self.assertNotEqual(boundary_id(row, target), boundary_id(row, dict(target, target='821D5978')))

    def test_accounting_includes_discovery_and_reopening(self):
        self.assertEqual(accounting(10, 3, 4, 1)['remaining'], 10)
        with self.assertRaises(ValueError):
            accounting(0, 0, 1)

    def test_real_reconciliation_is_conservative_and_reproducible(self):
        config = historical_config()
        snapshot = build(ROOT, config)
        summary = snapshot['summary']
        self.assertEqual(summary['functions']['total'], 9216)
        self.assertEqual(sum(v for k, v in summary['functions'].items() if k != 'total'), 9216)
        self.assertEqual(summary['functions']['contract_understood'], 0)
        self.assertEqual(summary['functions']['partial'], 468)
        self.assertEqual(summary['functions']['untriaged'], 8748)
        self.assertEqual(summary['accepted_getter_sets'], [{
            'id': 'scalar-getters-v1', 'accepted': 59, 'net_new_partial': 48,
            'disposition': 'partial'}])
        self.assertEqual(summary['accepted_setter_sets'], [{
            'id': 'scalar-setters-v1', 'accepted': 24, 'net_new_partial': 24,
            'disposition': 'partial'}])
        reviewed_hooks = [key for key, value in snapshot['entities'].items()
                          if 'retained-call-effect-reviews.csv' in value.get('evidence', [])]
        self.assertEqual(len(reviewed_hooks), 145)
        self.assertEqual(summary['accepted_bounded_research'], 2)
        self.assertEqual(summary['obligation_accounting']['remaining'], summary['unique_boundary_obligations'])
        for symbol in ('sub_821A5080', 'sub_821A4980', 'sub_821A3BA0'):
            self.assertEqual(snapshot['entities']['function:' + symbol]['status'], 'partial')
        self.assertEqual(snapshot['entities']['task:R01.callbacks']['status'], 'open')
        self.assertEqual(build(ROOT, config)['snapshot_id'], snapshot['snapshot_id'])

    def test_duplicate_acceptance_cannot_double_count_progress(self):
        config = historical_config()
        config['accepted_slices'].append(copy.deepcopy(config['accepted_slices'][0]))
        with self.assertRaisesRegex(ValueError, 'Duplicate accepted slice'):
            build(ROOT, config)

    def test_duplicate_getter_acceptance_cannot_double_count(self):
        config = historical_config()
        config['accepted_getter_sets'].append(config['accepted_getter_sets'][0])
        with self.assertRaisesRegex(ValueError, 'Duplicate getter acceptance'):
            build(ROOT, config)

    def test_getter_archive_corruption_is_rejected(self):
        config = historical_config()
        original = file_hash
        def changed_hash(path):
            path = Path(path).as_posix()
            if path.endswith('accepted/getters-v1/contracts.json'):
                return 'changed'
            return original(path)
        with patch('renderer_tally.file_hash', side_effect=changed_hash):
            with self.assertRaisesRegex(ValueError, 'Stale getter artifact'):
                build(ROOT, config)

    def test_duplicate_setter_acceptance_cannot_double_count(self):
        config = historical_config()
        config['accepted_setter_sets'].append(config['accepted_setter_sets'][0])
        with self.assertRaisesRegex(ValueError, 'Duplicate setter acceptance'):
            build(ROOT, config)

    def test_setter_archive_corruption_is_rejected(self):
        config = historical_config()
        original = file_hash
        def changed_hash(path):
            path = Path(path).as_posix()
            if path.endswith('accepted/setters-v1/renderer_setter_analysis.py'):
                return 'changed'
            return original(path)
        with patch('renderer_tally.file_hash', side_effect=changed_hash):
            with self.assertRaisesRegex(ValueError, 'Stale setter artifact'):
                build(ROOT, config)

    def test_setter_required_pin_cannot_be_omitted(self):
        config = historical_config()
        acceptance = read_json(ROOT / 'docs/renderer-setter-acceptance.json')
        del acceptance['artifacts'][acceptance['harness_source']]
        real_read = read_json
        def changed_read(path):
            if Path(path).name == 'renderer-setter-acceptance.json':
                return acceptance
            return real_read(path)
        with patch('renderer_tally.read_json', side_effect=changed_read):
            with self.assertRaisesRegex(ValueError, 'omits a required artifact pin'):
                build(ROOT, config)

    def test_live_setter_tool_and_harness_do_not_invalidate_historical_acceptance(self):
        config = historical_config()
        original = file_hash
        def changed_hash(path):
            relative = Path(path).as_posix()
            if relative.endswith(('tools/renderer_setter_analysis.py',
                                  'tests/renderer_setter_contract_tests.cpp')):
                return 'changed-live-source'
            return original(path)
        with patch('renderer_tally.file_hash', side_effect=changed_hash):
            snapshot = build(ROOT, config)
        self.assertEqual(snapshot['summary']['accepted_setter_sets'][0]['accepted'], 24)

    def test_changed_setter_runtime_header_is_rejected(self):
        config = historical_config()
        config['accepted_getter_sets'] = []
        original = file_hash
        def changed_hash(path):
            if Path(path).as_posix().endswith('generated/default/edf2017_pch.h'):
                return 'changed'
            return original(path)
        with patch('renderer_tally.file_hash', side_effect=changed_hash):
            with self.assertRaisesRegex(ValueError, 'Stale setter artifact'):
                build(ROOT, config)

    def test_changed_generated_setter_body_is_rejected(self):
        config = historical_config()
        acceptance = read_json(ROOT / 'docs/renderer-setter-acceptance.json')
        manifest = read_json(ROOT / acceptance['manifest'])
        symbol = acceptance['functions'][0]
        record = next(row for row in manifest['functions'] if row['function'] == symbol)
        original = Path.read_text
        def changed_text(path, *args, **kwargs):
            text = original(path, *args, **kwargs)
            if Path(path).as_posix().endswith(record['source']):
                needle = 'DEFINE_REX_FUNC(' + symbol + ') {\n'
                return text.replace(needle, needle + '// changed\n', 1)
            return text
        with patch('pathlib.Path.read_text', new=changed_text):
            with self.assertRaisesRegex(ValueError, 'Changed accepted setter body'):
                build(ROOT, config)

    def test_getter_required_pin_cannot_be_omitted(self):
        config = historical_config()
        acceptance = read_json(ROOT / 'docs/renderer-getter-acceptance.json')
        del acceptance['artifacts'][acceptance['report']]
        real_read = read_json
        def changed_read(path):
            if Path(path).name == 'renderer-getter-acceptance.json':
                return acceptance
            return real_read(path)
        with patch('renderer_tally.read_json', side_effect=changed_read):
            with self.assertRaisesRegex(ValueError, 'omits a required artifact pin'):
                build(ROOT, config)

    def test_changed_generated_getter_body_is_rejected(self):
        config = historical_config()
        acceptance = read_json(ROOT / 'docs/renderer-getter-acceptance.json')
        manifest = read_json(ROOT / acceptance['manifest'])
        symbol = acceptance['functions'][0]
        record = next(row for row in manifest['functions'] if row['function'] == symbol)
        original = Path.read_text
        def changed_text(path, *args, **kwargs):
            text = original(path, *args, **kwargs)
            if Path(path).as_posix().endswith(record['source']):
                needle = 'DEFINE_REX_FUNC(' + symbol + ') {\n'
                return text.replace(needle, needle + '// changed\n', 1)
            return text
        with patch('pathlib.Path.read_text', new=changed_text):
            with self.assertRaisesRegex(ValueError, 'Changed accepted getter body'):
                build(ROOT, config)

    def test_changed_local_source_cannot_keep_partial_credit(self):
        config = historical_config()
        def changed_hash(path):
            return 'changed' if Path(path).name == 'guest_shader_bridge.cpp' else file_hash(path)
        with patch('renderer_tally.file_hash', side_effect=changed_hash):
            with self.assertRaisesRegex(ValueError, 'Stale local evidence'):
                build(ROOT, config)


if __name__ == '__main__':
    unittest.main()
