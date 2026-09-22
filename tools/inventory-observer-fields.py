"""Locate observer field uses; offset matches alone do not prove receiver type."""
import csv,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
def word(a): return struct.unpack_from('>I',image,a-0x82000000)[0]
rows=[]
for offset in (13068,19956,20100):
 for fn,addr,raw,mn,d,a in db.execute('select func,addr,raw,mn,d,a from instrs where imm=? order by addr',(offset,)):
  assert word(addr)==raw&0xffffffff
  rows.append(dict(offset=offset,function=fn,site=f'{addr:08X}',raw=f'{raw&0xffffffff:08X}',mnemonic=mn,d=d,a=a,
   scope='immediate-offset candidate; aliases/indexed/bulk writes not excluded'))
with (OUT/'observer-field-sites.csv').open('w',newline='',encoding='utf-8') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
table=0x82009c74
slots={str(s):f'{word(table+s*4):08X}' for s in (0,3,6,7,8,10)}
assert slots['6']=='82144868' and slots['7']=='8252B718'
summary=dict(field_sites=len(rows),observer_table=f'{table:08X}',raw_verified_slots=slots,
 monitor_locations={f'{a:08X}':f'{word(a):08X}' for a in (0x8200071c,0x82000800)},
 limitation='Table construction path verified separately; later replacement and runtime monitor contents remain unresolved.')
(OUT/'observer-field-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
