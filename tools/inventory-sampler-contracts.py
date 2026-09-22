"""Local sampler effects, with raw instructions and decompiler provenance."""
import csv,hashlib,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
GH=ROOT/'out/ghidra/renderer-inventory/dispatch'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
with (OUT/'device-sampler-table.csv').open() as f:table=list(csv.DictReader(f))
effects={
0:'word0x400 bits10..12',4:'word0x400 bits13..15',8:'word0x400 bits16..18',
12:'word0x414 low2bits become boolean(value)',
16:'mag filter: words0x40c/0x410; reads anisotropy byte0x2d84 and mip override byte0x2dd2',
20:'min filter: words0x40c/0x410; reads anisotropy byte0x2d84 and mip override byte0x2dd2',
24:'word0x40c bits23..24 mip filter',28:'word0x410 bits12..21: float bias times constant82003198, PPC fctidz low10bits',
32:'min LOD byte0x2d9e always; word0x410 bits2..5 and dirty only when bound texture exists; max(texture minimum,requested)',
36:'anisotropy byte0x2d84 always; word0x40c bits25..27 and dirty only when alias bits10/11 in word0x410 set',
40:'mip override byte0x2dd2 clears bit0 then ORs value; recomputes effective word0x410 low2bits',
44:'mip override byte0x2dd2 clears bit1 then ORs value<<1; recomputes effective word0x410 low2bits',
48:'mip override byte0x2dd2 clears bit2 then ORs value<<2; recomputes effective word0x410 low2bits',
52:'max LOD byte0x2db8 always; word0x410 bits6..9 and dirty only when bound texture exists; min(texture maximum,requested)',
56:'word0x414 bits3..4',60:'word0x414 bits5..8: float times constant82009658, PPC fctidz low4bits',
64:'word0x410 bits22..26',68:'word0x410 bits27..31',72:'word0x414 bit2',76:'word0x404 bit11 becomes value==0'}
rows,raw=[],[]
for entry in table:
 offset=int(entry['offset'],16)
 for role in ('getter','setter'):
  fn=entry[role];path=GH/(fn[4:].lower()+'.c');source=path.read_text()
  body=list(db.execute('select addr,raw,mn from instrs where func=? order by addr',(fn,)))
  assert body
  stores=[];calls=[]
  for addr,value,mn in body:
   value&=0xffffffff
   assert struct.unpack_from('>I',image,addr-0x82000000)[0]==value
   raw.append(dict(function=fn,site=f'{addr:08X}',raw=f'{value:08X}',mnemonic=mn))
   op=value>>26;xo=(value>>1)&1023
   if op in (36,37,38,39,44,45,52,53,54,55,62) or op==31 and xo in (151,183,215,247,407,439,663,695,727,759,983,231):stores.append(f'{addr:08X}')
   if op in (16,18) and value&1 or op==19 and xo in (16,528) and value&1:calls.append(f'{addr:08X}')
  assert not calls,'unexpected outgoing call requiring transitive review'
  # Keep the exact decompiler result separate from the scoped human review.
  returns=' | '.join(line.strip() for line in source.splitlines() if 'return ' in line)
  rows.append(dict(function=fn,offset=entry['offset'],role=role,
   effects=effects[offset]+'; packed word addresses are device+24*slot+offset, control bytes device+slot+offset; OR dirty64 device+16 with PPC slot bit, subject to stated guards' if role=='setter' else 'reads current sampler/control fields; see exact decompiler return and raw instructions',
   decompiler_return=returns,store_sites=' | '.join(stores),instruction_count=len(body),
   decompilation=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
   remaining='native route coverage for all20operations, nonpixel slots16..25, value equivalence, floating conversion and publication ownership'))
for name,data in [('sampler-contracts.csv',rows),('sampler-instructions.csv',raw)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
summary=dict(table_records=len(table),functions=len(rows),raw_verified_instructions=len(raw),
 getter_functions_with_store_instructions=[r['function'] for r in rows if r['role']=='getter' and r['store_sites']],
 outgoing_linked_calls=0,
 native_scope='native_material_sampler.h models material filter/mip/bias program and rejects slots>=16; initializer covers26indices. No full20setter native replacement claim.',
 limitation='Local effects from raw/generated/decompiled code; not proof of runtime slot population or native equivalence.')
(OUT/'sampler-contract-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
