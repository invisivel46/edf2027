"""Check caller-bounded native replacement of legacy tiling entry/exit."""
import csv,hashlib,json,re,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
bridge=ROOT/'src/native_graphics/guest_shader_bridge.cpp';source=bridge.read_text()
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
specs={0x821409A0:({0x8219C654},'insert','require absent device; insert into untiled_devices; return r3=0'),
       0x82140E98:({0x8219C990,0x8219C6B8},'erase','require present device; erase from untiled_devices; return r3=0')}
rows,calls=[],[]
for address,(allowed,operation,effect) in specs.items():
 fn=f'sub_{address:08X}';marker=f'REX_HOOK_RAW({fn})';start=source.index(marker)
 body_start=source.index('{',start);depth=1;end=body_start+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 body=source[body_start:end]
 assert '__imp__' not in body and 'REX_CALL' not in body
 assert f'untiled_devices.{operation}(ctx.r3.u32)' in body and 'ctx.r3.u64=0;' in body
 guards={int(x,16) for x in re.findall(r'uint32_t\(ctx.lr\)!=0x([0-9A-F]+)',body)}
 assert guards==allowed
 actual=list(db.execute('select func,addr,raw from instrs where target=? order by addr',(address,)))
 assert {a+4 for _,a,_ in actual}==allowed
 for caller,site,raw in actual:
  assert struct.unpack_from('>I',image,site-0x82000000)[0]==raw&0xffffffff and raw&1
  calls.append(dict(caller=caller,site=f'{site:08X}',raw=f'{raw&0xffffffff:08X}',target=fn,return_address=f'{site+4:08X}'))
 rows.append(dict(function=fn,route='native replacement with strict caller/lifetime guards',native_effect=effect,
  accepted_return_addresses=' | '.join(f'{a:08X}' for a in sorted(allowed)),
  original_called='False',source='src/native_graphics/guest_shader_bridge.cpp:'+str(source.count('\n',0,start)+1),
  sha256=hashlib.sha256(bridge.read_bytes()).hexdigest(),
  remaining='native clear/resolve owners and enclosing original pass effects; indirect callers not established; full runtime parity unproven'))
for name,data in [('untiled-boundary-contracts.csv',rows),('untiled-boundary-callers.csv',calls)]:
 with (OUT/name).open('w',encoding='utf-8',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
summary=dict(native_replaced_boundaries=len(rows),verified_direct_callers=len(calls),all_direct_return_addresses_match_guards=True,
 limitation='Source route and direct caller coverage only. Does not prove native output parity or that enclosing pass is fully replaced.')
(OUT/'untiled-boundary-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
