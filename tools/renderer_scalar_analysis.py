"""Narrow, fail-closed scalar contracts. No inferred whole-function ownership."""
import csv
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'tests/fixtures/renderer-scalar-pilot.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def load_manifest(path=MANIFEST):
    manifest = json.loads(Path(path).read_text())
    records = manifest['functions']
    require(len(records) == manifest['function_count'], 'Incomplete frozen corpus')
    require(len({r['function'] for r in records}) == len(records), 'Duplicate corpus function')
    return manifest


def generated_body(source, name):
    matches = list(re.finditer(r'^DEFINE_REX_FUNC\(' + re.escape(name) + r'\) \{\n.*?^\}',
                              source, re.M | re.S))
    require(len(matches) == 1, 'Missing/duplicate original body: ' + name)
    return matches[0].group()


def freeze(root=ROOT, output=MANIFEST):
    """Explicit one-time corpus pinning; never overwrites accepted fixtures."""
    require(not Path(output).exists(), 'Frozen corpus exists; use a reviewed new version')
    inventory = root / 'out/renderer-inventory'
    paths = ['scalar-dispatch-review.csv', 'scalar-dispatch-instructions.csv',
             'complete-function-inventory.csv', 'complete-summary.json']
    sources = {name: (inventory / name).read_bytes() for name in paths}
    summary = json.loads(sources['complete-summary.json'])
    image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
    require(sha(image) == summary['external_image_sha256'], 'Image differs from census baseline')
    corpus = read_rows(inventory / paths[0])
    instructions = {}
    for row in read_rows(inventory / paths[1]):
        instructions.setdefault(row['function'], []).append(row)
    source_cache = {}
    locations = {}
    wanted = {r['function'] for r in corpus}
    for source_file in sorted((root / 'generated/default').glob('edf2017_recomp.*.cpp')):
        source = source_file.read_text()
        path = source_file.relative_to(root).as_posix()
        for name in re.findall(r'^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\)', source, re.M):
            if name in wanted:
                require(name not in locations, 'Duplicate original function')
                locations[name] = path
                source_cache[path] = source
    records = []
    for row in sorted(corpus, key=lambda r: r['function']):
        name = row['function']
        listing = sorted(instructions[name], key=lambda r: r['site'])
        require(len(listing) == int(row['instruction_count']), 'Instruction count mismatch')
        for insn in listing:
            address = int(insn['site'], 16)
            require(image[address - 0x82000000:address - 0x82000000 + 4].hex().upper() == insn['raw'],
                    'Instruction bytes differ from image')
        require(name in locations, 'Original function not found')
        source_path = locations[name]
        body = generated_body(source_cache[source_path], name)
        records.append(dict(function=name, source=source_path, body_sha256=sha(body.encode()),
            partition='holdout' if int(sha(name.encode())[:8], 16) % 4 == 0 else 'development',
            instructions=[dict(address=r['site'], word=r['raw']) for r in listing]))
    result = dict(version=1, scope='scalar-dispatch-170; local getter contracts only',
        function_count=len(records), instruction_count=sum(len(r['instructions']) for r in records),
        image_sha256=sha(image), inventory_sha256={name: sha(data) for name, data in sources.items()},
        split='sha256(function)[0:8] modulo 4 == 0 is reserved holdout; not a blinded model benchmark',
        functions=records)
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(result, indent=2) + '\n')
    return result


def decode(word):
    """Typed fields from raw encodings; mnemonics are never acceptance evidence."""
    word = int(word, 16) if isinstance(word, str) else word
    require(0 <= word <= 0xffffffff, 'Invalid instruction word')
    opcode = word >> 26
    result = dict(word=f'{word:08X}', primary_opcode=opcode, supported=False)
    if opcode == 32:
        displacement = word & 0xffff
        if displacement & 0x8000:
            displacement -= 0x10000
        result.update(operation='load_u32_be', destination=(word >> 21) & 31,
                      base=(word >> 16) & 31, displacement=displacement, supported=True)
    elif opcode == 21:
        result.update(operation='rotate_mask_u32', source=(word >> 21) & 31,
                      destination=(word >> 16) & 31, shift=(word >> 11) & 31,
                      mask_begin=(word >> 6) & 31, mask_end=(word >> 1) & 31,
                      record_condition=bool(word & 1), supported=True)
    elif word == 0x4e800020:
        result.update(operation='return_lr', supported=True)
    else:
        result['operation'] = 'unsupported'
    return result


