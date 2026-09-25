import sys
from collections import Counter
path, tid = sys.argv[1], sys.argv[2]; n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
c = Counter(); tot = 0
for line in open(path, errors='replace'):
    if not line.startswith('         SampledProfile,'): continue
    p = [x.strip() for x in line.split(',')]
    if p[3] != tid: continue
    c[p[7]] += 1; tot += 1
print('total', tot)
for f, k in c.most_common(n): print(f'{k:6d} {f}')
