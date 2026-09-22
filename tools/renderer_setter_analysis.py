"""Bounded, fail-closed scalar setter contracts over a pre-frozen corpus."""
import argparse
import hashlib
import json
from pathlib import Path

import renderer_scalar_analysis as getter

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'tests/fixtures/renderer-scalar-pilot.json'
CANDIDATES = ROOT / 'tests/fixtures/renderer-setter-pilot.json'
FAMILY_V2 = ROOT / 'tests/fixtures/renderer-setter-family-v2.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def signed16(value):
    return value - 0x10000 if value & 0x8000 else value


def decode(word):
    word = int(word, 16) if isinstance(word, str) else word
    require(0 <= word <= 0xffffffff, 'Invalid instruction word')
    op = word >> 26
    result = dict(word=f'{word:08X}', primary_opcode=op, supported=False,
                  record_condition=False)
    if op in (32, 36, 38):
        names = {32: 'load_u32_be', 36: 'store_u32_be', 38: 'store_u8'}
        result.update(operation=names[op], register=(word >> 21) & 31,
                      base=(word >> 16) & 31, displacement=signed16(word & 0xffff),
                      supported=True)
    elif op == 20:
        result.update(operation='rotate_insert_u32', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31, shift=(word >> 11) & 31,
                      mask_begin=(word >> 6) & 31, mask_end=(word >> 1) & 31,
                      record_condition=bool(word & 1), supported=True)
    elif op == 21:
        result.update(operation='rotate_mask_u32', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31, shift=(word >> 11) & 31,
                      mask_begin=(word >> 6) & 31, mask_end=(word >> 1) & 31,
                      record_condition=bool(word & 1), supported=True)
    elif op == 14:
        result.update(operation='add_immediate', destination=(word >> 21) & 31,
                      base=(word >> 16) & 31, immediate=signed16(word & 0xffff),
                      supported=True)
    elif op == 24:
        result.update(operation='or_immediate', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31, immediate=word & 0xffff,
                      supported=True)
    elif op in (58, 62) and (word & 3) == 0:
        result.update(operation='load_u64_be' if op == 58 else 'store_u64_be',
                      register=(word >> 21) & 31, base=(word >> 16) & 31,
                      displacement=(word & 0xfffc) - (0x10000 if word & 0x8000 else 0),
                      supported=True)
    elif op == 30 and ((word >> 2) & 7) == 1:
        result.update(operation='rotate_left_clear_right_u64', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31,
                      shift=((word >> 11) & 31) | (((word >> 1) & 1) << 5),
                      mask_end=((word >> 6) & 31) | (((word >> 5) & 1) << 5),
                      record_condition=bool(word & 1), supported=True)
    elif op == 31 and ((word >> 1) & 0x3ff) == 444:
        result.update(operation='or_register', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31, other=(word >> 11) & 31,
                      record_condition=bool(word & 1), supported=True)
    elif word == 0x4e800020:
        result.update(operation='return_lr', supported=True)
    else:
        result['operation'] = 'unsupported'
    return result


def load_inputs(manifest_path=MANIFEST, candidate_path=CANDIDATES):
    manifest_bytes = Path(manifest_path).read_bytes()
    manifest = getter.load_manifest(manifest_path)
    frozen = json.loads(Path(candidate_path).read_text())
    require(hashlib.sha256(manifest_bytes).hexdigest() == frozen['source_manifest_sha256'],
            'Frozen scalar source manifest hash drift')
    require(frozen['source_function_count'] == manifest['function_count'], 'Setter source count drift')
    require(frozen['excluded_getter_count'] == 59, 'Getter exclusion count drift')
    require(frozen['getter_unsupported_count'] == 111, 'Unsupported source count drift')
    require(frozen['raw_pcode_store_count'] == 96 and
            frozen['raw_store_without_natural_store_count'] == 9,
            'Raw/natural store accounting drift')
    names = frozen['functions']
    require(len(names) == frozen['candidate_count'] == 87, 'Incomplete setter candidate freeze')
    require(len(set(names)) == len(names), 'Duplicate setter candidate')
    indexed = {r['function']: r for r in manifest['functions']}
    require(all(n in indexed for n in names), 'Setter candidate absent from scalar freeze')
    require(all(getter.classify(indexed[n])['status'] == 'unsupported' for n in names),
            'Accepted getter included in setter candidates')
    return manifest, frozen, [indexed[n] for n in names]


def validate_frozen_attestation(frozen, records):
    accepted = [r for r in records if classify(r).get('rule', '').endswith('-v1')]
    attested = frozen['accepted_boundary_attestation']
    digest = hashlib.sha256(''.join(i['address'] + i['word'] for r in accepted
                                    for i in r['instructions']).encode()).hexdigest()
    require(len(accepted) == attested['function_count'], 'Accepted setter count differs from attestation')
    require(attested['all_decompile_completed'] and
            attested['all_boundaries_match_frozen_inventory'], 'Incomplete boundary attestation')
    require(digest == attested['ordered_address_word_sha256'],
            'Accepted setter bodies differ from boundary attestation')


