import sys, re
from collections import Counter, defaultdict
path = sys.argv[1]; proc = sys.argv[2] if len(sys.argv) > 2 else 'edf2027.exe'
per_thread = Counter(); start = {}; funcs = defaultdict(Counter); mods = defaultdict(Counter)
for line in open(path, errors='replace'):
    if not line.startswith('         SampledProfile,') or proc not in line: continue
    parts = [p.strip() for p in line.split(',')]
    tid = parts[3]; ts = parts[6]; fn = parts[7]
    per_thread[tid] += 1; start[tid] = ts
    funcs[tid][fn] += 1
    mods[tid][fn.split('!')[0]] += 1
total = sum(per_thread.values())
print('samples', total)
for tid, n in per_thread.most_common(int(sys.argv[3]) if len(sys.argv) > 3 else 14):
    print(f'\n== tid {tid} samples {n} start {start[tid]}')
    print('   modules:', ', '.join(f'{m}={c}' for m, c in mods[tid].most_common(6)))
    for f, c in funcs[tid].most_common(12): print(f'   {c:6d} {f}')
