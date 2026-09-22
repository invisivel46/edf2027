"""Conservative straight-line PPC evidence for constructor table installs.

Stops at control-flow splits or unsupported instructions. Tracks entry r3,
constants and nonvolatile register copies; ordinary calls invalidate volatile
GPRs. Save-only helpers are recognized from their raw instruction bodies.
This proves prefix facts, not complete constructor effects or runtime population.
"""
import csv, json, sqlite3, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
def word(a): return struct.unpack_from('>I',image,a-0x82000000)[0]
def sign16(v): return (v & 0x7fff)-(v & 0x8000)
def save_only(a):
    if not 0x821e7f00 <= a < 0x821e8900: return False
    for i in range(40):
        v=word(a+4*i)
        if v==0x4e800020: return True
        # Non-update integer/floating stores only; base registers unchanged.
        if v>>26 not in (36,54) and not (v>>26==62 and v&3==0): return False
    return False
def add(value,n): return (value[0],(value[1]+n)&0xffffffff) if value else None
edges=list(csv.DictReader((OUT/'renderable-constructor-edges.csv').open()))
specs=list(csv.DictReader((OUT/'renderable-constructor-candidates.csv').open()))
installs=[]; callrows=[]; reviews=[]
for spec in specs:
    fn=spec['candidate_constructor']; regs={3:('object',0)}; matched=[]; stop='end'
    for addr,raw,mn,target in db.execute('select addr,raw,mn,target from instrs where func=? order by addr',(fn,)):
        v=raw&0xffffffff; assert word(addr)==v
        op=v>>26; d=(v>>21)&31; a=(v>>16)&31; b=(v>>11)&31; imm=sign16(v)
        if op==18:
            if not v&1: stop=f'control transfer {addr:08X}'; break
            parents=[e for e in edges if e['caller']==fn and e['callee']==f'sub_{target:08X}']
            if parents:
                callrows.append(dict(caller=fn,callee=f'sub_{target:08X}',site=f'{addr:08X}',
                    r3_expression=str(regs.get(3)),same_entry_object=regs.get(3)==('object',0)))
            if not save_only(target):
                for r in [0,*range(3,13)]: regs.pop(r,None)
        elif op in (16,19): stop=f'control transfer {addr:08X}'; break
        elif op in (14,15):
            regs[d]=add(regs.get(a) if a else ('constant',0),imm*(65536 if op==15 else 1))
        elif op==31 and ((v>>1)&1023)==444 and d==b: regs[a]=regs.get(d) # mr rA,rS
        elif op in (24,25):
            source=regs.get(d); mask=(v&0xffff) << (16 if op==25 else 0)
            regs[a]=('constant',source[1]|mask) if source and source[0]=='constant' else None
        elif op==36:
            dest=add(regs.get(a),imm); value=regs.get(d)
            if dest==('object',0) and value==('constant',int(spec['table'],16)):
                matched.append(f'{addr:08X}')
                installs.append(dict(constructor=fn,table=spec['table'],site=f'{addr:08X}',raw=f'{v:08X}',
                    receiver='entry r3 + 0',proof='straight-line constants and register copies; ABI call clobbers'))
        elif op in (32,34,40,42): regs[d]=None
        elif op==58:
            regs[d]=None
            if v&3==1: regs[a]=None
        elif op in (33,35,41,43): regs[d]=None; regs[a]=None
        elif op in (37,39,45,53,55): regs[a]=None
        elif op==31 and ((v>>1)&1023)==339: regs[d]=None # mfspr
        elif op in (10,11,38,44,48,50,52,54,59,63): pass
        elif op==31 and ((v>>1)&1023) in (0,32,467): pass # compare, mtspr
        elif op==62 and v&3==0: pass
        else: stop=f'unsupported {addr:08X} {mn}'; break
    reviews.append(dict(constructor=fn,table=spec['table'],install_sites=' | '.join(matched),
        result='entry-object table store verified' if matched else 'prefix proof incomplete',stop=stop))
def write(name,rows):
    with (OUT/name).open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
write('renderable-table-installations.csv',installs)
write('renderable-constructor-call-arguments.csv',callrows)
write('renderable-constructor-review.csv',reviews)
summary=dict(candidates=len(specs),verified_entry_table_stores=sum(bool(r['install_sites']) for r in reviews),
    candidate_edges=len(edges),prefix_edges_observed=len(callrows),same_object_edges=sum(r['same_entry_object'] for r in callrows),
    limitation='Straight-line prefix only; unobserved edges and runtime receiver populations remain unresolved.')
(OUT/'renderable-constructor-proof-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
for r in reviews:
    if not r['install_sites']: print(r)
