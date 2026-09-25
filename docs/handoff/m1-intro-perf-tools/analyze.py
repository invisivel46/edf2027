"""Per-window timeline of a run: trace.csv + game.log markers + threads.csv."""
import csv, sys, importlib.util, statistics, re
from datetime import datetime
from pathlib import Path
from collections import defaultdict

spec = importlib.util.spec_from_file_location('gate', r'D:\roms2\edf2027\.claude\worktrees\m1-intro-perf\tools\renderer-runtime-gate.py')
gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)

run = Path(sys.argv[1])
win = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
lines = gate.read_log_lines(run / 'game.log')
first = None
for l in lines:
    m = gate.STAMP.match(l)
    if m:
        first = datetime.fromisoformat(m.group(1)).timestamp(); break
marks = gate.markers(lines)
abs_marks = {k: (first + v if isinstance(v, (int, float)) and k != 'loading_screens' and v is not None else None) for k, v in marks.items()}
rows = list(csv.DictReader(open(run / 'trace.csv', newline='')))
rows = [r for r in rows if r.get('game_tick')]
t0 = abs_marks['entry'] or float(rows[0]['epoch_ms']) / 1000
def phase(t):
    e, l, g = abs_marks['entry'], abs_marks['load'], abs_marks['gameplay']
    if e is None or t < e: return 'pre'
    if l is None or t < l: return 'intro'
    if g is None or t < g: return 'load'
    return 'game'
# thread samples
threads = defaultdict(lambda: defaultdict(float))
tpath = run / 'threads.csv'
tstart = None
runj = run / 'run.json'
if tpath.exists():
    # sampler t_s is from sampler start (~launch); align via file mtime of log start: approximate with first stamp
    for r in csv.DictReader(open(tpath, newline='')):
        if r['tid'] == '0': continue
        threads[float(r['t_s'])][(r['name'] or 'tid' + r['tid'])] += float(r['cpu_ms'])
bins = defaultdict(list)
for r in rows:
    t = float(r['epoch_ms']) / 1000
    bins[int((t - t0) // win)].append(r)
hdr = f"{'t_s':>6} {'ph':5} {'fps':>6} {'ms':>6} {'st/f':>5} {'disp/f':>7} {'disp/st':>7} {'help/f':>7} {'trans':>6} {'gpuw':>6} {'eng_w':>6} {'draws':>6} {'pcpu/f':>7} {'swcpu':>6}"
print('markers (s after entry):', {k: (None if v is None else round(v - t0, 1)) for k, v in abs_marks.items()})
print(hdr)
for k in sorted(bins):
    rs = bins[k]
    f = lambda n: sum(float(r[n]) for r in rs)
    n = len(rs)
    span = (float(rs[-1]['epoch_ms']) - float(rs[0]['epoch_ms'])) / 1000 or 1
    steps = f('steps')
    t = t0 + k * win
    print(f"{k*win:6.0f} {phase(t + win/2):5} {n/span:6.1f} {f('interval_ms')/n:6.2f} {steps/n:5.2f} {f('step_dispatch_ms')/n:7.2f} {f('step_dispatch_ms')/max(steps,1):7.2f} {f('render_helper_ms')/n:7.2f} {f('frame_transition_ms')/n:6.2f} {f('gpu_wait_ms')/n:6.2f} {f('engine_wait_ms')/n:6.2f} {f('geometry_draws')/n:6.0f} {f('process_cpu_ms')/n:7.2f} {f('swap_thread_cpu_ms')/n:6.2f}")

# per-thread CPU (% of one core) per window, aligned from log first stamp
if threads:
    tids = defaultdict(float)
    for ts, d in threads.items():
        for k, v in d.items(): tids[k] += v
    top = [k for k, _ in sorted(tids.items(), key=lambda kv: -kv[1])[:10]]
    print('thread CPU % of a core per window (tid order):', ' '.join(top))
    tb = defaultdict(lambda: defaultdict(float))
    for ts, d in threads.items():
        t = first + ts
        for k, v in d.items(): tb[int((t - t0) // win)][k] += v
    for k in sorted(tb):
        print(f"{k*win:6.0f} {phase(t0 + k*win + win/2):5} " + ' '.join(f"{tb[k][x]/(win*10):6.1f}" for x in top))
