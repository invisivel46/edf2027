import re, sys
from collections import defaultdict
agg=defaultdict(lambda: defaultdict(list))
for line in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=re.search(r' (\d\d:\d\d:\d\d)\.\d+\].*Native GPU timing: pass=(\S+) frames=\d+ total_ms=[\d.]+ avg_ms=([\d.]+)',line)
    if m:
        agg[m.group(1)[:7]+'0'][m.group(2)].append(float(m.group(3)))
passes=sorted(set(p for d in agg.values() for p in d)); print(passes)
want=sys.argv[2].split(',') if len(sys.argv)>2 else passes
for t in sorted(agg):
    d=agg[t]; print(t,' '.join(f"{p}={sum(d[p])/len(d[p]):.3f}" for p in want if p in d))
