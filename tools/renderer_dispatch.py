"""Coordinator-owned renderer queue. Content identity and structural gates, not semantic proof."""
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
MODELS = {'gpt-6-astra', 'gpt-5.6-terra', 'gpt-5.6-sol', 'gpt-5.6-luna', 'gpt-5.5'}
VOLATILE = {'generated/default/codegen.build.stamp'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def now():
    return datetime.now(timezone.utc).isoformat()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def file_hash(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def relative(value):
    require(isinstance(value, str) and value and '\\' not in value and ':' not in value,
            'Use canonical repository-relative forward-slash paths')
    p = PurePosixPath(value)
    require(not p.is_absolute() and '..' not in p.parts and str(p) == value and value != '.', 'Unsafe path: ' + value)
    return value


def local(root, value):
    path = (Path(root) / relative(value)).resolve()
    require(path.is_relative_to(Path(root).resolve()), 'Path escapes workspace: ' + value)
    return path


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE).decode('utf-8')


def source_manifest(root):
    """Include dirty and untracked source; exclude evidence/build outputs and volatile stamp."""
    names = git(root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').split('\0')
    names += git(root, 'ls-tree', '-r', '--name-only', '-z', 'HEAD').split('\0')
    result = {}
    for name in sorted(set(filter(None, names))):
        if name.startswith(('knowledge/', 'out/')) or name in VOLATILE:
            continue
        path = local(root, name)
        result[name] = file_hash(path) if path.is_file() else None
    require(bool(result), 'Empty source manifest')
    require(len({x.casefold() for x in result}) == len(result), 'Case-aliased source paths')
    return result


def evidence_exists(root, identifier):
    require(identifier.startswith('ev-') and '/' not in identifier and '\\' not in identifier, 'Expected Epistemic evidence ID')
    require((Path(root) / 'knowledge/evidence' / (identifier + '.md')).is_file(), 'Missing evidence: ' + identifier)


def verify_tally(root, spec):
    ref = spec.get('tally')
    if ref is None:
        return None
    require(isinstance(ref, dict) and set(ref) == {'path', 'sha256'}, 'Tally needs path and sha256')
    path = local(root, ref['path'])
    require(path.is_file() and file_hash(path) == ref['sha256'], 'Missing/changed tally: ' + ref['path'])
    snapshot = read_json(path)
    require(isinstance(snapshot, dict) and isinstance(snapshot.get('entities'), dict), 'Invalid tally snapshot')
    if 'snapshot_id' in snapshot:
        require(isinstance(snapshot['snapshot_id'], str) and bool(snapshot['snapshot_id'].strip()),
                'Invalid tally snapshot_id')
    for identifier, entity in snapshot['entities'].items():
        require(isinstance(identifier, str) and bool(identifier.strip()) and isinstance(entity, dict),
                'Invalid tally entity')
    return dict(ref=ref, snapshot=snapshot)


def verify_spec(spec, catalog, tally=None):
    tasks = {x['id']: x for x in catalog['tasks']}
    slices = spec['slices']
    ids = [x['id'] for x in slices]
    require(len(ids) == len(set(ids)), 'Duplicate slice ID')
    for item in slices:
        require(item['parent'] in tasks or item['parent'] is None, 'Unknown parent')
        require(item['kind'] in ('implementation', 'investigation', 'coordination'), 'Unknown kind')
        require(item['model'] in MODELS and item['effort'] in ('low', 'medium', 'high', 'xhigh'), 'Invalid model policy')
        for key in ('scope', 'excludes', 'inputs', 'cases', 'steps', 'stop_conditions'):
            require(bool(item[key]), 'Missing slice field: ' + key)
        case_ids = [x['id'] for x in item['cases']]
        require(len(case_ids) == len(set(case_ids)), 'Duplicate acceptance case')
        paths = item['allowed_files']
        require(len(paths) == len({p.casefold() for p in paths}), 'Case-aliased ownership')
        for name in paths + item['inputs']:
            relative(name)
        for name in paths:
            require(name.startswith(('src/', 'tests/')), 'Workers may own only explicit src/tests files')
        require(item['kind'] == 'implementation' or not paths, 'Research must not own source files')
        for dep in item['depends_on']:
            require(dep in ids and dep != item['id'], 'Unknown/self slice dependency')
        require(isinstance(item['required_evidence'], list), 'Missing prerequisite evidence list')
        if tally is not None:
            tally_ids = item.get('tally_ids')
            require(isinstance(tally_ids, list) and bool(tally_ids), 'Missing tally IDs')
            require(len(tally_ids) == len(set(tally_ids)), 'Duplicate tally ID')
            require(all(identifier in tally['snapshot']['entities'] for identifier in tally_ids),
                    'Unknown tally ID')
        if item['parent'] and tasks[item['parent']]['dependencies'] and item['kind'] == 'implementation':
            require(item['depends_on'] or item['required_evidence'], 'Dependent implementation lacks contract evidence')
    def visit(key, chain):
        require(key not in chain, 'Dependency cycle')
        for dep in next(x for x in slices if x['id'] == key)['depends_on']:
            visit(dep, chain + [key])
    for key in ids:
        visit(key, [])


def initialize(root, spec):
    catalog = read_json(Path(root) / 'docs/renderer-coverage.json')
    tally = verify_tally(root, spec)
    verify_spec(spec, catalog, tally)
    inputs = sorted({p for s in spec['slices'] for p in s['inputs']})
    if tally is not None:
        inputs.append(tally['ref']['path'])
        inputs = sorted(set(inputs))
    hashes = {p: file_hash(local(root, p)) for p in inputs}
    source = source_manifest(root)
    baseline = dict(head=git(root, 'rev-parse', 'HEAD').strip(), source=source, inputs=hashes)
    baseline['id'] = digest(baseline)
    ledger = dict(version=1, coordinator=spec['coordinator'], root=str(Path(root).resolve()),
                baseline=baseline, created=now(), revision=0,
                parents={t['id']: t for t in catalog['tasks'] if t['id'] in {s['parent'] for s in spec['slices']}},
                slices={s['id']: dict(spec=s, status='pending', attempts=[]) for s in spec['slices']}, events=[])
    if tally is not None:
        ledger['tally'] = tally
    return ledger


def unchanged_inputs(ledger, workspace, allow=(), names=None):
    for name, expected in ledger['baseline']['inputs'].items():
        if name in allow or (names is not None and name not in names):
            continue
        path = local(workspace, name)
        require(path.is_file() and file_hash(path) == expected, 'Stale input: ' + name)


def slice_inputs(ledger, spec):
    names = set(spec['inputs'])
    if 'tally' in ledger:
        names.add(ledger['tally']['ref']['path'])
    return names


def readiness(ledger, key):
    item = ledger['slices'][key]
    require(item['status'] in ('pending', 'blocked', 'needs_revision', 'ready'), 'Slice cannot become ready')
    require(source_manifest(ledger['root']) == ledger['baseline']['source'], 'Coordinator baseline changed; initialize a new ledger')
    unchanged_inputs(ledger, ledger['root'])
    for dep in item['spec']['depends_on']:
        other = ledger['slices'][dep]
        require(other['status'] == 'integrated', 'Unintegrated dependency: ' + dep)
        require(bool(other['attempts'][-1]['result']['evidence_ids']), 'Dependency has no contract evidence')
        for ev in other['attempts'][-1]['result']['evidence_ids']:
            evidence_exists(ledger['root'], ev)
        verify_accepted(ledger, other['attempts'][-1])
    for ev in item['spec']['required_evidence']:
        evidence_exists(ledger['root'], ev)
    item['status'] = 'ready'


def assign(ledger, key, owner, workspace):
    require(owner and owner != ledger['coordinator'], 'Worker must differ from coordinator')
    item = ledger['slices'][key]
    require(item['status'] == 'ready', 'Slice is not ready')
    readiness(ledger, key)
    workspace = Path(workspace).resolve()
    root = Path(ledger['root']).resolve()
    if item['spec']['kind'] == 'implementation':
        require(workspace != root, 'Implementation requires an isolated checkout')
    require(git(workspace, 'rev-parse', 'HEAD').strip() == ledger['baseline']['head'], 'Wrong checkout HEAD')
    require(source_manifest(workspace) == ledger['baseline']['source'], 'Checkout differs from pinned dirty baseline')
    unchanged_inputs(ledger, workspace)
    owned = {x.casefold() for x in item['spec']['allowed_files']}
    for other in ledger['slices'].values():
        if other['status'] not in ('assigned', 'submitted', 'reviewed'):
            continue
        attempt = other['attempts'][-1]
        require(owner != attempt['owner'], 'Owner already has an active assignment')
        require(not owned.intersection(p.casefold() for p in other['spec']['allowed_files']), 'Concurrent write ownership overlap')
        if item['spec']['kind'] == 'implementation' or other['spec']['kind'] == 'implementation':
            require(str(workspace) != attempt['workspace'], 'Implementation checkout already in use')
    attempt = dict(id=uuid.uuid4().hex, owner=owner, workspace=str(workspace), assigned=now())
    attempt['output'] = 'out/renderer-dispatch/results/' + attempt['id']
    attempt['packet_id'] = digest(dict(spec=item['spec'], baseline=ledger['baseline']['id'], attempt=attempt))
    item['attempts'].append(attempt)
    item['status'] = 'assigned'
    return attempt


def artifact(workspace, ref):
    require(isinstance(ref, dict) and set(ref) == {'path', 'sha256'}, 'Artifact needs path and sha256')
    path = local(workspace, ref['path'])
    require(path.is_file() and file_hash(path) == ref['sha256'], 'Missing/changed artifact: ' + ref['path'])
    return path


def validate_offline(report, source_id):
    require(report.get('passed') is True and report.get('suite') == 'all', 'Full offline gate not passed')
    require(report.get('source_id_start') == source_id == report.get('source_id_end'), 'Offline report does not bind tested source')
    require(report.get('commands') and all(c['exit_code'] == 0 for c in report['commands']), 'Offline command failure')
    require(report.get('selected') and sorted(t['name'] for t in report.get('tests', [])) == sorted(report['selected']), 'Offline results incomplete')
    require(len(report['selected']) == len(set(report['selected'])), 'Duplicate offline tests')
    from renderer_offline_suites import CPU, RENDER
    require(sorted(report['selected']) == sorted(CPU + RENDER), 'Offline gate omitted required suites')
    require(all(t.get('status') == 'run' for t in report['tests']), 'Offline test skipped')


def validate_result(ledger, key, result):
    item = ledger['slices'][key]
    require(bool(item['attempts']), 'No assigned attempt')
    attempt = item['attempts'][-1]
    spec = item['spec']
    for field, expected in (('slice_id', key), ('attempt_id', attempt['id']), ('owner', attempt['owner']),
                            ('packet_id', attempt['packet_id']), ('baseline_id', ledger['baseline']['id'])):
        require(result.get(field) == expected, 'Result identity mismatch: ' + field)
    require(result.get('disposition') == 'slice_complete', 'Only complete slices can be submitted; use block for handoffs')
    require(result.get('parent_complete') is False, 'Slice cannot complete its parent')
    workspace = Path(attempt['workspace'])
    current = source_manifest(workspace)
    source_id = digest(current)
    require(result.get('source_id') == source_id, 'Result source fingerprint is stale')
    baseline = ledger['baseline']['source']
    changed = sorted(p for p in set(current) | set(baseline) if current.get(p) != baseline.get(p))
    require(result.get('changed_files') == changed, 'Changed-file declaration differs from actual checkout')
    require(set(changed) <= set(spec['allowed_files']), 'Out-of-scope source mutation')
    unchanged_inputs(ledger, workspace, spec['allowed_files'], slice_inputs(ledger, spec))
    require(result.get('evidence_ids'), 'No evidence IDs')
    evidence_ids = result['evidence_ids']
    require(len(evidence_ids) == len(set(evidence_ids)), 'Duplicate evidence ID')
    for ev in result['evidence_ids']:
        evidence_exists(ledger['root'], ev)
    if 'tally' in ledger:
        assigned = spec['tally_ids']
        updates = result.get('tally_updates')
        require(isinstance(updates, list), 'Missing tally updates')
        require(all(isinstance(update, dict) and set(update) == {'id', 'disposition', 'evidence_ids', 'details'}
                    for update in updates), 'Invalid tally update')
        update_ids = [update['id'] for update in updates]
        require(len(update_ids) == len(set(update_ids)) and set(update_ids) == set(assigned),
                'Tally updates must cover exactly assigned IDs')
        for update in updates:
            require(update['disposition'] in ('unchanged', 'partial', 'resolved') and
                    isinstance(update['evidence_ids'], list) and
                    len(update['evidence_ids']) == len(set(update['evidence_ids'])) and
                    isinstance(update['details'], str) and bool(update['details'].strip()),
                    'Invalid tally update')
            if update['disposition'] in ('partial', 'resolved'):
                require(bool(update['evidence_ids']) and set(update['evidence_ids']) <= set(evidence_ids),
                        'Tally resolution evidence must be result evidence')
        discoveries = result.get('discoveries', [])
        require(isinstance(discoveries, list), 'Invalid discoveries')
        require(all(isinstance(entry, dict) and set(entry) == {'id', 'details', 'evidence_ids'}
                    for entry in discoveries), 'Invalid discovery')
        discovery_ids = [entry['id'] for entry in discoveries]
        require(len(discovery_ids) == len(set(discovery_ids)), 'Duplicate discovery ID')
        require(not set(discovery_ids).intersection(ledger['tally']['snapshot']['entities']),
                'Discovery ID already exists')
        for entry in discoveries:
            require(isinstance(entry['id'], str) and bool(entry['id'].strip()) and
                    isinstance(entry['details'], str) and bool(entry['details'].strip()) and
                    isinstance(entry['evidence_ids'], list) and
                    len(entry['evidence_ids']) == len(set(entry['evidence_ids'])) and
                    set(entry['evidence_ids']) <= set(evidence_ids), 'Invalid discovery')
    session = result.get('epistemic_session', '')
    require(session.startswith('session-') and '/' not in session and '\\' not in session and
            (Path(ledger['root']) / 'knowledge/journal' / (session + '.md')).is_file(), 'Missing Epistemic session')
    artifacts = result.get('artifacts', [])
    require(artifacts, 'No result artifacts')
    for ref in artifacts:
        require(ref['path'].startswith(attempt['output'] + '/'), 'Result artifact outside assigned output directory')
        artifact(workspace, ref)
    cases = result.get('cases', {})
    require(set(cases) == {x['id'] for x in spec['cases']}, 'Missing/extra acceptance cases')
    for case in spec['cases']:
        outcome = cases[case['id']]
        expected = 'not_run' if case.get('runtime') else 'passed'
        require(outcome.get('status') == expected and bool(outcome.get('details')), 'Unmet acceptance case: ' + case['id'])
        require(outcome.get('artifact') in [r['path'] for r in artifacts], 'Acceptance case lacks artifact')
    if spec['kind'] == 'implementation':
        report = read_json(artifact(workspace, result['offline_report']))
        validate_offline(report, source_id)
    checks = result.get('checks', {})
    require(set(checks) == set(spec['checks']), 'Missing/extra supplemental checks')
    for name, expected in spec['checks'].items():
        check = checks[name]
        require(check.get('command') == expected and check.get('exit_code') == 0, 'Failed/wrong command: ' + name)
        artifact(workspace, check['log'])
    return source_id


def submit(ledger, key, result):
    item = ledger['slices'][key]
    require(item['status'] == 'assigned', 'Slice is not assigned')
    validate_result(ledger, key, result)
    item['attempts'][-1]['result'] = result
    item['attempts'][-1]['result_id'] = digest(result)
    current = source_manifest(item['attempts'][-1]['workspace'])
    item['attempts'][-1]['changed_hashes'] = {p: current.get(p) for p in result['changed_files']}
    item['attempts'][-1]['evidence_hashes'] = {ev: file_hash(Path(ledger['root']) / 'knowledge/evidence' / (ev + '.md'))
                                            for ev in result['evidence_ids']}
    item['status'] = 'submitted'


def review(ledger, key, reviewer, review_ref):
    item = ledger['slices'][key]
    require(item['status'] == 'submitted', 'Slice not submitted')
    attempt = item['attempts'][-1]
    require(reviewer and reviewer != attempt['owner'], 'Independent reviewer required')
    validate_result(ledger, key, attempt['result'])
    record = read_json(artifact(attempt['workspace'], review_ref))
    require(record.get('reviewer') == reviewer and record.get('result_id') == attempt['result_id'] and
            record.get('packet_id') == attempt['packet_id'], 'Review identity mismatch')
    require(record.get('decision') == 'accept' and record.get('contract_checked') is True and
            record.get('oracle_checked') is True and bool(record.get('details')), 'Review has not accepted contract/oracle')
    attempt['review'] = dict(reviewer=reviewer, artifact=review_ref)
    item['status'] = 'reviewed'


def integrate(ledger, key, report_ref=None):
    item = ledger['slices'][key]
    require(item['status'] == 'reviewed', 'Slice not independently reviewed')
    attempt = item['attempts'][-1]
    validate_result(ledger, key, attempt['result'])
    artifact(attempt['workspace'], attempt['review']['artifact'])
    result = attempt['result']
    root = Path(ledger['root'])
    worker = Path(attempt['workspace'])
    current = source_manifest(root)
    unchanged_inputs(ledger, root, item['spec']['allowed_files'], slice_inputs(ledger, item['spec']))
    permitted = set(result['changed_files'])
    for other in ledger['slices'].values():
        if other['status'] == 'integrated': permitted.update(other['attempts'][-1]['result']['changed_files'])
    baseline = ledger['baseline']['source']
    root_changes = {p for p in set(current) | set(baseline) if current.get(p) != baseline.get(p)}
    require(root_changes <= permitted, 'Coordinator has changes outside reviewed integration scope')
    worker_source = source_manifest(worker)
    for name in result['changed_files']:
        require(current.get(name) == worker_source.get(name), 'Patch not integrated: ' + name)
    if item['spec']['kind'] == 'implementation':
        require(report_ref is not None, 'Integration requires a fresh root offline report')
        report = read_json(artifact(root, report_ref))
        validate_offline(report, digest(current))
    attempt['integration'] = dict(at=now(), source_id=digest(current), offline_report=report_ref)
    item['status'] = 'integrated'


def verify_accepted(ledger, attempt):
    require(digest(attempt['result']) == attempt['result_id'], 'Accepted result changed')
    for ref in attempt['result']['artifacts']:
        artifact(attempt['workspace'], ref)
    artifact(attempt['workspace'], attempt['review']['artifact'])
    for ev, expected in attempt['evidence_hashes'].items():
        require(file_hash(Path(ledger['root']) / 'knowledge/evidence' / (ev + '.md')) == expected,
                'Accepted dependency evidence changed: ' + ev)


def advance(ledger, reason):
    """Advance only across reviewed, integrated patches with no in-flight assignments."""
    require(reason.strip(), 'Baseline advance needs an applicability review reason')
    require(not any(s['status'] in ('assigned', 'submitted', 'reviewed') for s in ledger['slices'].values()),
            'Cannot advance with active assignments')
    expected = dict(ledger['baseline']['source'])
    for item in ledger['slices'].values():
        if item['status'] != 'integrated':
            continue
        attempt = item['attempts'][-1]
        verify_accepted(ledger, attempt)
        expected.update(attempt['changed_hashes'])
    current = source_manifest(ledger['root'])
    require({k: v for k, v in expected.items() if v is not None} == {k: v for k, v in current.items() if v is not None},
            'Baseline advance contains changes outside integrated results; use a new campaign')
    refreshed_spec = dict(coordinator=ledger['coordinator'], slices=[s['spec'] for s in ledger['slices'].values()])
    if 'tally' in ledger:
        refreshed_spec['tally'] = ledger['tally']['ref']
    refreshed = initialize(ledger['root'], refreshed_spec)
    ledger.setdefault('baseline_history', []).append(dict(baseline=ledger['baseline'], at=now(), reason=reason))
    ledger['baseline'] = refreshed['baseline']
    ledger['parents'] = refreshed['parents']
    for item in ledger['slices'].values():
        if item['status'] == 'ready': item['status'] = 'pending'


@contextmanager
def locked(path):
    lock = Path(str(path) + '.lock')
    lock.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise ValueError('Coordinator ledger is locked; investigate its owner before removing ' + str(lock))
    try:
        os.write(descriptor, (str(os.getpid()) + '\n').encode())
        os.close(descriptor)
        yield
    finally:
        lock.unlink()


def packet(ledger, key):
    from renderer_task_packet import render_task
    item = ledger['slices'][key]
    spec = item['spec']
    attempt = item['attempts'][-1] if item['attempts'] else None
    text = ['# Renderer dispatch packet: ' + key, '',
            'DRAFT — not assigned; do not execute.' if not attempt else 'Assigned attempt: ' + attempt['id'], '',
            'Model: ' + spec['model'] + '; effort: ' + spec['effort'],
            'Baseline: ' + ledger['baseline']['id'], 'State: ' + item['status'], '',
            '## Slice scope', '', spec['scope'], '', 'Excluded: ' + spec['excludes'], '',
            '## Steps', '']
    text += [f'{i}. {step}' for i, step in enumerate(spec['steps'], 1)]
    text += ['', '## Acceptance', ''] + ['- ' + c['id'] + ': ' + c['description'] +
             (' [milestone-only: report not_run]' if c.get('runtime') else '') for c in spec['cases']]
    text += ['', '## Ownership and identity', '', 'Allowed source edits: ' + (', '.join(spec['allowed_files']) or 'None'),
             'Dependencies: ' + str(spec['depends_on']), 'Required evidence: ' + str(spec['required_evidence'])]
    if 'tally' in ledger:
        text += ['Assigned tally IDs: ' + ', '.join(spec['tally_ids']),
                 'Report one tally_update per assigned ID; discoveries are additive and do not close assigned entities.']
    if attempt:
        text += ['Workspace: ' + attempt['workspace'], 'Output directory: ' + attempt['output'],
                 'Owner: ' + attempt['owner'], 'Packet ID: ' + attempt['packet_id']]
    text += ['', 'Input SHA-256:', ''] + ['- ' + p + ': ' + ledger['baseline']['inputs'][p] for p in spec['inputs']]
    text += ['', '## Stop and report', ''] + ['- ' + s for s in spec['stop_conditions']]
    text += ['', 'Do not edit the ledger, generated coverage, or parent status. Do not spawn further agents.',
             'Use a unique Epistemic session; submit evidence IDs and explicit unknowns.',
             'Never infer completion from a worker message. Coordinator and independent reviewer accept artifacts.',
             'No game boot in this slice. Serialize GPU validation through the coordinator.',
             'See docs/renderer-dispatch.md for result/review schemas and submission commands.', '',
             'Required supplemental commands:', '```json', json.dumps(spec['checks'], indent=2), '```']
    if spec['kind'] == 'implementation':
        text += ['', 'Run .\\validate-renderer-offline.cmd in this checkout; retain its complete report.']
    if spec['parent']:
        parent = ledger['parents'][spec['parent']]
        text += ['', '## Parent context (broader than this assignment)', '', render_task(parent)]
    return '\n'.join(text) + '\n'
