"""Inventory literal callback/counter store candidates; equal offsets are not object proof."""
import csv, hashlib, json, sqlite3, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
db = sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1', uri=True)
image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest() == '91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
rows = []
for fn, address, raw in db.execute('select func,addr,raw from instrs order by addr'):
    raw &= 0xffffffff
    if raw >> 26 != 36 or raw & 0xffff not in (15120,15136):
        continue
    assert struct.unpack_from('>I', image, address-0x82000000)[0] == raw
    rows.append(dict(function=fn, site=f'{address:08X}', raw=f'{raw:08X}',
                     offset=raw & 0xffff, base_register=(raw >> 16) & 31,
                     source_register=(raw >> 21) & 31,
                     scope='literal stw only; object identity must be established separately'))
with (OUT/'interrupt-publisher-candidates.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
summary = dict(candidate_sites=len(rows), candidates=rows,
    reviewed_registration='821BEBF0 calls82139928(Word(Word8257BFB4+8),821BEA98);821BEC50 calls same setter/device chain with0.',
    native_boundary='guest_shader_bridge.cpp:6140 skips original registration when edf_native_host is true;5367 rejects nonzero device15120 in native swap.',
    teardown='82147A10 normalmode callsVdSetGraphicsInterruptCallback(0,0) at82147B44 before deviceglobalclear and platformshutdown;inflight callback drain not proven.',
    limitation='Equal offsets alone do not identify device state. Computed/indexed/wide stores, bulk initialization and runtime populations are not exhausted.')
(OUT/'interrupt-publisher-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
