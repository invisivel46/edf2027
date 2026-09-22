"""Reproducible coverage accounting. Inventory rows are not completed contracts."""
import csv
import hashlib
import re
from collections import Counter
from pathlib import Path

from renderer_dispatch import digest, file_hash, local, read_json, require, verify_accepted


def rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def boundary_id(row, source_record):
    # CSV row numbers, actions and editorial wording are not identity.
    return 'boundary:' + digest(dict(source=row['source'], task=row['task'], record=source_record))


def accounting(start, discovered, resolved, reopened=0):
    require(min(start, discovered, resolved, reopened) >= 0, 'Negative accounting')
    remaining = start + discovered - resolved + reopened
    require(remaining >= 0, 'Resolutions exceed obligations')
    return dict(starting_unresolved=start, discovered=discovered, accepted_resolutions=resolved,
                reopened=reopened, remaining=remaining)


def accept_getters(root, acceptance, entities, pin):
    """Import an exact, reviewed getter set as partial knowledge only."""
    require(acceptance['disposition'] == 'partial', 'Getter acceptance may only be partial')
    required_artifacts = {acceptance[name] for name in ('report', 'manifest', 'rule_source',
        'facts', 'harness_source', 'harness_runtime_source', 'harness_report', 'offline_report')}
    require(required_artifacts <= set(acceptance['artifacts']),
            'Getter acceptance omits a required artifact pin')
    for path, expected in acceptance['artifacts'].items():
        require(file_hash(pin(path)) == expected, 'Stale getter artifact: ' + path)
    for evidence in acceptance['evidence_ids']:
        pin('knowledge/evidence/' + evidence + '.md')

    report = read_json(local(root, acceptance['report']))
    manifest = read_json(local(root, acceptance['manifest']))
    selected = acceptance['functions']
    require(len(selected) == acceptance['accepted_count'] == len(set(selected)),
            'Getter acceptance selection count/uniqueness changed')
    actual = [row['function'] for row in report['functions'] if row['status'] == 'local_contract']
    require(actual == selected, 'Getter acceptance differs from archived report')
    require(report['summary']['statuses'] == {'unsupported': 111, 'local_contract': 59},
            'Getter report status totals changed')
    require(report['summary']['rules'] == {'masked-load-return-v1': 40, 'load-return-v1': 19},
            'Getter report rule totals changed')
    require(report['parent_complete'] is False and report['tally_statuses_changed'] is False,
            'Getter report claims broader closure')
    require(file_hash(local(root, acceptance['manifest'])) == report['manifest_sha256'],
            'Getter report manifest pin changed')
    require(file_hash(local(root, acceptance['rule_source'])) == report['analyzer_sha256'],
            'Getter report rule pin changed')
    require(file_hash(local(root, acceptance['facts'])) == report['ghidra_sha256'],
            'Getter report facts pin changed')

    records = {row['function']: row for row in manifest['functions']}
    require(set(selected) <= set(records), 'Accepted getter missing from frozen manifest')
    source_cache = {}
    for symbol in selected:
        record = records[symbol]
        path = record['source']
        if path not in source_cache:
            source_cache[path] = local(root, path).read_text()
            pin(path)
        matches = list(re.finditer(r'^DEFINE_REX_FUNC\(' + re.escape(symbol)
                      + r'\) \{\n.*?^\}', source_cache[path], re.M | re.S))
        require(len(matches) == 1, 'Missing/duplicate accepted getter body: ' + symbol)
        require(hashlib.sha256(matches[0].group().encode()).hexdigest() == record['body_sha256'],
                'Changed accepted getter body: ' + symbol)
        target = 'function:' + symbol
        require(target in entities, 'Accepted getter outside function census')
        entities[target]['status'] = 'partial'
        marker = 'getter_acceptance:' + acceptance['id']
        if marker not in entities[target]['evidence']:
            entities[target]['evidence'].append(marker)

    harness = read_json(local(root, acceptance['harness_report']))
    require(harness['passed'] is True and harness['functions'] == len(selected),
            'Getter harness did not pass exact accepted set')
    offline = read_json(local(root, acceptance['offline_report']))
    require(offline['passed'] is True and len(offline['selected']) == 22
            and offline['game_booted'] is False, 'Getter final offline evidence changed')
    return sum(entities['function:' + symbol]['status'] == 'partial' for symbol in selected)


