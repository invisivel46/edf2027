"""Build review ledgers without treating syntax or old labels as port status."""
import csv,json,re
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'; GH=ROOT/'out/ghidra/renderer-inventory'
def read(path,delimiter=','):
 with path.open(encoding='utf-8-sig',newline='') as stream: return list(csv.DictReader(stream,delimiter=delimiter))
def write(name,rows,fields):
 with (OUT/name).open('w',encoding='utf-8',newline='') as stream:
  writer=csv.DictWriter(stream,fieldnames=fields); writer.writeheader(); writer.writerows(rows)
functions=read(OUT/'complete-function-inventory.csv')
known={r['function'][4:].upper() for r in functions}
extras=[r for r in read(GH/'functions.tsv','\t') if r['address'].upper() not in known]
write('ghidra-additional-functions.csv',extras,['address','name','bytes'])
missing=read(GH/'missing.tsv','\t')
manifest=(ROOT/'generated/default/edf2017_init.cpp').read_text()
symbols={a.upper():n for a,n in re.findall(r'\{\s*0x([0-9A-Fa-f]{8}),\s*(\w+)\s*\}',manifest)}
label_targets={r['address'].upper() for r in missing if r['address'].upper() not in symbols}
labels={a:[] for a in label_targets}
for path in (ROOT/'generated/default').glob('*.cpp'):
 source=path.read_text(encoding='utf-8-sig')
 for match in re.finditer(r'^loc_([0-9A-Fa-f]{8}):',source,re.M):
  if match[1].upper() in labels:
   labels[match[1].upper()].append(f'{path.relative_to(ROOT)}:{source.count(chr(10),0,match.start())+1}')
for row in missing:
 row['generated_symbol']=symbols.get(row['address'].upper(),'')
 row['generated_labels']=' | '.join(labels.get(row['address'].upper(),[]))
 row['status']='symbol mapped; Ghidra body not traversed' if row['generated_symbol'] else ('emitted internal label found; inspect Ghidra body boundary' if row['generated_labels'] else 'unresolved; inspect raw target and containing function')
write('ghidra-unresolved-direct-targets.csv',missing,['address','reason','generated_symbol','generated_labels','status'])
stores=[]
for row in read(GH/'sites.tsv','\t'):
 if row['kind']!='store': continue
 row['review']='stack-base syntax; alias/escape not analyzed' if re.search(r'\(r1\)$',row['instruction']) else 'state-write candidate; destination and lifetime need semantic review'
 stores.append(row)
write('state-write-ledger.csv',stores,['function','site','kind','instruction','review'])
snippets=[]; deps=read(OUT/'native-original-dependencies.csv')
for row in deps:
 path,line=row['source'].rsplit(':',1); line=int(line)
 lines=(ROOT/path).read_text(encoding='utf-8-sig').splitlines()
 row['syntactic_kind']=('original guest function call' if row['callee'].startswith('__imp__sub_') else
                        'named CPU-tail/adapter call' if row['callee'].startswith('__imp__edf_') else 'platform import')
 row['required_review']='classify guard, writes, producer/resource obligations and render ownership'
 snippets.append('## '+row['source']+' / '+row['hook']+' -> '+row['callee']+'\n```cpp\n'+'\n'.join(f'{i+1}: {lines[i]}' for i in range(max(0,line-9),min(len(lines),line+5)))+'\n```\n')
write('native-dependency-review.csv',deps,list(deps[0]))
(OUT/'native-dependency-contexts.md').write_text('\n'.join(snippets),encoding='utf-8')
tables=read(OUT/'verified-vtable-pointers.csv'); callbacks=[]
families={
 'backend':['82001EF0','820198E8','82019FCC'],
 'world':['82002624'],
 'camera':['82003FA4','82003E28','82003E50','82012C5C','82019D4C'],
 'listener':['82001648','820118D4','82015E1C','82017988','82020360']}
sites=[('821A5158','backend',1,'manager+132'),('821A51D8','world',2,'manager+44 list payload+8'),
 ('821A5264','listener',3,'manager+2232 list payload+12'),('821A5290','camera',4,'view list payload+8'),
 ('821A52A8','backend',2,'manager+132'),('821A52E4','backend',3,'manager+132'),
 ('821A52F8','backend',4,'manager+132'),('821A5368','listener',4,'manager+2232 list payload+12')]
for site,family,slot,receiver in sites:
 for table in tables:
  if table['table'] in families[family] and int(table['slot'])==slot:
   callbacks.append(dict(site=site,receiver=receiver,slot=slot,table=table['table'],
    class_name_testimony=table['class_name'],candidate_target=table['function'],pointer_verified=table['pointer_matches'],
    status='slot and table pointer verified; family assignment provisional; runtime receiver population not closed'))
write('core-callback-candidates.csv',callbacks,list(callbacks[0]))
summary=dict(ghidra_additional_functions=len(extras),unresolved_direct_target_records=len(missing),
 unique_unresolved_direct_targets=len({r['address'] for r in missing}),state_store_sites=len(stores),
 direct_target_symbol_gaps=sorted({r['address'] for r in missing if not r['generated_symbol']}),
 direct_target_without_symbol_or_label=sorted({r['address'] for r in missing if not r['generated_symbol'] and not r['generated_labels']}),
 state_store_syntax=dict(Counter(r['review'] for r in stores)),dependency_syntax=dict(Counter(r['syntactic_kind'] for r in deps)),
 core_callback_candidate_rows=len(callbacks),
 warning='These ledgers inventory uncertainty. Syntax and table class names do not prove runtime roles or native completeness.')
(OUT/'review-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
