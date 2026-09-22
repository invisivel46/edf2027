"""Advisory blocker planning; never grants contracts or changes acceptance."""
import argparse
from collections import Counter
import json
from pathlib import Path

import renderer_scalar_effects as effects
from renderer_scalar_analysis import load_manifest, validate_sources
from renderer_dispatch import ROOT, digest, file_hash, write_json

NAMES = {8:'subfic', 10:'cmpli', 11:'cmpi', 15:'addis', 16:'bc', 18:'b',
         28:'andi.', 44:'sth', 48:'lfs', 50:'lfd', 52:'stfs'}
MEMORY = {32:4, 33:4, 34:1, 35:1, 36:4, 37:4, 38:1, 39:1,
          40:2, 41:2, 42:2, 43:2, 44:2, 45:2, 48:4, 49:4,
          50:8, 51:8, 52:4, 53:4, 54:8, 55:8, 58:8, 62:8}


def operation_key(word):
    """Distinguish extended forms; never collapse a whole XO family to one task."""
    op = word >> 26
    if op in (19, 31):
        return f'op{op}.xo{(word >> 1) & 1023}.bit0{word & 1}'
    if op in (59, 63):
        low = (word >> 1) & 31
        xo = low if op == 59 or low in (18,20,21,22,23,25,26,28,29,30,31) else (word >> 1) & 1023
        return f'op{op}.xo{xo}.rc{word & 1}'
    if op == 30:
        form = (word >> 1) & 15 if word & 0x10 else (word >> 2) & 7
        return f'op30.form{form}.rc{word & 1}'
    return NAMES.get(op, f'op{op}')


def describe(key):
    if key.startswith('gate:'):
        return dict(dependencies=['boundary/receiver investigation'],
                    action='Resolve this boundary or memory precondition for the listed sites; preserve rejection until evidenced.',
                    completion_test='Independent boundaries and receiver/address evidence agree; rerun analyzer and three-way validation.')
    if key == 'subfic' or key.startswith('op31.xo136.'):
        deps = ['XER carry state', '64-bit subtract/borrow semantics']
        tests = 'Check carry-in/out, zero, wraparound, signed limits and preservation of other XER fields.'
    elif key == 'control-flow' or key in ('bc', 'b') or key.startswith('op19.'):
        deps = ['bounded CFG and path coverage', 'CR/LR/CTR state and return semantics']
        tests = 'Exercise taken/untaken paths, LR/CTR effects, exact return boundaries, and reject external or unbounded paths.'
    elif key == 'condition-register' or key in ('cmpi','cmpli','andi.'):
        deps = ['condition-register representation', 'XER SO and comparison width semantics']
        tests = 'Check CR fields, signed/unsigned limits, XER SO propagation and preservation of unrelated fields.'
    elif key in ('lfs','lfd','stfs') or key.startswith(('op59.','op63.')) or '.xo983.' in key:
        deps = ['FPR/FPSCR state', 'floating-point conversion and rounding', 'memory alias/order model']
        tests = 'Check signed zero, infinities, NaNs, subnormals, rounding/FPSCR effects and ordered big-endian memory behavior.'
    else:
        deps = ['instruction-form semantics', 'register/memory effect IR']
        tests = 'Check operand extremes, all affected state and aliases; reject adjacent unsupported instruction variants.'
    return dict(dependencies=deps,
                action='Implement this effect in the engine and independent raw oracle; review exact instruction variants before policy renewal.',
                completion_test=tests + ' Every proposed function must then pass exact-boundary three-way execution and negative controls.')


