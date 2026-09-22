"""Verify the optional hierarchy callback in the previously identified map table."""
import hashlib,json,sqlite3,struct
from pathlib import Path
root=Path(__file__).resolve().parent.parent
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
table=0x82002624
rows=[]
for slot in (2,6):
    symbol=db.execute('select func from vtables where tbl=? and slot=?',(table,slot)).fetchone()[0]
    pointer=struct.unpack_from('>I',image,table+slot*4-0x82000000)[0]
    assert symbol==f'sub_{pointer:08X}'
    rows.append(dict(table=f'{table:08X}',slot=slot,byte_offset=slot*4,target=symbol,raw_pointer=f'{pointer:08X}'))
primitive_rows=[]
for kind in (2,3):
    address=0x82008888+kind*8
    multiplier,bias=struct.unpack_from('>II',image,address-0x82000000)
    primitive_rows.append(dict(type=kind,address=f'{address:08X}',multiplier=multiplier,bias=bias,primitive_count=4,vertex_count=(multiplier*4+bias)&0xffffffff))
result=dict(rows=rows,primitive_rows=primitive_rows,image_sha256=hashlib.sha256(image).hexdigest(),
    scope='Previously identified map table only; pointer identity is not a complete runtime receiver population. Slot6 is the candidate for C5FC8 callback byte-offset24.')
(root/'out/renderer-inventory/hierarchy-callback-candidate.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