def load_family_v2(path=FAMILY_V2):
    family = json.loads(Path(path).read_text())
    require(family['version'] == 2 and family['rule'] == 'packed-nibble-store-dirty-v2',
            'Unexpected setter family attestation')
    require(family['function_count'] == len(family['functions']) == 12,
            'Incomplete setter family attestation')
    require(len(set(family['functions'])) == 12, 'Duplicate setter family member')
    require(len(family['body_sha256']) == 12 and
            set(family['body_sha256']) == set(family['functions']),
            'Incomplete setter family body attestation')
    require(len(family['instruction_body_sha256']) == 12 and
            set(family['instruction_body_sha256']) == set(family['functions']),
            'Incomplete Ghidra body attestation')
    require(len(family['canonical_boundaries']) == 12 and
            set(family['canonical_boundaries']) == set(family['functions']),
            'Incomplete setter family boundary attestation')
    return family


def validate_family_v2(family, records):
    by_name = {r['function']: r for r in records}
    require(all(n in by_name for n in family['functions']), 'Setter family member absent from corpus')
    for name in family['functions']:
        record = by_name[name]
        require(record['body_sha256'] == family['body_sha256'][name],
                'Setter family generated body hash drift: ' + name)
        require([i['address'] for i in record['instructions']] ==
                family['canonical_boundaries'][name], 'Setter family boundary drift: ' + name)
        body_digest = hashlib.sha256(''.join(i['address'] + i['word']
                                             for i in record['instructions']).encode()).hexdigest()
        require(body_digest == family['instruction_body_sha256'][name],
                'Setter family Ghidra body drift: ' + name)
    digest = hashlib.sha256(''.join(i['address'] + i['word'] for n in family['functions']
                                    for i in by_name[n]['instructions']).encode()).hexdigest()
    require(digest == family['ordered_address_word_sha256'],
            'Setter family instruction body drift')
    return set(family['functions'])


def _base_result(record, facts):
    return dict(function=record['function'], partition=record['partition'], status='unsupported',
                reason='Outside the three complete-body setter rules', facts=facts)


