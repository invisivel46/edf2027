"""Static survey of the codegen fork's interprocedural register pass.

usage: survey.py [out/codegen-fork/ipa-report.jsonl]

Reads the per-function JSON lines the fork writes (edf_ipa_report) and prints: how many
functions get the locals form and why the rest do not; how precise the READS / WRITES /
LIVEOUT summaries are; which functions follow the PPC/Xbox 360 calling convention on
their inputs and outputs as far as the analysis can see, and which do not and why; and
the same for the hot functions of the codegen-overhead profile (docs/codegen-overhead.md).
"""
import collections
import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'out/codegen-fork/ipa-report.jsonl'
R = [json.loads(l) for l in open(path, encoding='utf-8')]

ARGS = {f'r{i}' for i in range(3, 11)} | {f'f{i}' for i in range(1, 14)}
NONVOL = {f'r{i}' for i in range(14, 32)} | {f'f{i}' for i in range(14, 32)} | {'cr2', 'cr3', 'cr4'}
FIXED = {'r1', 'r2', 'r13', 'lr', 'xer'}
ABI_IN = ARGS | NONVOL | FIXED
VOLATILE_SCRATCH = {'r0', 'r11', 'r12', 'f0', 'cr0', 'cr1', 'cr5', 'cr6', 'cr7', 'ctr', 'reserved'}
ABI_OUT = {'r3', 'r4', 'f1', 'f2', 'f3', 'f4'} | NONVOL | FIXED

# Engine-thread exclusive shares from the xperf profile (docs/codegen-overhead.md).
HOT = {
    'horde': {'821AEE50': 20.1, '821C38F0': 8.0, '8211CE10': 5.2, '821AF100': 4.3, '821AF7A0': 3.4,
              '821AF328': 2.7, '821B0258': 2.5, '820D6868': 2.2, '821C58F8': 2.1, '8211CFF0': 2.1},
    'm1': {'821B0258': 7.8, '821C9688': 7.0, '821AEE50': 5.4, '821C9478': 3.0, '821C38F0': 2.7,
           '821C58F8': 2.1},
}


def regs(s):
    if s == 'ALL':
        return None
    return set(s.split()) if s else set()


def pct(a, b):
    return f'{100.0 * a / b:5.1f}%' if b else '  -  '


total = len(R)
loc = [r for r in R if r['locals']]
print(f'functions: {total}; locals form: {len(loc)} ({pct(len(loc), total)})')
reasons = collections.Counter(r['reason'] or 'locals' for r in R)
for k, v in reasons.most_common():
    print(f'  {k:28s} {v:6d}')

print('\nsummary precision (locals + ctx-form functions with a computed summary):')
S = [r for r in R if r['reads'] != 'ALL']
print(f'  computed summaries: {len(S)}; opaque (ALL): {total - len(S)}')
hist = collections.Counter()
for r in S:
    n = r['nreads']
    hist['<=8' if n <= 8 else '<=16' if n <= 16 else '<=32' if n <= 32 else '<=64' if n <= 64 else '>64'] += 1
print('  |READS| ' + ', '.join(f'{k}: {hist[k]}' for k in ['<=8', '<=16', '<=32', '<=64', '>64']))
lo = collections.Counter()
for r in R:
    n = r.get('nliveout', 76)
    lo['ALL' if n == 76 else '<=24' if n <= 24 else '<=40' if n <= 40 else '<=60' if n <= 60 else '>60'] += 1
print('  |LIVEOUT| ' + ', '.join(f'{k}: {lo[k]}' for k in ['<=24', '<=40', '<=60', '>60', 'ALL']))

print('\nABI survey (what the analysis proves about each function\'s boundary):')
clean_in = clean_out = both = 0
viol_in = collections.Counter()
viol_out = collections.Counter()
why = collections.Counter()
for r in S:
    rd = regs(r['reads'])
    lo_ = regs(r.get('liveout', 'ALL'))
    bad_in = rd - ABI_IN
    ok_in = not bad_in
    ok_out = lo_ is not None and not (lo_ - ABI_OUT)
    clean_in += ok_in
    clean_out += ok_out
    both += ok_in and ok_out
    for x in bad_in:
        viol_in[x] += 1
    if lo_ is not None:
        for x in lo_ - ABI_OUT:
            viol_out[x] += 1
    if not ok_in:
        if r['nreads'] > 60:
            why['reads ~everything (opaque callee in closure: import/indirect/hook/ctx-form)'] += 1
        elif bad_in & {'cr0', 'cr1', 'cr5', 'cr6', 'cr7'} and not (bad_in - {'cr0', 'cr1', 'cr5', 'cr6', 'cr7'}):
            why['reads only volatile CR fields (mfcr save of the whole CR)'] += 1
        else:
            why['reads volatile scratch registers'] += 1
print(f'  inputs within the ABI (args, r1/r2/r13, lr, xer, non-volatiles): {clean_in} of {len(S)} ({pct(clean_in, len(S))})')
print(f'  LIVEOUT within the ABI results (r3/r4, f1-f4, non-volatiles, r1/r2/r13, lr, xer): {clean_out} ({pct(clean_out, len(S))})')
print(f'  both: {both} ({pct(both, len(S))})')
print('  input violations by register: ' + ', '.join(f'{k}:{v}' for k, v in viol_in.most_common(12)))
print('  LIVEOUT beyond ABI by register: ' + ', '.join(f'{k}:{v}' for k, v in viol_out.most_common(12)))
for k, v in why.most_common():
    print(f'   {v:6d}  {k}')

by = {r['addr']: r for r in R}
for name, hot in HOT.items():
    print(f'\nhot functions ({name}, % of engine-thread samples):')
    cov = 0.0
    tot = sum(hot.values())
    for a, share in hot.items():
        r = by.get(a)
        if not r:
            print(f'  {a} ? not in report')
            continue
        rd = regs(r['reads'])
        ok = rd is not None and not (rd - ABI_IN)
        cov += share if r['locals'] else 0
        print(f"  sub_{a} {share:5.1f}%  locals={str(r['locals']):5s} {r['reason'] or '':10s} "
              f"|READS|={r['nreads']:2d} |LIVEOUT|={r.get('nliveout', 76):2d} ABI-in={'yes' if ok else 'no':3s} "
              f"auditable={r['auditable']}")
    print(f'  locals form covers {cov:.1f}% of {tot:.1f}% listed')
