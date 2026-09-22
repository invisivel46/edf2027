"""Direct callers of resource-header format setters and scoped pass effects."""
import csv,hashlib,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory';GH=ROOT/'out/ghidra/renderer-inventory/passes'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
specs={
0x8219C510:('sub_8219C258',1,'resource setup','Publishes owner resource handles and configuration; after color-surface allocation attempt calls format setter with1, then continues depth allocation/error handling. Full allocation callees remain separate.'),
0x8219C65C:('sub_8219C5A8',1,'tiled pass begin','Binds owner+132 color and+136 depth via82137F98/82137CB8; initializes global clear-color words8257BFC0..CC only if8257BFD0 bit0 clear; calls821409A0 then format setter1.'),
0x8219C6C0:('sub_8219C678',0,'tiled pass exit helper','Calls82140E98 then format setter0. Complete resolve/copy callee contract separate.'),
0x8219C998:('sub_8219C930',0,'scene end','Mode1 passes owner+104 to82140E98; other modes pass0. Then format setter0, owner+96 byte=1, bind owner+112 color and+120 depth via82137F98/82137CB8.')}
targets=(0x821360D8,0x82136188,0x82136238,0x821362E8)
calls=list(db.execute('select func,addr,raw,target from instrs where target in (?,?,?,?) order by addr',targets))
assert {a for _,a,_,_ in calls}==set(specs)
rows,raw=[],[]
for fn,site,value,target in calls:
 expected,flag,role,effects=specs[site];assert fn==expected and target==targets[0]
 body=list(db.execute('select addr,raw,mn from instrs where func=? order by addr',(fn,)))
 for a,v,mn in body:
  assert struct.unpack_from('>I',image,a-0x82000000)[0]==v&0xffffffff
  raw.append(dict(function=fn,site=f'{a:08X}',raw=f'{v&0xffffffff:08X}',mnemonic=mn))
 words={a:v&0xffffffff for a,v,_ in body}
 assert words[site-8]==0x38800000+flag and words[site-4]==0x807f0008
 path=GH/(fn[4:].lower()+'.c')
 rows.append(dict(function=fn,site=f'{site:08X}',target=f'sub_{target:08X}',value=flag,
  device='Word(owner+8); r31 owner retained by caller',role=role,effects=effects,
  decompilation=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
  remaining='owner/device identity, underlying resource and resolve callees, retained CPU consumers and native pass equivalence'))
for name,data in [('pass-format-contracts.csv',rows),('pass-format-instructions.csv',raw)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
summary=dict(direct_call_sites=len(rows),raw_verified_instructions=len(raw),
 direct_target_counts={f'sub_{t:08X}':sum(c[3]==t for c in calls) for t in targets},
 limitation='Database direct calls only; initializer and other table-driven calls remain possible. Native scene end still forwards original8219C930 at guest_shader_bridge.cpp:7449.')
(OUT/'pass-format-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
