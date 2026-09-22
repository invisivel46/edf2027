"""Scoped source/raw reviews of the eight shared renderable-helper CTR sites."""
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
GH = ROOT / 'out/ghidra/renderer-inventory/helpers'


def read(path, delimiter=','):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream, delimiter=delimiter))


specs = [
 ('8219CEA4', 'device state save', 'target=Word(device + input_offset + 524); device=Word(owner+8)',
  'r3=device; return r3 saved as value in an 8-byte offset/value record',
  'For each supplied offset append offset and callback result; append 0x7fffffff/count marker. Owner+44 count and owner+40 capacity mutate.',
  'Not a vtable slot: offset is added to device before loading target. Device function table population remains unclosed.'),
 ('8219CA6C', 'device state restore', 'target=Word(device + saved_offset + 56); device=Word(owner+8)',
  'r3=device; r4=saved value',
  'Pop marker then recorded offset/value entries in reverse order; owner+44 count mutates. Bad marker calls8219F7A0.',
  'Not a vtable slot: saved offset is added to device before loading target. Restore targets remain unclosed.'),
 ('8219F800', 'optional diagnostic object', 'object=Word(8257C00C); target=Word(Word(object)+4)',
  'r3=object; r4=1; r5=entry r3; r6=pointer to saved variadic argument area',
  'Null global object skips dispatch. One caller is the state-restore marker failure path.',
  'Slot1 object callback; registration and full caller population remain unclosed. Diagnostic purpose inferred from argument flow.'),
 ('82130034', 'allocator override', 'target=Word(entry r3+1412)',
  'r3=entry allocator; r4=address of mutable local pointer; r5=entry size pointer',
  'Null callback uses NtAllocateVirtualMemory with0x60001000,protection4,zero; negative status returns null. Updates allocation bookkeeping after success.',
  'Allocator callback registration unknown; not a render vtable method.'),
 ('821E9FDC', 'TLS-provided runtime callback', 'target=KeTlsGetValue(Word(825566F8))',
  'r3=Word(825566F4)',
  'Calls TLS value without local null guard. Null return triggers allocation of196bytes via821F0338.',
  'TLS publisher/target population unclosed; runtime data acquisition rather than established draw submission.'),
 ('821EA010', 'runtime allocation publication callback', 'target=Word(8257C590)',
  'r3=Word(825566F4); r4=new196byte allocation',
  'Called after preceding TLS callback returned null and allocation succeeded. Nonzero result enters runtime record initialization.',
  'Global callback producer and ownership unclosed; no local target-null guard.'),
 ('821F03D0', 'optional runtime predicate', 'target=Word(8257C5A8)',
  'entry argument registers forwarded',
  'Null target returns0; nonzero callback return normalized to1,zero to0.',
  'Callback identity and registration unclosed; purpose beyond predicate not established.'),
 ('821EFB00', 'runtime failure handler', 'target=Word(825892E0)',
  'entry argument registers forwarded',
  'Non-null target invoked; null target calls821F6318 withr3=2 then trap22.',
  'Handler identity and registration unclosed; not established as ordinary frame work.'),
]
raw = {r['site']:r for r in read(OUT/'renderable-helper-instructions.csv')}
witnesses = read(OUT/'renderable-helper-witnesses.csv')
expected = {r['site'] for r in witnesses if r['kind']=='indirect CTR transfer'}
assert expected == {r[0] for r in specs}
manifest = read(GH/'decompilation.tsv', '\t')
assert len(manifest)==7 and all(r['status']=='ok' for r in manifest)
rows=[]
for site, role, target, arguments, effects, remaining in specs:
    instruction=raw[site]
    assert int(instruction['raw'],16)==0x4E800421
    fn=instruction['function']
    path=GH/(fn[4:].lower()+'.c')
    rows.append(dict(function=fn, site=site, raw=instruction['raw'], role=role,
                     target_expression=target, arguments=arguments, effects=effects,
                     remaining=remaining, methods_reaching_site=len({r['method'] for r in witnesses if r['site']==site and r['kind']=='indirect CTR transfer'}),
                     decompilation=path.relative_to(ROOT).as_posix(),
                     decompilation_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                     status='local dispatch reviewed; target population unresolved'))
with (OUT/'helper-callback-review.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
summary=dict(reviewed_sites=len(rows), functions=len({r['function'] for r in rows}),
             corrected_vtable_pattern_sites=['8219CEA4','8219CA6C'],
             target_population_resolved=0,
             limitation='Local raw/generated/Ghidra dispatch review, not exhaustive target registration or runtime activation.')
(OUT/'helper-callback-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
