"""Bounded address provenance for the scalar dispatch instruction population."""
import csv,json
from collections import Counter,defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
def read(name):
 with (OUT/name).open(encoding='utf-8-sig') as f:return list(csv.DictReader(f))
instructions=defaultdict(list)
for r in read('scalar-dispatch-instructions.csv'):instructions[r['function']].append(r)

def destination(row):
 w=int(row['raw'],16);op=w>>26;mn=row['mnemonic']
 if op in (8,14,15,32,34,40,42,58):return (w>>21)&31
 if op in (20,21,24,25,28,29,30):return (w>>16)&31
 if op==31:
  xo=(w>>1)&1023
  if xo in (28,60,316,444,26):return (w>>16)&31
  if xo in (40,136):return (w>>21)&31
  assert xo==983,(row,'unhandled GPR instruction')
 if mn.startswith('st') or op in (10,11,16,18,19,48,50,59,63):return None
 raise AssertionError(row)

owners=[]
for store in read('scalar-dispatch-stores.csv'):
 fn=store['function'];site=int(store['site'],16);w=int(store['raw'],16)
 prefix=[r for r in instructions[fn] if int(r['site'],16)<site]
 prior=defaultdict(list)
 for r in prefix:
  reg=destination(r)
  if reg is not None:prior[reg].append(r)
 base=int(store['base_register']);proof=[]
 if base in (1,3) and w>>26!=31:
  assert not prior[base],(fn,site,base,prior[base])
  owner='entry stack' if base==1 else 'entry device'
  expression=('entrySP' if base==1 else 'device')+f"{int(store['displacement']):+d}"
 elif base==8:
  assert len(prior[8])==1 and not prior[3]
  load=prior[8][0];v=int(load['raw'],16)
  assert v>>26==32 and (v>>16)&31==3
  owner='bound resource header';expression=f'Word(device+{v&65535})+{store["displacement"]}'
  proof=[load['site']]
 elif w>>26==31 and (w>>1)&1023==983:
  assert base==0
  rb=(w>>11)&31;setup=prior[rb][-1];v=int(setup['raw'],16)
  assert v>>26==14 and (v>>16)&31==1 and (v&65535)==0xfff0 and not prior[1]
  owner='entry stack';expression='entrySP-16';proof=[setup['site']]
 else:raise AssertionError(store)
 owners.append(dict(**store,owner=owner,address_expression=expression,provenance_sites=' | '.join(proof),
                    scope='local pointer origin only; object lifetime and caller identity remain separate'))
with (OUT/'scalar-store-owners.csv').open('w',encoding='utf-8',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(owners[0]));w.writeheader();w.writerows(owners)
rows=[]
for review in read('scalar-dispatch-review.csv'):
 stores=[r for r in owners if r['function']==review['function']]
 rows.append(dict(function=review['function'],table_uses=review['table_uses'],
  destinations=' | '.join(sorted({r['address_expression'] for r in stores})),
  owner_classes=' | '.join(sorted({r['owner'] for r in stores})),
  remaining='scissor tail effects separately reviewed; value equivalence, resource-header ownership and runtime publication remain open'))
with (OUT/'scalar-store-function-summary.csv').open('w',encoding='utf-8',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=dict(store_sites=len(owners),owner_classes=dict(Counter(r['owner'] for r in owners)),
 resource_header_functions=sorted({r['function'] for r in owners if r['owner']=='bound resource header'}),
 limitation='Raw bounded GPR provenance; no alias lifetime or native replacement proof.')
(OUT/'scalar-store-owner-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