def classify(record):
    entry = int(record['function'].removeprefix('sub_'), 16)
    facts = [dict(address=i['address'], **decode(i['word'])) for i in record['instructions']]
    result = _base_result(record, facts)
    if any(int(f['address'], 16) != entry + 4 * i for i, f in enumerate(facts)):
        result['reason'] = 'Noncontiguous body or incorrect entry'
        return result
    if any(f['record_condition'] for f in facts):
        result['reason'] = 'Condition-register effects are unsupported'
        return result
    rule = None
    contract = None
    ops = [f['operation'] for f in facts]
    if ops == ['store_u32_be', 'return_lr']:
        store = facts[0]
        if store['base'] == 3 and store['register'] == 4 and store['displacement'] % 4 == 0:
            rule = 'direct-store-u32-v1'
            contract = _contract(store, 32, [], changed=[])
    elif ops == ['store_u8', 'load_u64_be', 'or_immediate', 'store_u64_be', 'return_lr']:
        store, load, ori, dirty = facts[:4]
        if (store['base'], store['register']) == (3, 4) and _dirty_tail(load, ori, dirty):
            rule = 'direct-store-u8-dirty-v1'
            contract = _contract(store, 8, [load, ori, dirty], changed=[11])
    elif ops == ['load_u32_be', 'rotate_insert_u32', 'store_u32_be',
                 'load_u64_be', 'or_immediate', 'store_u64_be', 'return_lr']:
        load32, merge, store, load, ori, dirty = facts[:6]
        if (load32['base'] == store['base'] == 3 and
                load32['displacement'] == store['displacement'] and
                load32['register'] == 11 and store['register'] == merge['destination'] and
                merge['source'] in (4, 11) and merge['destination'] in (4, 11) and
                merge['source'] != merge['destination'] and store['displacement'] % 4 == 0 and
                _dirty_tail(load, ori, dirty)):
            rule = 'masked-store-u32-dirty-v1'
            changed = sorted({11, merge['destination']})
            contract = _contract(store, 32, [load, ori, dirty], changed=changed)
            contract.update(load_register=11, merge_destination=merge['destination'],
                            merge_source=merge['source'], shift=merge['shift'],
                            mask_begin=merge['mask_begin'], mask_end=merge['mask_end'])
    elif ops == ['load_u32_be', 'rotate_mask_u32', 'add_immediate', 'rotate_mask_u32',
                 'rotate_left_clear_right_u64', 'or_register', 'store_u32_be',
                 'load_u64_be', 'or_register', 'store_u64_be', 'return_lr']:
        load32, input_mask, li, old_mask, dirty_bit, merge, store, load64, dirty_or, store64 = facts[:10]
        family = load_family_v2()
        instruction_digest = hashlib.sha256(''.join(i['address'] + i['word']
                                                     for i in record['instructions']).encode()).hexdigest()
        if (record['function'] in family['functions'] and
                instruction_digest == family['instruction_body_sha256'][record['function']] and
                load32['base'] == store['base'] == load64['base'] == store64['base'] == 3 and
                load32['register'] == old_mask['source'] == old_mask['destination'] == 10 and
                load32['displacement'] == store['displacement'] and load32['displacement'] % 4 == 0 and
                input_mask['source'] == 4 and input_mask['destination'] == 11 and
                li['destination'] == 12 and li['base'] == 0 and li['immediate'] == 1 and
                dirty_bit['source'] == dirty_bit['destination'] == 12 and
                dirty_bit['shift'] == 45 and dirty_bit['mask_end'] == 63 and
                merge['source'] == 10 and merge['destination'] == merge['other'] == 11 and
                store['register'] == 11 and load64['register'] == store64['register'] == 11 and
                load64['displacement'] == store64['displacement'] and load64['displacement'] % 8 == 0 and
                dirty_or['source'] == dirty_or['destination'] == 11 and dirty_or['other'] == 12):
            rule = 'packed-nibble-store-dirty-v2'
            dirty_mask = ((1 << 64) - 1 if dirty_bit['mask_end'] == 63 else
                          ((1 << (dirty_bit['mask_end'] + 1)) - 1) << (63 - dirty_bit['mask_end']))
            dirty_mask &= ((1 << 64) - 1)
            rotated = 1 << dirty_bit['shift']
            contract = _contract(store, 32, [load64, dict(immediate=rotated & dirty_mask), store64],
                                 changed=[10, 11, 12])
            contract.update(first_shift=input_mask['shift'], first_mb=input_mask['mask_begin'],
                            first_me=input_mask['mask_end'], second_shift=old_mask['shift'],
                            second_mb=old_mask['mask_begin'], second_me=old_mask['mask_end'],
                            li_immediate=li['immediate'], dirty_shift=dirty_bit['shift'],
                            dirty_mask_end=dirty_bit['mask_end'])
    if contract is None:
        return result
    result.update(status='local_contract', reason='', rule=rule, contract=contract)
    return result


def _dirty_tail(load, ori, store):
    return (load['register'] == ori['source'] == ori['destination'] == store['register'] == 11 and
            load['base'] == store['base'] == 3 and load['displacement'] == store['displacement'] and
            load['displacement'] % 8 == 0 and ori['immediate'] != 0)


def _contract(store, width, dirty, changed):
    writes = [dict(displacement=store['displacement'], width=width, endian='big',
                   source_register=store['register'])]
    dirty_disp = 0
    dirty_mask = 0
    if dirty:
        dirty_disp, dirty_mask = dirty[0]['displacement'], dirty[1]['immediate']
        writes.append(dict(displacement=dirty_disp, width=64, endian='big',
                           operation='old_value_or_immediate', immediate=dirty_mask))
    return dict(input_receiver_register=3, input_value_register=4,
                value_displacement=store['displacement'], value_width=width,
                value_source_register=store['register'], dirty_displacement=dirty_disp,
                dirty_or_mask=dirty_mask, writes_ordered=writes, changed_registers=changed,
                calls=[], condition_register_changes=[],
                alias_model='Writes execute in listed order; later loads observe earlier overlapping writes',
                preconditions=['Effective addresses use low32(entry_r3) plus signed displacement modulo 2^32',
                               '32/64-bit accesses are respectively 4/8-byte aligned',
                               'All listed bytes are mapped writable ordinary guest memory; no MMIO or physical-alias semantics',
                               'Valid caller return context; no concurrent mutation during invocation'])


def validate_candidates(frozen, ghidra):
    selected = {r['function'] for r in ghidra.values()
                if any(p['opcode'] == 'STORE' for p in r.get('high_pcode', []))}
    require(set(frozen['functions']) == selected, 'Pre-rule setter candidate selection drift')


def analyze(records, ghidra=None):
    results = []
    for record in records:
        item = classify(record)
        if ghidra is not None:
            row = ghidra[record['function']]
            item['high_pcode_available'] = row['decompile_completed']
            item['boundary_matches_inventory'] = row['boundary_matches_inventory']
            item['additional_body_addresses'] = row['additional_body_addresses']
            if item['status'] == 'local_contract':
                require(row['decompile_completed'], 'Accepted setter lacks high p-code')
                require(row['boundary_matches_inventory'], 'Accepted setter boundary differs')
                require(not any(p['opcode'] == 'CALL' for p in row.get('high_pcode', [])),
                        'Accepted setter contains a call')
        results.append(item)
    return results


