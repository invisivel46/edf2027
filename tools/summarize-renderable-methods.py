"""Export raw-verified renderable instructions and a bounded method review ledger.

Decompiler call names are syntactic evidence only: Ghidra can follow tail calls
and include callee code. Direct branches below retain actual instruction sites.
"""
import csv, json, re, sqlite3, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
GH = ROOT / 'out/ghidra/renderer-inventory'
db = sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1', uri=True)
image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
native = {r['function']: r for r in csv.DictReader((OUT/'complete-function-inventory.csv').open(encoding='utf-8-sig'))}
manifest = list(csv.DictReader((GH/'renderables/decompilation.tsv').open(), delimiter='\t'))
assert len(manifest) == len((GH/'renderable-functions.txt').read_text().splitlines())
assert all(r['status'] == 'ok' for r in manifest), 'Incomplete decompilation'
instructions, branches = [], []
for entry in manifest:
    fn = 'sub_' + entry['address'].upper()
    for addr, raw, mn, target in db.execute('select addr,raw,mn,target from instrs where func=? order by addr', (fn,)):
        raw &= 0xffffffff
        assert struct.unpack_from('>I', image, addr-0x82000000)[0] == raw
        instructions.append(dict(function=fn, site=f'{addr:08X}', raw=f'{raw:08X}', mnemonic=mn))
        if target and raw >> 26 == 18:
            target_name = f'sub_{target:08X}'
            branches.append(dict(function=fn, site=f'{addr:08X}', target=target_name,
                link=bool(raw & 1), native_boundary=native.get(target_name, {}).get('native_boundary','')))

def write(name, rows):
    with (OUT/name).open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)

write('renderable-instructions.csv', instructions)
write('renderable-direct-branches.csv', branches)
rows = []
for method in csv.DictReader((OUT/'renderable-method-candidates.csv').open(encoding='utf-8-sig')):
    fn = method['method']
    path = GH/'renderables'/f'{fn[4:].lower()}.c'
    source = path.read_text()
    callees = sorted({f'sub_{a.upper()}' for a in re.findall(r'(?:func_0x|FUN_|sub_)([0-9a-fA-F]{8})\s*\(', source)
                      if a.upper() != fn[4:]})
    actual = [b for b in branches if b['function'] == fn]
    rows.append(dict(method=fn, classes=method['classes'], decompilation=str(path.relative_to(ROOT)),
        instruction_count=sum(r['function']==fn for r in instructions),
        direct_link_targets=' | '.join(sorted({b['target'] for b in actual if b['link']})),
        direct_nonlink_targets=' | '.join(sorted({b['target'] for b in actual if not b['link']})),
        decompiler_call_names=' | '.join(callees),
        decompiler_named_native_boundaries=' | '.join(c for c in callees if native.get(c,{}).get('native_boundary')),
        warning_count=source.count('WARNING:'),
        status='single blr verified' if fn=='sub_8252B718' else 'body exported; runtime population and transitive state ownership unresolved'))
write('renderable-method-review.csv', rows)
summary = dict(decompiled_functions=len(manifest), methods=len(rows), raw_verified_instructions=len(instructions),
    direct_branch_sites=len(branches), methods_with_decompiler_warnings=sum(r['warning_count']>0 for r in rows),
    limitation='Decompiler may follow tail calls; names are not a direct-call census or proof of native independence.')
(OUT/'renderable-review-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