def accept_setters(root, acceptance, entities, pin):
    """Import an exact, reviewed historical setter set as partial knowledge only."""
    require(acceptance['disposition'] == 'partial', 'Setter acceptance may only be partial')
    required_artifacts = {acceptance[name] for name in ('report', 'manifest', 'selection',
        'rule_source', 'facts', 'harness_source', 'harness_runtime_source',
        'harness_report', 'offline_report')}
    require(required_artifacts <= set(acceptance['artifacts']),
            'Setter acceptance omits a required artifact pin')
    for path, expected in acceptance['artifacts'].items():
        require(file_hash(pin(path)) == expected, 'Stale setter artifact: ' + path)
    for evidence in acceptance['evidence_ids']:
        pin('knowledge/evidence/' + evidence + '.md')

    report = read_json(local(root, acceptance['report']))
    manifest = read_json(local(root, acceptance['manifest']))
    selection = read_json(local(root, acceptance['selection']))
    selected = acceptance['functions']
    require(len(selected) == acceptance['accepted_count'] == len(set(selected)),
            'Setter acceptance selection count/uniqueness changed')
    actual = [row['function'] for row in report['functions'] if row['status'] == 'local_contract']
    require(actual == selected, 'Setter acceptance differs from archived report')
    require(report['accepted_count'] == 24 and report['unsupported_count'] == 63,
            'Setter report status totals changed')
    require(Counter(row['rule'] for row in report['functions']
                    if row['status'] == 'local_contract') == {
                        'masked-store-u32-dirty-v1': 15,
                        'direct-store-u8-dirty-v1': 7,
                        'direct-store-u32-v1': 2}, 'Setter report rule totals changed')
    require(report['parent_complete'] is False and report['tally_statuses_changed'] is False,
            'Setter report claims broader closure')
    require(file_hash(local(root, acceptance['manifest'])) == report['manifest_sha256'],
            'Setter report manifest pin changed')
    require(file_hash(local(root, acceptance['selection'])) == report['selection_sha256'],
            'Setter report selection pin changed')
    require(file_hash(local(root, acceptance['rule_source'])) == report['analyzer_sha256'],
            'Setter report historical rule pin changed')
    require(file_hash(local(root, acceptance['facts'])) == report['ghidra_sha256'],
            'Setter report facts pin changed')
    require(selection['functions'] == [row['function'] for row in report['functions']],
            'Setter frozen candidate selection differs from archived report')
    require(selection['accepted_boundary_attestation']['function_count'] == len(selected)
            and selection['accepted_boundary_attestation']['all_decompile_completed'] is True
            and selection['accepted_boundary_attestation']['all_boundaries_match_frozen_inventory'] is True,
            'Setter selection boundary attestation changed')

    records = {row['function']: row for row in manifest['functions']}
    require(set(selected) <= set(records), 'Accepted setter missing from frozen manifest')
    source_cache = {}
    for symbol in selected:
        record = records[symbol]
        path = record['source']
        if path not in source_cache:
            source_cache[path] = local(root, path).read_text()
            pin(path)
        matches = list(re.finditer(r'^DEFINE_REX_FUNC\(' + re.escape(symbol)
                      + r'\) \{\n.*?^\}', source_cache[path], re.M | re.S))
        require(len(matches) == 1, 'Missing/duplicate accepted setter body: ' + symbol)
        require(hashlib.sha256(matches[0].group().encode()).hexdigest() == record['body_sha256'],
                'Changed accepted setter body: ' + symbol)
        target = 'function:' + symbol
        require(target in entities, 'Accepted setter outside function census')
        entities[target]['status'] = 'partial'
        marker = 'setter_acceptance:' + acceptance['id']
        if marker not in entities[target]['evidence']:
            entities[target]['evidence'].append(marker)

    harness = read_json(local(root, acceptance['harness_report']))
    require(harness['passed'] is True and harness['functions'] == len(selected)
            and harness['invocations'] == 14256 and harness['negative_controls'] == 214,
            'Setter harness did not pass exact accepted set')
    offline = read_json(local(root, acceptance['offline_report']))
    require(offline['passed'] is True and len(offline['selected']) == 24
            and offline['game_booted'] is False, 'Setter final offline evidence changed')
    return sum(entities['function:' + symbol]['status'] == 'partial' for symbol in selected)


