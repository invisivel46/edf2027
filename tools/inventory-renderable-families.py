"""Find table-bearing callers of the common renderable constructor.

This is a candidate derivation graph, not a proof of inheritance or receiver
population. Every edge keeps its direct-call and table-reference provenance.
"""
import csv,json,sqlite3,struct
from collections import defaultdict,deque
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
callers=defaultdict(set)
for caller,callee in db.execute('select caller,callee from calls'):callers[callee].add(caller)
tables=defaultdict(set);names=dict(db.execute('select tbl,name from classes'))
for fn,addr in db.execute('select func,addr from datarefs'):
 if addr in names:tables[fn].add(addr)
base='sub_821C2090';queue=deque([base]);reached={base};edges=[]
while queue:
 parent=queue.popleft()
 for child in sorted(callers[parent]):
  if not tables[child]:continue
  edges.append(dict(caller=child,callee=parent,review='table-bearing direct caller; same-object base construction needs argument-flow review'))
  if child not in reached:reached.add(child);queue.append(child)
rows=[]
for fn in sorted(reached):
 for table in sorted(tables[fn]):
  target=db.execute('select func from vtables where tbl=? and slot=4',(table,)).fetchone()
  if not target:continue
  pointer=struct.unpack_from('>I',image,table+16-0x82000000)[0]
  assert pointer==int(target[0][4:],16)
  rows.append(dict(candidate_constructor=fn,table=f'{table:08X}',class_name_testimony=names[table],
   slot4=target[0],pointer_verified=True,review='table reference plus constructor-call chain; table installation and runtime receiver provenance need review'))
def write(name,items):
 with (OUT/name).open('w',newline='',encoding='utf-8') as stream:
  writer=csv.DictWriter(stream,fieldnames=list(items[0]));writer.writeheader();writer.writerows(items)
write('renderable-constructor-candidates.csv',rows);write('renderable-constructor-edges.csv',edges)
methods=[]
native={r['function']:r for r in csv.DictReader((OUT/'complete-function-inventory.csv').open(encoding='utf-8-sig'))}
for method in sorted({r['slot4'] for r in rows}):
 matches=[r for r in rows if r['slot4']==method];record=native.get(method,{})
 methods.append(dict(method=method,classes=' | '.join(sorted({r['class_name_testimony'] for r in matches})),
  tables=' | '.join(sorted({r['table'] for r in matches})),native_boundary=record.get('native_boundary',''),
  review='verified single-blr stub' if method=='sub_8252B718' else 'render method candidate; body and native route require review'))
write('renderable-method-candidates.csv',methods)
gh=ROOT/'out/ghidra/renderer-inventory'
(gh/'renderable-functions.txt').write_text('\n'.join(sorted({f[4:] for f in reached}|{m['method'][4:] for m in methods}))+'\n')
(OUT/'renderable-family-summary.json').write_text(json.dumps(dict(candidate_functions=len(reached),table_references=len(rows),
 distinct_tables=len({r['table'] for r in rows}),slot4_methods=len(methods),constructor_edges=len(edges),
 warning='Call-chain and table-reference evidence are overinclusive; do not report this as proven class inheritance.'),indent=2)+'\n')
print((OUT/'renderable-family-summary.json').read_text())
