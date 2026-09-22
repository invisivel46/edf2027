"""Bound immediate-field searches; absence is not an alias or reachability proof."""
import csv,hashlib,json,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
path=Path('D:/roms2/edf2027-analysis/guest_image.bin');image=path.read_bytes()
assert struct.unpack_from('>I',image,0x82131A50-0x82000000)[0]==0x3BE1FEC0 # r31=entry SP-320
call=db.execute('select raw,target from instrs where addr=?',(0x82132294,)).fetchone()
assert call and call[1]==0x8252CFAC and (call[0]&1)
assert struct.unpack_from('>I',image,0x82132294-0x82000000)[0]==call[0]&0xffffffff
rows=[]
for fn,addr,raw,mn,d,a,b,imm in db.execute('select func,addr,raw,mn,d,a,b,imm from instrs where imm=13140 or (imm between 160 and 172 and addr between ? and ?) order by addr',(0x82130000,0x82152000)):
    raw &= 0xffffffff
    assert struct.unpack_from('>I',image,addr-0x82000000)[0]==raw
    primary=raw>>26
    width={36:4,37:4,38:1,39:1,44:2,45:2,52:4,53:4,54:8,55:8}.get(primary,0)
    if primary==62 and raw&3 in (0,1):width=8
    if imm!=13140 and not(width and a!=1):continue
    overlaps=[field for field in (164,168,172) if width and imm<field+4 and imm+width>field]
    rows.append(dict(function=fn,site=f'{addr:08X}',raw=f'{raw:08X}',database_mnemonic=mn,
        d=d,a=a,b=b,immediate=imm,scalar_store_width=width,
        candidate_callback_fields=' | '.join(map(str,overlaps)),
        scope='whole database immediate13140' if imm==13140 else 'non-stack scalar store in graphics address window',
        review='receiver provenance unproven; bulk/indexed/alias writers not excluded'))
    if addr==0x82132290:
        rows[-1]['review']='excluded callback candidate: r31 is stack frame (82131A50); this store populates exception record passed to RtlRaiseException at82132294'
with (OUT/'retirement-owner-provenance.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
summary=dict(image_sha256=hashlib.sha256(image).hexdigest(),verified_rows=len(rows),
    immediate13140_references=sum(r['immediate']==13140 for r in rows),
    immediate13140_scalar_stores=sum(r['immediate']==13140 and r['scalar_store_width']>0 for r in rows),
    callback_field_store_candidates=[r for r in rows if r['candidate_callback_fields']],
    excluded_stack_alias_candidates=sum(r['review'].startswith('excluded') for r in rows),
    scope='13140 immediate references across instruction database; overlapping scalar stores starting160..172 in82130000..82152000 excludingr1 base',
    limitation='Not a proof that device13140 or owner164/168/172 are never initialized. Earlier-starting wide stores, indexed/vector/bulk/alias writes and external producers are not covered.')
(OUT/'retirement-owner-provenance-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
