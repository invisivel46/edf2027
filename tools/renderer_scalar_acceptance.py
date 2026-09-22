"""Generic, policy-gated partial evidence imports for the automated scalar lane."""
from pathlib import Path
import json
from renderer_dispatch import digest, file_hash, local, read_json, require
from renderer_scalar_analysis import generated_body, sha


def verify_policy(root, path, pin=None):
    policy = read_json(pin(path) if pin else local(root, path))
    require(policy['version'] == 1 and policy['disposition'] == 'partial', 'Invalid scalar policy')
    require(policy['reviewed'] is True, 'Scalar policy has not been reviewed')
    require(policy['scope'] == 'frozen-scalar-170', 'Unexpected scalar policy scope')
    required = {'tools/renderer_scalar_effects.py', 'tools/renderer_scalar_pipeline.py',
        'tools/renderer_scalar_acceptance.py', 'tools/renderer_scalar_analysis.py',
        'tests/renderer_scalar_pipeline_tests.cpp', 'tests/fixtures/renderer-scalar-pilot.json',
        'tests/fixtures/renderer-scalar-boundaries.json', 'generated/default/edf2017_pch.h'}
    require(required <= set(policy['inputs']), 'Scalar policy lacks required reviewed inputs')
    for name, expected in policy['inputs'].items():
        resolved = pin(name) if pin else local(root, name)
        require(file_hash(resolved) == expected, 'Changed reviewed scalar dependency: ' + name)
    return policy


def apply_bundle(root, name, entities, pin):
    bundle = read_json(pin(name))
    require(bundle['version'] == 1 and bundle['disposition'] == 'partial', 'Invalid scalar bundle')
    require(bundle['parent_complete'] is False, 'Scalar bundle cannot close parent')
    policy_path = bundle['policy']
    policy = verify_policy(root, policy_path, pin)
    require(file_hash(pin(policy_path)) == bundle['policy_sha256'], 'Scalar bundle policy drift')
    required = {bundle[k] for k in ('analysis', 'validation', 'manifest')}
    require(required <= set(bundle['artifacts']), 'Scalar bundle missing required artifact pins')
    artifact_bytes = {}
    for path, expected in bundle['artifacts'].items():
        data = pin(path).read_bytes()
        require(sha(data) == expected, 'Changed scalar evidence: ' + path)
        artifact_bytes[path] = data
    analysis = json.loads(artifact_bytes[bundle['analysis']])
    validation = json.loads(artifact_bytes[bundle['validation']])
    manifest = json.loads(artifact_bytes[bundle['manifest']])
    require(analysis['engine_sha256'] == policy['inputs']['tools/renderer_scalar_effects.py'],
            'Scalar evidence engine differs from reviewed policy')
    require(analysis['manifest_sha256'] == sha(artifact_bytes[bundle['manifest']]) and
            analysis['boundaries_sha256'] == policy['inputs']['tests/fixtures/renderer-scalar-boundaries.json'],
            'Scalar evidence input identity mismatch')
    require(validation['oracle_sha256'] == policy['inputs']['tests/renderer_scalar_pipeline_tests.cpp'] and
            validation['pipeline_sha256'] == policy['inputs']['tools/renderer_scalar_pipeline.py'],
            'Scalar validation producer differs from reviewed policy')
    require(bundle['id'] == digest(dict(analysis=analysis,validation=validation,policy=bundle['policy_sha256'])),
            'Scalar bundle content identity mismatch')
    records = {r['function']: r for r in manifest['functions']}
    require(len(records) == 170 and set(records) == {r['function'] for r in analysis['functions']},
            'Scalar corpus accounting incomplete')
    require(len(analysis['functions']) == 170, 'Duplicate scalar analysis record')
    require(all(r['status'] in ('local_contract','unsupported') for r in analysis['functions']),
            'Scalar analysis contains unsupported acceptance status')
    accepted = [r for r in analysis['functions'] if r['status'] == 'local_contract']
    selected = [r['function'] for r in accepted]
    require(selected == bundle['functions'] and len(selected) == len(set(selected)),
            'Scalar acceptance differs from analyzed set')
    tested = {r['function']: r for r in validation['functions']}
    require(set(tested) == set(selected) and len(tested) == len(validation['functions']),
            'Scalar execution set mismatch')
    require(validation['passed'] is True, 'Scalar execution failed')
    runtime = validation['runtime']
    require(runtime['headers'], 'Missing scalar runtime dependencies')
    for path, expected in runtime['headers'].items():
        require(file_hash(Path(path)) == expected, 'Changed scalar runtime header: ' + path)
    require(file_hash(Path(runtime['compiler'])) == runtime['compiler_sha256'],
            'Changed scalar validation compiler')
    # The policy admits only tested local effects. No other status can flow through.
    for row in accepted:
        result = tested[row['function']]
        require(result['passed'] is True and result['invocations'] >= policy['minimum_invocations']
                and result['negative_controls'] >= policy['minimum_negative_controls'],
                'Insufficient scalar execution evidence')
        require(result['contract_sha256'] == digest(row), 'Tested scalar contract mismatch')
        require(result['body_sha'] == records[row['function']]['body_sha256'],
                'Tested scalar body mismatch')
    source_cache = {}
    before = sum(e['kind'] == 'function' and e['status'] == 'partial' for e in entities.values())
    for symbol in selected:
        record = records[symbol]
        path = record['source']
        if path not in source_cache:
            source_cache[path] = pin(path).read_text()
        require(sha(generated_body(source_cache[path], symbol).encode()) == record['body_sha256'],
                'Changed automated scalar body: ' + symbol)
        target = 'function:' + symbol
        require(target in entities and entities[target]['kind'] == 'function', 'Scalar outside tally census')
        entities[target]['status'] = 'partial'
        marker = 'scalar_pipeline:' + bundle['id']
        if marker not in entities[target]['evidence']:
            entities[target]['evidence'].append(marker)
    after = sum(e['kind'] == 'function' and e['status'] == 'partial' for e in entities.values())
    return dict(id=bundle['id'], accepted=len(selected), net_new_partial=after-before,
                unsupported=170-len(selected), disposition='partial')