def emit_cpp(records, output, root=ROOT):
    manifest, frozen, canonical = load_inputs()
    require([r['function'] for r in records] == frozen['functions'],
            'Emitter requires the exact frozen setter candidate order')
    require(records == canonical, 'Emitter candidate bodies differ from the frozen scalar corpus')
    validate_frozen_attestation(frozen, records)
    validate_family_v2(load_family_v2(), records)
    sources = getter.validate_sources(manifest, root)
    accepted = [r for r in analyze(records) if r['status'] == 'local_contract']
    by_name = {r['function']: r for r in records}
    rule_ids = {'masked-store-u32-dirty-v1': 1, 'direct-store-u8-dirty-v1': 2,
                'direct-store-u32-v1': 3, 'packed-nibble-store-dirty-v2': 4}
    chunks = ['// Exact generated guest bodies; instruction semantics are unchanged.']
    for result in accepted:
        record = by_name[result['function']]
        chunks.append(getter.generated_body(sources[record['source']], record['function']))
    chunks.append('static const SetterCase kSetterCases[] = {')
    for result in accepted:
        c = result['contract']
        regs = (c['changed_registers'] + [0, 0, 0])[:3]
        chunks.append(('  {"%s", &__imp__%s, %d, %d, %d, %d, %d, UINT64_C(0x%X), '
                       '%d, {%d, %d, %d}, %s, %d, %d, %d, %d, %d, %d, '
                       '%d, %d, %d, %d, %d, %d, %d, %d, %d},') % (
            result['function'], result['function'], rule_ids[result['rule']],
            c['value_displacement'], c['value_width'] // 8, c['value_source_register'],
            c['dirty_displacement'], c['dirty_or_mask'], len(c['changed_registers']), *regs,
            'true' if result['partition'] == 'holdout' else 'false', c.get('load_register', 0),
            c.get('merge_destination', 0), c.get('merge_source', 0), c.get('shift', 0),
            c.get('mask_begin', 0), c.get('mask_end', 0), c.get('first_shift', 0),
            c.get('first_mb', 0), c.get('first_me', 0), c.get('second_shift', 0),
            c.get('second_mb', 0), c.get('second_me', 0), c.get('li_immediate', 0),
            c.get('dirty_shift', 0), c.get('dirty_mask_end', 0)))
    chunks.append('};\n')
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text('\n\n'.join(chunks))
    return len(accepted)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-cpp', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'out/renderer-automation-pilot/setter-contracts.json')
    parser.add_argument('--ghidra-facts', type=Path,
                        help='Optionally revalidate the frozen natural Ghidra export')
    args = parser.parse_args(argv)
    manifest, frozen, records = load_inputs()
    getter.validate_sources(manifest)
    validate_frozen_attestation(frozen, records)
    validate_family_v2(load_family_v2(), records)
    ghidra = None
    if args.ghidra_facts:
        require(hashlib.sha256(args.ghidra_facts.read_bytes()).hexdigest() ==
                frozen['natural_ghidra_facts_sha256'], 'Natural Ghidra facts hash drift')
        ghidra = getter.validate_ghidra(manifest, args.ghidra_facts)
        validate_candidates(frozen, ghidra)
    results = analyze(records, ghidra)
    if args.emit_cpp:
        emit_cpp(records, args.emit_cpp)
    if args.output and not args.emit_cpp:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(dict(
            scope=frozen['scope'], candidate_count=len(results), excluded_getter_count=59,
            manifest_sha256=getter.sha(MANIFEST.read_bytes()),
            selection_sha256=getter.sha(CANDIDATES.read_bytes()),
            family_v2_sha256=getter.sha(FAMILY_V2.read_bytes()),
            analyzer_sha256=getter.sha(Path(__file__).read_bytes()),
            ghidra_sha256=frozen['natural_ghidra_facts_sha256'],
            ghidra_revalidated_this_run=ghidra is not None,
            tally_statuses_changed=False, parent_complete=False,
            task='R09.effects',
            raw_store_without_natural_store_functions=frozen['raw_store_without_natural_store_functions'],
            accepted_count=sum(r['status'] == 'local_contract' for r in results),
            unsupported_count=sum(r['status'] == 'unsupported' for r in results),
            functions=results), indent=2) + '\n')
    print(json.dumps(dict(candidates=len(results), accepted=sum(
        r['status'] == 'local_contract' for r in results), unsupported=sum(
        r['status'] == 'unsupported' for r in results))))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
