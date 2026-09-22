"""Verify fixed state-save offset arrays at every database direct caller."""
import csv
import hashlib
import json
import sqlite3
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
specs={0x820C1C80:(0x82015070,1),0x82185AFC:(0x82015070,1),
       0x8218D394:(0x82017728,3),0x8218EFE0:(0x82015070,1),
       0x821A72A4:(0x820176F0,11),0x821A7690:(0x82017720,2),
       0x821A78F0:(0x82017720,2),0x821A7A84:(0x82017720,2),
       0x821A7B94:(0x82017728,3),0x821A7CB0:(0x82017728,3)}
calls=list(db.execute('select func,addr,raw from instrs where target=? order by addr',(0x8219CE18,)))
assert {a for _,a,_ in calls}==set(specs)
rows,instructions,arrays=[],[],{}
for fn,site,branch in calls:
    pointer,count=specs[site]
    window=list(db.execute('select addr,raw from instrs where func=? and addr<=? order by addr desc limit 19',(fn,site)))[::-1]
    decoded=[]
    for addr,word in window:
        word &= 0xffffffff
        assert struct.unpack_from('>I',image,addr-0x82000000)[0]==word
        instructions.append(dict(function=fn,call_site=f'{site:08X}',site=f'{addr:08X}',raw=f'{word:08X}'))
        imm=word&0xffff
        if imm&0x8000:imm-=0x10000
        decoded.append((addr,word>>26,(word>>21)&31,(word>>16)&31,imm))
    # Source-reviewed local argument setup, checked against raw PPC encoding.
    add=max(r for r in decoded if r[1:4]==(14,4,11))
    high=max(r for r in decoded if r[0]<add[0] and r[1:4]==(15,11,0))
    size=max(r for r in decoded if r[1:4]==(14,5,0))
    assert ((high[4]<<16)+add[4])&0xffffffff==pointer and size[4]==count
    values=struct.unpack_from('>'+str(count)+'I',image,pointer-0x82000000)
    arrays[pointer]=values
    rows.append(dict(function=fn,site=f'{site:08X}',array=f'{pointer:08X}',count=count,
                     offsets=' | '.join(f'0x{v:x}' for v in values),
                     argument_sites=' | '.join(f'{r[0]:08X}' for r in (high,add,size)),
                     scope='all database direct calls to8219CE18; indirect callers and runtime array mutation not excluded'))
with (OUT/'material-setter-population.csv').open(encoding='utf-8-sig') as stream:
    setters={int(r['offset'],16):r for r in csv.DictReader(stream)}
offsets=[]
for value in sorted({v for values in arrays.values() for v in values}):
    mapped=setters[value]
    offsets.append(dict(offset=f'0x{value:x}',save_device_displacement=value+524,
                        restore_device_displacement=value+56,
                        native_expected_setter=mapped['setter'],native_activation_route=mapped['activation_route'],
                        array_word_sites=' | '.join(f'{a+i*4:08X}' for a,values in sorted(arrays.items()) for i,v in enumerate(values) if v==value),
                        remaining='verify device getter/setter table initialization; native expected setter is not runtime target proof'))
for filename,data in [('state-save-callers.csv',rows),('state-save-argument-instructions.csv',instructions),('state-save-offsets.csv',offsets)]:
    with (OUT/filename).open('w',encoding='utf-8',newline='') as stream:
        w=csv.DictWriter(stream,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
summary=dict(direct_calls=len(rows),unique_arrays=len(arrays),unique_offsets=len(offsets),
             raw_verified_argument_window_rows=len(instructions),
             image_sha256=hashlib.sha256(image).hexdigest(),
             arrays={f'{a:08X}':list(v) for a,v in sorted(arrays.items())},
             limitation='Static array contents and source-reviewed direct-call setup only; table identity, getter effects and runtime mutation remain open.')
(OUT/'state-save-offset-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