def build(root, reconciliation):
    root = Path(root)
    inputs = {}
    def pin(name):
        path = local(root, name)
        inputs[name] = file_hash(path)
        return path
    catalog = read_json(pin('docs/renderer-coverage.json'))
    sources = {}
    for source in catalog['sources']:
        path = pin(source['path'])
        require(inputs[source['path']] == source['sha256'], 'Stale inventory: ' + source['path'])
        data = rows(path)
        require(len(data) == source['rows'], 'Inventory row count changed')
        sources[path.name] = data
    functions = sources['complete-function-inventory.csv']
    entities = {}
    for row in functions:
        key = 'function:' + row['function']
        require(key not in entities, 'Duplicate function')
        entities[key] = dict(kind='function', status='untriaged', task='R01.scope', evidence=[])
    # Reviewed local contracts establish partial knowledge, never whole-function closure.
    for name in reconciliation['partial_sources']:
        require(name in sources, 'Unknown partial evidence table')
        for row in sources[name]:
            freshness = 'catalog_only'
            for path_field, hash_field in (('decompilation', 'sha256'),
                    ('decompilation', 'decompilation_sha256'), ('source', 'source_sha256'),
                    ('source', 'reviewed_source_sha256'), ('source', 'sha256')):
                if row.get(path_field) and row.get(hash_field):
                    evidence_path = row[path_field].replace('\\', '/').split(':')[0]
                    require(file_hash(pin(evidence_path)) == row[hash_field],
                            'Stale local evidence: ' + evidence_path)
                    freshness = 'source_hash_verified'
            # Call-path reviews describe the hook body, not an unreviewed callee.
            field = {'retained-call-effect-reviews.csv': 'hook',
                     'renderable-content-routes.csv': 'method'}.get(name, 'function')
            require(field in row, 'Partial source lacks explicit function key: ' + name)
            symbol = row[field].removeprefix('__imp__')
            key = 'function:' + symbol
            if key in entities:
                entities[key]['status'] = 'partial'
                if name not in entities[key]['evidence']:
                    entities[key]['evidence'].append(name)
                entities[key].setdefault('evidence_freshness', {})[name] = freshness
    tasks = {t['id']: t for t in catalog['tasks']}
    for deferred in reconciliation.get('deferred_sources', []):
        require(deferred['task'] in tasks, 'Deferred evidence lacks task')
        pin(deferred['path'])
    for task in tasks.values():
        entities['task:' + task['id']] = dict(kind='task', status=task['status'],
            lane=task['execution']['kind'], dependencies=task['dependencies'],
            completion_test=task['completion_test'])
    boundary_rows = rows(pin('out/renderer-coverage/boundary-tasks.csv'))
    for row in boundary_rows:
        require(row['task'] in tasks, 'Boundary lacks task')
        require(row['status'] == 'open', 'Boundary closure needs explicit reconciliation')
        source_index = int(row['row']) - 2
        require(row['source'] in sources and 0 <= source_index < len(sources[row['source']]),
                'Boundary source row missing')
        key = boundary_id(row, sources[row['source']][source_index])
        record = dict(kind='boundary', status='open', task=row['task'], source=row['source'],
                      locator=row['locator'], subject=row['subject'],
                      action=row['action'], completion_test=row['completion_test'])
        if key in entities:
            require(entities[key] == record, 'Conflicting duplicate boundary')
        entities[key] = record
    for category in ('features', 'scenarios', 'configuration_modes'):
        for row in catalog[category]:
            links = row.get('tasks', [row.get('task')])
            require(links and all(t in tasks for t in links), 'Unassigned coverage item')
            key = category + ':' + row.get('id', row.get('name'))
            require(key not in entities, 'Duplicate coverage item')
            entities[key] = dict(kind=category, status=row['status'], tasks=links)
    events = []
    for accepted in reconciliation['accepted_slices']:
        ledger = read_json(pin(accepted['ledger']))
        item = ledger['slices'][accepted['slice']]
        require(item['status'] == 'integrated', 'Pilot not accepted and integrated')
        attempt = item['attempts'][-1]
        verify_accepted(ledger, attempt)
        result = attempt['result']
        require(result['parent_complete'] is False, 'Pilot unexpectedly closes parent')
        # Reuse only source evidence still matching the accepted baseline. Tools/docs
        # may evolve without invalidating the original instructions/native contracts.
        for name, expected in ledger['baseline']['inputs'].items():
            if name.startswith(('src/', 'generated/', 'out/renderer-inventory/')):
                require(file_hash(pin(name)) == expected, 'Stale accepted source: ' + name)
        for ref in result['artifacts'] + [attempt['review']['artifact']]:
            pin(ref['path'])
        for evidence in result['evidence_ids']:
            pin('knowledge/evidence/' + evidence + '.md')
        key = 'research:' + accepted['slice']
        require(key not in entities, 'Duplicate accepted slice')
        entities[key] = dict(kind='bounded_research', status='resolved', task=item['spec']['parent'],
                            scope=accepted['scope'], remaining=accepted['remaining'],
                            evidence=result['evidence_ids'], result_id=attempt['result_id'])
        for symbol in accepted['partial_functions']:
            target = 'function:' + symbol
            require(target in entities, 'Pilot function outside census')
            entities[target]['status'] = 'partial'
            entities[target]['evidence'].append(key)
        events.extend([dict(event='discover', id=key), dict(event='accepted_resolution', id=key,
                       review=attempt['review']['artifact'])])
    getter_acceptances = []
    seen_getter_acceptances = set()
    for name in reconciliation.get('accepted_getter_sets', []):
        acceptance = read_json(pin(name))
        require(acceptance['id'] not in seen_getter_acceptances, 'Duplicate getter acceptance')
        seen_getter_acceptances.add(acceptance['id'])
        before = sum(e['status'] == 'partial' for e in entities.values() if e['kind'] == 'function')
        accept_getters(root, acceptance, entities, pin)
        after = sum(e['status'] == 'partial' for e in entities.values() if e['kind'] == 'function')
        require(after - before == acceptance['expected_net_new_partial'],
                'Getter acceptance net-new partial count changed')
        getter_acceptances.append(dict(id=acceptance['id'], accepted=acceptance['accepted_count'],
                                       net_new_partial=after - before,
                                       disposition='partial'))
    setter_acceptances = []
    seen_setter_acceptances = set()
    for name in reconciliation.get('accepted_setter_sets', []):
        acceptance = read_json(pin(name))
        require(acceptance['id'] not in seen_setter_acceptances, 'Duplicate setter acceptance')
        seen_setter_acceptances.add(acceptance['id'])
        before = sum(e['status'] == 'partial' for e in entities.values() if e['kind'] == 'function')
        accept_setters(root, acceptance, entities, pin)
        after = sum(e['status'] == 'partial' for e in entities.values() if e['kind'] == 'function')
        require(after - before == acceptance['expected_net_new_partial'],
                'Setter acceptance net-new partial count changed')
        setter_acceptances.append(dict(id=acceptance['id'], accepted=acceptance['accepted_count'],
                                       net_new_partial=after - before,
                                       disposition='partial'))
    automated_scalar_sets = []
    if reconciliation.get('accepted_scalar_pipeline'):
        from renderer_scalar_acceptance import apply_bundle
        require(len(reconciliation['accepted_scalar_pipeline']) == 1, 'Multiple active scalar pipeline bundles')
        for name in reconciliation['accepted_scalar_pipeline']:
            automated_scalar_sets.append(apply_bundle(root, name, entities, pin))
    function_counts = Counter(e['status'] for e in entities.values() if e['kind'] == 'function')
    boundary_count = sum(e['kind'] == 'boundary' for e in entities.values())
    summary = dict(functions=dict(total=len(functions), **{s:function_counts[s] for s in
                   ('untriaged', 'partial', 'contract_understood', 'justified_exclusion')}),
                   boundary_rows=len(boundary_rows), unique_boundary_obligations=boundary_count,
                   obligation_accounting=accounting(boundary_count, len(events)//2, len(events)//2),
                   accepted_bounded_research=len(events)//2,
                   accepted_getter_sets=getter_acceptances,
                   accepted_setter_sets=setter_acceptances,
                   automated_scalar_sets=automated_scalar_sets,
                   parent_tasks_by_lane={lane: dict(Counter(t['status'] for t in entities.values()
                       if t['kind'] == 'task' and t['lane'] == lane))
                       for lane in sorted({t['execution']['kind'] for t in tasks.values()})},
                   scenarios=dict(Counter(r['status'] for r in catalog['scenarios'])),
                   features=len(catalog['features']), configuration_modes=len(catalog['configuration_modes']))
    payload = dict(version=1, inputs=inputs, reconciliation_id=digest(reconciliation),
                   deferred_evidence=reconciliation.get('deferred_sources', []),
                   summary=summary, entities=entities, events=events,
                   limitations=['Counts measure documented scope, not percent ported or rendering correctness.',
                     'Partial imports are table-level local evidence; transitive ownership and modes remain open.',
                     'Boundary obligations include instruction-level inventory; they are not equally sized work units.',
                     'Function, boundary, task and scenario counts overlap and must not be added.',
                     'New worker proposals require coordinator reconciliation before changing accepted totals.'])
    return dict(snapshot_id=digest(payload), **payload)
