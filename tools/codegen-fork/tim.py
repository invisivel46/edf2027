"""Summarize timing runs: ms/step (engine step dispatch wall time per simulation step, --edf_step_timing),
simulation steps/s and FPS inside the measurement window.
M1 (benchmark): window = MISSION.CAM marker +20..+80 s (the heavy opening of street gameplay).
Horde: window = mark 'horde hold-begin' .. 'horde hold-end'.
usage: tim.py [runs_dir]"""
import re, sys, os, glob, datetime, collections, statistics
d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'runs')
TS = re.compile(r'^\[(\S+ \S+)\]')
def ts(l):
    m = TS.match(l)
    return datetime.datetime.strptime(m.group(1), '%Y-%m-%d %H:%M:%S.%f') if m else None
rows = []
for p in sorted(glob.glob(os.path.join(d, 't*-*.log'))):
    tag = os.path.basename(p)[:-4]
    m = re.match(r't(\d+)-(.+)-(m1|horde)$', tag)
    if not m: continue
    rep, var, sc = m.groups()
    L = open(p, encoding='utf-8', errors='replace').read().split('\n')
    start = end = None
    for l in L:
        if sc == 'm1' and start is None and "MISSION.CAM'" in l:
            t = ts(l); start, end = t + datetime.timedelta(seconds=20), t + datetime.timedelta(seconds=80)
        if sc == 'horde' and "mark 'horde hold-begin' at" in l: start = ts(l)
        if sc == 'horde' and "mark 'horde hold-end' at" in l: end = ts(l)
    errs = sum(1 for l in L if '[error]' in l or '[critical]' in l)
    crash = any('crashed' in l for l in L)
    if not start or not end:
        rows.append((var, sc, rep, None, None, None, errs, crash, 'incomplete')); continue
    ms = st = 0.0; fps = []; n = 0
    for l in L:
        t = ts(l)
        if not t or not (start <= t <= end): continue
        mm = re.search(r'Step timing: dispatches=\d+ steps=(\d+) dispatch_ms=([\d.]+)', l)
        if mm: st += int(mm.group(1)); ms += float(mm.group(2)); n += 1
        mm = re.search(r'\] FPS: ([\d.]+)', l)
        if mm: fps.append(float(mm.group(1)))
    secs = (end - start).total_seconds()
    rows.append((var, sc, rep, ms / st if st else None, st / secs if secs else None,
                 statistics.mean(fps) if fps else None, errs, crash, ''))
agg = collections.defaultdict(list)
print(f'{"variant":12s} {"sc":5s} rep  ms/step  steps/s   FPS  errors')
for r in rows:
    var, sc, rep, mss, sps, fps, errs, crash, note = r
    print(f'{var:12s} {sc:5s} {rep:3s} ' + (f'{mss:7.3f} {sps:8.1f} {fps:6.1f}' if mss else '   -        -       -') + f' {errs:5d} {"CRASH" if crash else ""} {note}')
    if mss: agg[(var, sc)].append((mss, sps, fps))
print()
order = ['base', 'ipa-exact', 'ipa-abi', 'vt-exact', 'vt-abi', 'fast-exact', 'fast-abi']
for sc in ('m1', 'horde'):
    b = agg.get(('base', sc))
    bm = statistics.median(x[0] for x in b) if b else None
    bf = statistics.median(x[2] for x in b) if b else None
    print(f'== {sc}: median of runs (vs base)')
    for v in order:
        a = agg.get((v, sc))
        if not a: continue
        m = statistics.median(x[0] for x in a); f = statistics.median(x[2] for x in a); s = statistics.median(x[1] for x in a)
        rng = f'{min(x[0] for x in a):.3f}-{max(x[0] for x in a):.3f}'
        print(f'  {v:12s} n={len(a)} ms/step {m:6.3f} ({rng}) {100*(m/bm-1):+6.1f}%  steps/s {s:5.1f}  FPS {f:6.1f} {100*(f/bf-1):+6.1f}%' if bm else f'  {v} {m}')