def classify(record):
    insns = record['instructions']
    name = record['function']
    address = int(name.removeprefix('sub_'), 16)
    facts = [dict(address=i['address'], **decode(i['word'])) for i in insns]
    result = dict(function=name, partition=record['partition'], status='unsupported',
                  reason='Outside the two complete-body getter rules', facts=facts)
    if len(insns) not in (2, 3):
        return result
    if any(int(i['address'], 16) != address + index * 4 for index, i in enumerate(insns)):
        result['reason'] = 'Noncontiguous body or incorrect entry'
        return result
    if facts[0]['operation'] != 'load_u32_be' or facts[-1]['operation'] != 'return_lr':
        return result
    load = facts[0]
    if load['base'] != 3 or load['displacement'] % 4:
        result['reason'] = 'Unsupported load base or unaligned displacement'
        return result
    if len(facts) == 2:
        if load['destination'] != 3:
            return result
        transform = dict(shift=0, mask_begin=0, mask_end=31)
        rule = 'load-return-v1'
    else:
        transform = facts[1]
        if (load['destination'] != 11 or transform['operation'] != 'rotate_mask_u32'
                or transform['source'] != 11 or transform['destination'] != 3
                or transform['record_condition']):
            return result
        rule = 'masked-load-return-v1'
    result.update(status='local_contract', reason='', rule=rule,
        contract=dict(input_register=3, load_register=load['destination'],
            displacement=load['displacement'], width=32, endian='big',
            shift=transform['shift'], mask_begin=transform['mask_begin'], mask_end=transform['mask_end'],
            changed_registers=[3, 11] if len(facts) == 3 else [3],
            return_zero_extended=True, memory_writes=[], calls=[],
            preconditions=['Aligned effective address (low32(entry_r3) + signed displacement) modulo 2^32',
                'Four readable ordinary guest-memory bytes; no MMIO or physical-alias semantics',
                'Valid caller return context; no concurrent mutation during invocation'],
            remaining=['Receiver ownership and valid caller population', 'Lifetime, modes and concurrent mutation',
                       'Native replacement equivalence (no native getter adapter in this pilot)']))
    return result


def validate_sources(manifest, root=ROOT):
    cache = {}
    for record in manifest['functions']:
        path = record['source']
        if path not in cache:
            cache[path] = (root / path).read_text()
        require(sha(generated_body(cache[path], record['function']).encode()) == record['body_sha256'],
                'Changed original body: ' + record['function'])
    return cache


def validate_ghidra(manifest, path):
    exported = json.loads(Path(path).read_text())
    require(exported['language'] == 'PowerPC:BE:64:64-32addr', 'Unexpected processor model')
    indexed = {r['function']: r for r in exported['functions']}
    require(len(indexed) == len(exported['functions']) == manifest['function_count'], 'Ghidra census mismatch')
    for record in manifest['functions']:
        require(record['function'] in indexed, 'Missing Ghidra function')
        row = indexed[record['function']]
        actual = {int(i['address'], 16): i['bytes'].upper() for i in row['instructions']}
        expected = {int(i['address'], 16): i['word'] for i in record['instructions']}
        require(len(actual) == len(row['instructions']), 'Duplicate Ghidra instruction address')
        require(all(actual.get(address) == word for address, word in expected.items()),
                'Ghidra/image body mismatch: ' + record['function'])
        row['boundary_matches_inventory'] = actual == expected
        row['additional_body_addresses'] = [f'{a:08X}' for a in sorted(set(actual) - set(expected))]
        require(actual == expected or classify(record)['status'] == 'unsupported',
                'Ghidra/image body mismatch for accepted getter: ' + record['function'])
    return indexed


def analyze(manifest, ghidra=None):
    result = []
    for record in manifest['functions']:
        item = classify(record)
        if ghidra is not None:
            row = ghidra[record['function']]
            item['high_pcode_quality'] = row.get('quality', [])
            item['high_pcode_available'] = row['decompile_completed']
            item['boundary_matches_inventory'] = row['boundary_matches_inventory']
            item['additional_body_addresses'] = row['additional_body_addresses']
            if not row['boundary_matches_inventory']:
                item['reason'] = 'Ghidra includes additional body ranges; boundary investigation required'
            # Unsupported/truncated high-p-code must remain visible. Raw proof is
            # independently restricted to our complete, decoded instruction body.
        result.append(item)
    return result


def emit_cpp(manifest, output, root=ROOT):
    sources = validate_sources(manifest, root)
    accepted = [r for r in analyze(manifest) if r['status'] == 'local_contract']
    by_name = {r['function']: r for r in manifest['functions']}
    chunks = ['// Extracted exact generated guest bodies; no rewritten instruction semantics.']
    for result in accepted:
        record = by_name[result['function']]
        chunks.append(generated_body(sources[record['source']], record['function']))
    chunks.append('static const GetterCase kGetterCases[] = {')
    for result in accepted:
        c = result['contract']
        chunks.append('  {"%s", &__imp__%s, %d, %d, %d, %d, %d, %s},' % (
            result['function'], result['function'], c['displacement'], c['load_register'],
            c['shift'], c['mask_begin'], c['mask_end'],
            'true' if result['partition'] == 'holdout' else 'false'))
    chunks.append('};\n')
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text('\n\n'.join(chunks))
    return len(accepted)
