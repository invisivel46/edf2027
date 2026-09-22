"""Scoped constructor evidence for bound W; not a complete constructor contract."""
import hashlib, json, sqlite3, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
db = sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1', uri=True)
image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest() == '91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
verified = []
for fn in ('sub_821C2090', 'sub_821C23F8', 'sub_820B36C0', 'sub_820B33B0'):
    body = list(db.execute('select addr,raw from instrs where func=? order by addr', (fn,)))
    assert body
    for address, raw in body:
        assert struct.unpack_from('>I', image, address-0x82000000)[0] == raw & 0xffffffff
    path = ROOT / 'out/ghidra/renderer-inventory/renderables' / (fn[4:].lower()+'.c')
    verified.append(dict(function=fn, raw_verified_instructions=len(body),
        decompilation=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        scope='Full body bytes checked; only constructor chain and bound initialization reviewed here'))
body = [(a,r & 0xffffffff) for a,r in db.execute('select addr,raw from instrs where func=? order by addr', ('sub_821C2090',))]
def matching(primary, reg, base, immediate):
    return [f'{a:08X}' for a,r in body if r>>26==primary and (r>>21)&31==reg
            and (r>>16)&31==base and r&0xffff==immediate]
sites = dict(bound_pointer=matching(14,29,31,288), matrix_pointer=matching(14,30,31,224),
    initial_center_w_store=matching(52,30,29,12),
    copied_translation_pointer=matching(14,11,31,272),
    overwritten_center_zw=matching(62,11,29,8))
assert all(len(v)==1 for v in sites.values()), sites
assert int(sites['initial_center_w_store'][0],16) < int(sites['overwritten_center_zw'][0],16)
result = dict(functions=verified, instruction_sites=sites,
    observation='821C2090 initializes bound+12 toFloat820008CC(1), then copies64bytes fromWord8257C310 toowner224 and copiesowner272..287 tobound288..303. Final local boundW therefore comes fromsourceMatrix+60, overwriting initial1.',
    chain='820B33B0 calls820B36C0, which calls821C23F8, which calls821C2090; helper calls elsewhere in these constructors remain separate.',
    limitation='No exhaustive writer or source-matrix invariant proof. Later helper calls can have transitive effects. This artifact does not mark complete local contracts for these constructors.')
(OUT/'bound-initialization-review.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
