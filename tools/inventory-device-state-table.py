"""Export raw state dispatch triplets and the verified initializer instructions."""
import csv,hashlib,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
GH=ROOT/'out/ghidra/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
def read(name):
 with (OUT/name).open(encoding='utf-8-sig') as f:return list(csv.DictReader(f))
def word(a):return struct.unpack_from('>I',image,a-0x82000000)[0]
raw=[]
for addr,value,mn in db.execute('select addr,raw,mn from instrs where func=? order by addr',('sub_821470A8',)):
 assert word(addr)==value&0xffffffff
 raw.append(dict(function='sub_821470A8',site=f'{addr:08X}',raw=f'{value&0xffffffff:08X}',mnemonic=mn))
# Source-reviewed loop: table base, indexed setter/getter writes, stride and bound.
assert word(0x821470C0)==0x3BAB2518
for address,expected in {0x821470EC:0x7D48F92E,0x821470F4:0x7D4BF92E,0x82147104:0x4E800421,
                         0x82147108:0x3BDE0004,0x8214710C:0x3BBD000C,0x82147110:0x2B1E0184}.items():
 assert word(address)==expected
rows=[]
selected={int(r['offset'],16):r for r in read('state-save-offsets.csv')}
for index in range(97):
 offset=index*4;address=0x82552518+index*12
 getter,setter,default=struct.unpack_from('>3I',image,address-0x82000000)
 expected=selected.get(offset,{}).get('native_expected_setter','')
 if expected:assert expected==f'sub_{setter:08X}'
 rows.append(dict(offset=f'0x{offset:x}',table_record=f'{address:08X}',getter=f'sub_{getter:08X}',setter=f'sub_{setter:08X}',
  default=f'{default:08X}',getter_device_displacement=524+offset,setter_device_displacement=56+offset,
  used_by_known_save_callers=offset in selected,native_expected_setter=expected,
  scope='initializer installs these targets and calls setter with default; subsequent mutation and runtime instance identity unproven'))
for name,data in [('device-state-table.csv',rows),('device-state-initializer-instructions.csv',raw)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
expressions={0x28:'Word(device+0x2d54)',0x30:'(Word(device+0x28b4)>>2)&1',
 0x34:'(Word(device+0x28c8)>>3)&255',0x38:'Word(device+0x28c8)&7',0x3c:'Word(device+0x2d3c)>>31',
 0x48:'Word(device+0x2d38)&31',0x4c:'(Word(device+0x2d38)>>8)&31',0x60:'(Word(device+0x28bc)>>3)&1',
 0x64:'PPC fmadds(float(device+0x2884),float(82009650),float(820008D4)); fctidz; low32 via stack scratch',
 0x68:'Word(device+0x28bc)&7',0x6c:'Word(device+0x2d58)',0xc8:'Word(device+0x2d40)'}
getter_rows,getter_raw=[],[]
for r in rows:
 if not r['used_by_known_save_callers']:continue
 offset=int(r['offset'],16);fn=r['getter']
 body=list(db.execute('select addr,raw,mn from instrs where func=? order by addr',(fn,)))
 assert body and body[-1][1]&0xffffffff==0x4e800020
 for address,value,mn in body:
  assert word(address)==value&0xffffffff
  assert mn in {'lwz','rlwinm','bclr','lis','lfs','addi','fmadds','fctidz','stfiwx'}
  if mn=='bclr':assert value&0xffffffff==0x4e800020
  if mn=='stfiwx':assert address==0x821353D8 and word(0x821353C0)==0x3941FFF0
  getter_raw.append(dict(function=fn,site=f'{address:08X}',raw=f'{value&0xffffffff:08X}',mnemonic=mn))
 getter_rows.append(dict(function=fn,offset=r['offset'],result=expressions[offset],instruction_count=len(body),
  stores='stack scratch at entrySP-16 only' if offset==0x64 else 'none',
  remaining='PPC floating conversion/exception semantics retained for0x64; live device identity and later table changes unproven'))
for name,data in [('device-state-getters.csv',getter_rows),('device-state-getter-instructions.csv',getter_raw)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
functions={'821470A8'}|{r['getter'][4:] for r in rows if r['used_by_known_save_callers']}
samplers=[]
for index in range(20):
 address=0x825529A8+index*12
 getter,setter,default=struct.unpack_from('>3I',image,address-0x82000000)
 samplers.append(dict(offset=f'0x{index*4:x}',table_record=f'{address:08X}',getter=f'sub_{getter:08X}',setter=f'sub_{setter:08X}',
  default=f'{default:08X}',getter_device_displacement=912+index*4,setter_device_displacement=444+index*4,
  scope='initializer repeats20table records for26sampler indices; same device callback slots, sampler index passed inr4'))
with (OUT/'device-sampler-table.csv').open('w',encoding='utf-8',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(samplers[0]));w.writeheader();w.writerows(samplers)
functions.update(r[role][4:] for r in samplers for role in ('getter','setter'))
functions.update(r[role][4:] for r in rows for role in ('getter','setter'))
(GH/'dispatch-functions.txt').write_text('\n'.join(sorted(functions))+'\n')
summary=dict(initializer='sub_821470A8',state_records=len(rows),selected_offsets=len(selected),selected_setter_matches=sum(bool(r['native_expected_setter']) for r in rows),
 initializer_raw_instructions=len(raw),focused_functions=len(functions),image_sha256=hashlib.sha256(image).hexdigest(),
 sampler_records=len(samplers),sampler_indices=26,
 limitation='97scalar and20sampler records installed; getter effect review limited to12known save offsets. Installation does not prove no later mutation.')
(OUT/'device-state-table-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
