"""Rank a fixed unsupported baseline; structural families are retrieval, not proofs."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def rank(baseline, facts, current):
    pending = {r['function'] for r in baseline['functions'] if r['status'] == 'unsupported'}
    assert len(pending) == 63, 'Expected the reviewed v1 baseline of 63 unsupported setters'
    indexed = {r['function']: r for r in facts['functions']}
    now = {r['function']: r for r in current['functions']}
    groups = defaultdict(list)
    for name in sorted(pending):
        # Use natural boundaries, including shared tails. No semantic acceptance
        # follows from this deliberately coarse mnemonic signature.
        signature = tuple(i['mnemonic'] for i in indexed[name]['instructions'])
        groups[signature].append(name)
    result = []
    for signature, names in groups.items():
        floating = any(op.startswith('f') or op.startswith(('lf', 'stf')) for op in signature)
        branching = any(op.startswith('b') and op != 'blr' for op in signature)
        calls = 'bl' in signature or 'bctrl' in signature or any(
            len(indexed[name]['body_ranges']) > 1 for name in names)
        tier = 3 if calls else 2 if floating else 1 if branching else 0
        category = ['straight-line integer', 'conditional integer', 'floating-point', 'transitive/shared-tail'][tier]
        identifier = 'SET-' + hashlib.sha256(','.join(signature).encode()).hexdigest()[:10]
        remaining = [name for name in names if now[name]['status'] != 'local_contract']
        action = ('Bound the shared tail and each call effect before composing a rule' if calls else
                  'Specify floating-point conversion, rounding and status effects before constructing a rule' if floating else
                  'Enumerate branch predicates and path-specific ordered writes before constructing a rule' if branching else
                  'Decode exact operands and dataflow, then implement one parameterized full-body effect rule')
        result.append(dict(id=identifier, functions=names, count=len(names), signature=list(signature),
            complexity=category, priority_tier=tier, task='R09.effects',
            accepted_local_contracts=len(names)-len(remaining), remaining_functions=remaining,
            disposition='local-contract-generated' if not remaining else 'investigation',
            action=action, dependencies=['Frozen scalar image/body manifest', 'Independent natural Ghidra boundaries',
                'Existing ordered-write harness'] + (['Callee summaries'] if calls else []),
            completion_test='Every listed member either passes exact-byte acceptance plus actual-guest full-state, memory and ordered-effect checks with negative controls, or has an explicit bounded rejection. No receiver/lifetime/task closure inferred.'))
    result.sort(key=lambda r:(r['priority_tier'], -r['count'], len(r['signature']), r['id']))
    assert sum(r['count'] for r in result)==len(pending)
    assert len({n for r in result for n in r['functions']})==len(pending)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,default=ROOT/'out/renderer-automation-pilot/accepted/setters-v1/contracts.json')
    p.add_argument('--current',type=Path,default=ROOT/'out/renderer-automation-pilot/setter-contracts.json')
    p.add_argument('--facts',type=Path,default=ROOT/'out/renderer-automation-pilot/ghidra/facts.json')
    p.add_argument('--output',type=Path,default=ROOT/'out/renderer-automation-pilot/setter-backlog.json')
    p.add_argument('--markdown',type=Path,default=ROOT/'docs/renderer-setter-family-backlog.md')
    args=p.parse_args()
    inputs={key:getattr(args,key).read_bytes() for key in ('baseline','current','facts')}
    baseline,current,facts=(json.loads(inputs[key]) for key in ('baseline','current','facts'))
    assert hashlib.sha256(inputs['facts']).hexdigest()==baseline['ghidra_sha256'], 'Ghidra facts drift'
    groups=rank(baseline,facts,current)
    result=dict(version=1,baseline_unsupported=63,structural_groups=len(groups),
        remaining_unsupported=sum(len(r['remaining_functions']) for r in groups),
        input_hashes={key:hashlib.sha256(value).hexdigest() for key,value in inputs.items()},groups=groups,
        limitation='Priority is a heuristic: straight-line integer first, then conditional, floating-point, transitive; coverage descending within each tier. Mnemonic similarity is not semantic equivalence or promised rule yield.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    text=['# Remaining setter families','',result['limitation'],'',
        f"The fixed v1 baseline has 63 unsupported functions in {len(groups)} groups. The current rules leave {result['remaining_unsupported']} unsupported. Every group belongs to R09.effects; local acceptance leaves receiver, lifetime and mode obligations open.",'',
        'Regenerate: `python tools/rank-renderer-setter-families.py`. Exact members, dependencies, actions and completion tests are in `out/renderer-automation-pilot/setter-backlog.json`.','',
        '| Rank | Group | Functions | Remaining | Complexity | Instruction structure |',
        '|---|---|---:|---:|---|---|']
    for i,r in enumerate(groups,1):
        text.append(f"| {i} | {r['id']} | {r['count']} | {len(r['remaining_functions'])} | {r['complexity']} | {', '.join(r['signature'])} |")
    text+=['','For each straight-line group, decode operands and exact clobbers before writing a rule. Conditional groups require path-specific effects; floating-point groups require rounding/status semantics; transitive groups require callee summaries. Completion requires exact-byte matching and actual guest execution with state, memory and ordered-effect checks, or explicit member-level rejection.','',
        'The nine raw stack/floating-store functions optimized out of high-p-code STORE selection remain separate investigations documented in the setter pilot. They are not silently included in or removed from this 63-function baseline.','']
    args.markdown.write_text('\n'.join(text))
    print(json.dumps({k:result[k] for k in ('baseline_unsupported','structural_groups','remaining_unsupported')}))


if __name__=='__main__': main()
