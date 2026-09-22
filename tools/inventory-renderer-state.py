"""Export raw setter/effect-state instructions and their direct callers."""
import csv,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
functions=['821C0AF8','821C0B00','821C0B90','821C0BA0','821C0D70','821C0ED8',
 '821A4150','821A4160','8217C3A8','8217C3C0','8217C4A0','8217C840','8217CE60',
 '821A6AA8','820CD218','821A5A10','821A4DE8','821A4FC0','821A6508','821A6158',
 '82202A30','8224D070','82249818','821A53A8','821A4BA0','82113020','820B3508','82112FC0',
 '821C1FE8','821B5258','820B25B8','820B8678','821C1080','820E60B8','82137978','821370E0',
 '821387E8','82138E00','82138858','82139508','82144E20','82144868','82138FC0','82144240','821444D0']
instructions=[];callers=[]
for fn in functions:
 for addr,raw,mn,d,a,b,imm,target in db.execute('select addr,raw,mn,d,a,b,imm,target from instrs where func=? order by addr',('sub_'+fn,)):
  assert struct.unpack_from('>I',image,addr-0x82000000)[0]==raw&0xffffffff
  instructions.append(dict(function=fn,site=f'{addr:08X}',raw=f'{raw&0xffffffff:08X}',mnemonic=mn,d=d,a=a,b=b,imm=imm,target=f'{target:08X}' if target else ''))
 for caller,addr,raw in db.execute('select func,addr,raw from instrs where target=?',(int(fn,16),)):
  raw &= 0xffffffff
  if raw>>26 != 18:continue
  assert struct.unpack_from('>I',image,addr-0x82000000)[0]==raw
  callers.append(dict(caller=caller,site=f'{addr:08X}',callee='sub_'+fn,linked=bool(raw&1)))
def write(name,rows):
 with (OUT/name).open('w',newline='',encoding='utf-8') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
write('renderer-state-instructions.csv',instructions);write('renderer-state-callers.csv',callers)
(ROOT/'out/ghidra/renderer-inventory/state-functions.txt').write_text('\n'.join(functions)+'\n')
summary=dict(functions=len(functions),verified_instructions=len(instructions),direct_callers_sites=len(callers),
 limitation='Direct caller sites only; alias writes, inlining and indirect calls not excluded.')
(OUT/'renderer-state-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