def blockers(record, boundary, row):
    """Collect all visible instruction and structural blockers, including past first rejection."""
    found = {}
    def add(key, site, word=None):
        item = found.setdefault(key, dict(id=key, kind='investigation' if key.startswith('gate:') else 'semantics', sites=[]))
        locator = dict(address=site)
        if word is not None: locator['word'] = f'{word:08X}'
        if locator not in item['sites']: item['sites'].append(locator)
    facts = row['facts']
    entry = int(record['function'][4:],16)
    if not boundary or not boundary.get('decompile_completed'):
        add('gate:boundary-unavailable',record['function'])
    elif effects._boundary_words(boundary) != [(f['address'],f['word']) for f in facts]:
        add('gate:boundary-disagreement',record['function'])
    if any(int(f['address'],16) != entry+4*i for i,f in enumerate(facts)):
        add('gate:noncontiguous-body',record['function'])
    mem_positions = [i for i,f in enumerate(facts) if int(f['word'],16)>>26 in MEMORY]
    last_mem = max(mem_positions,default=-1)
    for i,f in enumerate(facts):
        w=int(f['word'],16); op=w>>26; site=f['address']; ra=(w>>16)&31; rt=(w>>21)&31
        if not f['supported']: add(operation_key(w),site,w)
        if (f['record_condition'] or op in (10,11,28) or
            (op==31 and ((w>>1)&1023) in (26,28,60,136,316) and w&1)):
            add('condition-register',site,w)
        if f['operation']=='rlwimi' and f['mask_begin']>f['mask_end']: add('wrapping-rlwimi',site,w)
        if op in (16,18) or (op==19 and w!=0x4e800020): add('control-flow',site,w)
        if w==0x4e800020 and i!=len(facts)-1: add('control-flow',site,w)
        if op in MEMORY:
            if ra!=3: add('gate:memory-base',site,w)
            displacement=effects._s16(w & (0xfffc if op in (58,62) else 0xffff))
            if displacement % MEMORY[op]: add('gate:alignment',site,w)
        # Known integer definitions and receiver updates, even after an unknown opcode.
        dest = f.get('destination')
        if op in (8,14,15,32,33,34,35,40,41,42,43,58): dest=rt
        if op==28 or (op==31 and ((w>>1)&1023) in (26,28,60,316)): dest=ra
        if op==31 and ((w>>1)&1023)==136: dest=rt
        if (dest==3 or (op in (33,35,37,39,41,43,45,49,51,53,55) and ra==3)) and i<last_mem:
            add('gate:receiver-mutation',site,w)
    if not facts or facts[-1]['word']!='4E800020': add('gate:return-boundary',record['function'])
    if row['status']=='unsupported' and not found:
        add('gate:unclassified-rejection',record['function'])
    return [dict(found[k], **describe(k)) for k in sorted(found)]


def plan(manifest, boundaries):
    names=[r['function'] for r in manifest['functions']]
    if len(names)!=len(set(names)) or set(names)!=set(boundaries['functions']):
        raise ValueError('Planner corpus/boundary accounting mismatch')
    rows=effects.analyze(manifest,boundaries)
    records={r['function']:r for r in manifest['functions']}
    functions=[]
    for row in sorted(rows,key=lambda r:r['function']):
        items=blockers(records[row['function']],boundaries['functions'][row['function']],row)
        if row['status']=='local_contract' and items:
            raise ValueError('Planner disagrees with accepted analyzer: '+row['function'])
        functions.append(dict(function=row['function'],status=row['status'],first_rejection=row['reason'],
                              blockers=items,required_operations=[x['id'] for x in items if x['kind']=='semantics'],
                              investigation_gates=[x['id'] for x in items if x['kind']=='investigation']))
    unresolved=[r for r in functions if r['status']=='unsupported']
    eligible=[r for r in unresolved if not r['investigation_gates']]
    sets=sorted({tuple(r['required_operations']) for r in eligible})
    candidates=[]
    for operations in sets:
        covered=[r['function'] for r in eligible if set(r['required_operations'])<=set(operations)]
        candidates.append(dict(operations=list(operations),operation_count=len(operations),
            potential_functions=covered,potential_count=len(covered),
            dependencies=sorted({d for k in operations for d in describe(k)['dependencies']}),
            changes=[dict(operation=k,**describe(k)) for k in operations],
            completion_test='Implement/review the whole set; reanalyze exact boundaries and pass three-way validation. Candidate counts are estimates, never acceptance.',
            task='R09.effects'))
    # Each set is a real function's minimal observed requirement set. Union-only
    # supersets cannot beat it on the primary objective (number of new effects).
    candidates.sort(key=lambda r:(r['operation_count'],-r['potential_count'],r['operations']))
    for i,c in enumerate(candidates,1): c['rank']=i
    return dict(version=1,advisory=True,ranking='fewest distinct missing effects, then most potential functions; not an effort estimate',
        limitations='All identifiable blockers in frozen instructions are scanned. Unknown instruction effects, new paths and aliases can reveal additional blockers after implementation; counts are upper bounds.',
        counts=dict(Counter(r['status'] for r in functions)),functions=functions,candidates=candidates,
        investigations=[r for r in unresolved if r['investigation_gates']])


def write_plan(manifest, boundaries, path):
    report=plan(manifest,boundaries)
    report['inputs']=dict(manifest=digest(manifest),boundaries=digest(boundaries),
                         engine=file_hash(Path(effects.__file__)),planner=file_hash(Path(__file__)))
    path=Path(path)
    if not path.exists() or json.loads(path.read_text())!=report: write_json(path,report)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'out/renderer-scalar-pipeline/planner.json')
    args=parser.parse_args()
    manifest=load_manifest();validate_sources(manifest)
    report=write_plan(manifest,effects.load_boundaries(),args.output)
    print(json.dumps(dict(counts=report['counts'],investigations=len(report['investigations']),
        candidates=[{k:r[k] for k in ('rank','operations','potential_count')} for r in report['candidates']]),indent=2))

if __name__=='__main__':main()
