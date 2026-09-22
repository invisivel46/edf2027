"""Verify worker registration switch and known receiver slots against raw image."""
import hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
digest=hashlib.sha256(image).hexdigest()
assert digest=='91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
def word(address): return struct.unpack_from('>I',image,address-0x82000000)[0]
switch=[]
for i,target in enumerate((0x8243A06C,0x8243A078,0x8243A084,0x8243A060)):
    address=0x8243A050+4*i
    assert word(address)==target
    switch.append(dict(mode=i+1,address=f'{address:08X}',target=f'{target:08X}'))
slots=[]
for table in (0x82064F60,0x82065678):
    for slot,target in ((3,0x824339F0),(5,0x82433A60),(14,0x8243A000)):
        address=table+4*slot
        assert word(address)==target
        slots.append(dict(table=f'{table:08X}',slot=slot,address=f'{address:08X}',target=f'{target:08X}'))
result=dict(image_sha256=digest,switch=switch,known_table_slots=slots,
    registration='mode1 callback/context60/76; mode2 64/80; mode4 56/72; all others52/68; callbackword stored first',
    limitation='Known raw table candidates, not exhaustive runtime receiver population. Lock/unlock do not establish callback lifetime or drain.')
(ROOT/'out/renderer-inventory/worker-registration-review.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
