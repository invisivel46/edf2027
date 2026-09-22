"""Raw instruction classification of all scalar device dispatch targets.

Store classification is syntactic; destination ownership is a separate review.
"""
import csv,hashlib,json,sqlite3,struct
from collections import Counter,defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
GH=ROOT/'out/ghidra/renderer-inventory/dispatch'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
with (OUT/'device-state-table.csv').open() as f:table=list(csv.DictReader(f))
uses=defaultdict(list)
for r in table:
 for role in ('getter','setter'):uses[r[role]].append(role+':'+r['offset'])
rows,raw,stores,branches=[],[],[],[]
for fn in sorted(uses):
 body=list(db.execute('select addr,raw,mn from instrs where func=? order by addr',(fn,)))
 assert body
 sites={a for a,_,_ in body};own_stores=[];exits=[]
 for addr,value,mn in body:
  value&=0xffffffff
  assert struct.unpack_from('>I',image,addr-0x82000000)[0]==value
  raw.append(dict(function=fn,site=f'{addr:08X}',raw=f'{value:08X}',mnemonic=mn))
  op=value>>26;xo=(value>>1)&1023;base=(value>>16)&31
  if mn.startswith('st'):
   # Every encountered store must be a recognized raw opcode, independent of mnemonic.
   assert op in (36,37,38,39,44,45,52,53,54,55,62) or (op==31 and xo==983)
   displacement=(value&0xfffc) if op==62 else value&0xffff
   if displacement&0x8000:displacement-=0x10000
   row=dict(function=fn,site=f'{addr:08X}',raw=f'{value:08X}',mnemonic=mn,base_register=base,
            displacement=displacement if op!=31 else '',
            syntax='explicit r1 base' if base==1 and op!=31 else 'indexed/other base; alias review required')
   stores.append(row);own_stores.append(row)
  if op in (16,18):
   bits=16 if op==16 else 26;delta=value&((1<<bits)-4)
   if delta&(1<<(bits-1)):delta-=1<<bits
   target=(delta if value&2 else addr+delta)&0xffffffff
   if target not in sites:
    branches.append(dict(function=fn,site=f'{addr:08X}',target=f'{target:08X}',link=bool(value&1)))
    exits.append(f'{target:08X}')
  if op==19 and xo==528:exits.append('indirect CTR')
 words=[v&0xffffffff for _,v,_ in body]
 status=('empty return' if words==[0x4e800020] else
         'constant zero return' if words==[0x38600000,0x4e800020] else
         'outgoing branch; transitive effects required' if exits else
         'no store or outgoing branch in body' if not own_stores else
         'stores present; review destinations')
 path=GH/(fn[4:].lower()+'.c')
 rows.append(dict(function=fn,table_uses=' | '.join(uses[fn]),instruction_count=len(body),
                  store_sites=' | '.join(r['site'] for r in own_stores),outgoing_targets=' | '.join(exits),
                  local_classification=status,decompilation=path.relative_to(ROOT).as_posix(),
                  sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  limitation='body-level instruction evidence; no native value equivalence or runtime activation claim'))
for name,data in [('scalar-dispatch-review.csv',rows),('scalar-dispatch-instructions.csv',raw),
                  ('scalar-dispatch-stores.csv',stores),('scalar-dispatch-outgoing.csv',branches)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
summary=dict(table_records=len(table),unique_functions=len(rows),raw_verified_instructions=len(raw),
 classifications=dict(Counter(r['local_classification'] for r in rows)),store_sites=len(stores),outgoing_branch_sites=len(branches),
 empty_setter_records=sum(r['setter']=='sub_8252B718' for r in table),zero_getter_records=sum(r['getter']=='sub_821D5978' for r in table),
 limitation='Static local census. Store destinations, getter conversion scratch and retained consumers need scoped semantic review.')
(OUT/'scalar-dispatch-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
