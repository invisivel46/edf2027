"""Raw-verified syntactic candidates, not proof of shared object identity."""
import csv, hashlib, json, sqlite3, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest()=='91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
rows=[]
for fn,address,value in db.execute('select func,addr,raw from instrs order by addr'):
    value &= 0xffffffff
    # Decode stw directly rather than relying on mnemonic/operand DB labels.
    if value>>26 != 36 or value&0xffff != 812:
        continue
    assert struct.unpack_from('>I',image,address-0x82000000)[0]==value
    rows.append(dict(function=fn,site=f'{address:08X}',raw=f'{value:08X}',source_register=(value>>21)&31,
                     base_register=(value>>16)&31,scope='literal stw +812 only; object identity unresolved'))
with (OUT/'worker-publication-writer-candidates.csv').open('w',newline='',encoding='utf-8') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
print(json.dumps(rows,indent=2))
review=[]
for fn in ('sub_82144240','sub_821445A8','sub_82144868'):
    body=list(db.execute('select addr,raw from instrs where func=? order by addr',(fn,)))
    assert body
    for address,value in body:
        assert struct.unpack_from('>I',image,address-0x82000000)[0]==value&0xffffffff
    review.append(dict(function=fn,raw_verified_instructions=len(body),scope='raw bytes verified; only publication/init branch semantically reviewed'))
table=[]
for slot,target in ((3,0x821445A8),(6,0x82144868)):
    address=0x82009C74+4*slot
    actual=struct.unpack_from('>I',image,address-0x82000000)[0]
    assert actual==target
    table.append(dict(table='82009C74',slot=slot,target=f'{actual:08X}'))
(OUT/'worker-publication-route-review.json').write_text(json.dumps(dict(functions=review,verified_table_slots=table,
    finding='445A8 command84 with device10809 mask2: if object832==8214EBA0 copy object836 to812; else invoke object832(object836). 44868 later callsEBD0(object812),then clears812.',
    packet_dispatch='44868 scans entryr5 words atentryr4. Headerbits30..31 selecttype;type0 skips ((header>>16)&3fff)+1 payloadwords;type1/2 onlyadvanceheader. Type3 opcode=(header>>8)&ff,count=((header>>16)&3fff)+1,flag=header&1. Exceptopcodes55/63 invokesobjectvtable+12(object,opcode,payload,count,flag) at82144968 thenadvancescountwords. Opcode84thereforereaches445A8 forverifiedtable82009C74. Nested/special55/63 branches notsemanticallyreviewedhere.',
    limitation='Entry command-filter guards apply. Shared table slots establish structural linkage, not runtime receiver population. Other commands and indirect callback targets remain unreviewed.'),indent=2)+'\n')
