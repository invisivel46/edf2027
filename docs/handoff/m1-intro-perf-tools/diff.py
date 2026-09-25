import sys
from collections import Counter
def load(path, tid):
    c = Counter()
    for line in open(path, errors='replace'):
        if not line.startswith('         SampledProfile,'): continue
        p = [x.strip() for x in line.split(',')]
        if p[3] == tid: c[p[7]] += 1
    return c
a = load(sys.argv[1], sys.argv[3]); b = load(sys.argv[2], sys.argv[4] if len(sys.argv) > 4 else sys.argv[3])
print('totals', sum(a.values()), sum(b.values()))
keys = sorted(set(a) | set(b), key=lambda k: -(a[k] - b[k]))
for k in keys[:int(sys.argv[5]) if len(sys.argv) > 5 else 30]: print(f'{a[k]:6d} {b[k]:6d} {a[k]-b[k]:+6d} {k[:120]}')
