"""Validate exact planning coverage without conflating it with implementation."""
import csv
import hashlib
import json
import sys
import argparse
import subprocess
import re
from collections import Counter
from pathlib import Path
from renderer_task_playbooks import PLAYBOOKS
from renderer_task_packet import render_guide

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/renderer-coverage'
parser=argparse.ArgumentParser()
parser.add_argument('--catalog',type=Path,default=ROOT/'docs/renderer-coverage.json')
parser.add_argument('--report',type=Path,default=OUT/'coverage-audit.json')
args=parser.parse_args()
catalog=json.loads(args.catalog.read_text(encoding='utf-8'))
errors=[]
def check(ok,message):
    if not ok: errors.append(message)
def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as f: return list(csv.DictReader(f))
def unique(items,key,label):
    result={x[key]:x for x in items}
    check(len(result)==len(items),f'duplicate {label}')
    return result

tasks=unique(catalog['tasks'],'id','tasks')
features=unique(catalog['features'],'id','features')
scenarios=unique(catalog['scenarios'],'id','scenarios')
for t in tasks.values():
    for k in ('change','completion_test','title'): check(bool(t.get(k)),f"{t['id']} missing {k}")
    for d in t['dependencies']: check(d in tasks,f"{t['id']} missing dependency {d}")
    check(t['status']=='open',f"{t['id']} completion requires separate evidence review")
    e=t.get('execution',{})
    for key in ('kind','input_paths','search_symbols','first_slice','prerequisite_evidence','steps','acceptance_cases','deliverables','do_not','stop_condition'):
        check(bool(e.get(key)),f"{t['id']} missing execution {key}")
    check(len(e.get('steps',[]))>=4,f"{t['id']} needs ordered execution steps")
    check(len(e.get('acceptance_cases',[]))>=3,f"{t['id']} needs concrete acceptance cases")
    for path in e.get('input_paths',[]): check((ROOT/path).is_file(),f"{t['id']} missing starting file {path}")
    check(e==PLAYBOOKS.get(t['id']),f"{t['id']} execution differs from authored playbook; regenerate catalog")
check(set(PLAYBOOKS)==set(tasks),'task/playbook coverage mismatch')
cmake_source='\n'.join((ROOT/p).read_text(encoding='utf-8') for p in ['CMakeLists.txt','src/native_graphics/CMakeLists.txt'])
for t in tasks.values():
    for target in t.get('execution',{}).get('test_targets',[]):
        check(bool(re.search(r'add_executable\(\s*'+re.escape(target)+r'\s',cmake_source)),f"{t['id']} unknown build target {target}")
        check(bool(re.search(r'add_test\(NAME\s+'+re.escape(target)+r'\s',cmake_source)),f"{t['id']} unknown CTest name {target}")
# Compare the generated guide using the real catalog, not negative-test catalogs.
if args.catalog.resolve()==(ROOT/'docs/renderer-coverage.json').resolve():
    guide=ROOT/'docs/renderer-task-guide.md'
    check(guide.is_file() and guide.read_text(encoding='utf-8')==render_guide(catalog['tasks']),'generated task guide is stale')
done=set()
def visit(tid,active):
    if tid in active:
        errors.append('dependency cycle: '+' -> '.join(active+[tid])); return
    if tid in done or tid not in tasks: return
    for dep in tasks[tid]['dependencies']: visit(dep,active+[tid])
    done.add(tid)
for tid in tasks: visit(tid,[])
for f in features.values():
    for k in ('works','original','unknown','evidence','tasks','scenarios'): check(bool(f.get(k)),f"{f['id']} missing {k}")
    for t in f['tasks']: check(t in tasks,f"{f['id']} missing task {t}")
    for s in f['scenarios']: check(s in scenarios,f"{f['id']} missing scenario {s}")
for s in scenarios.values():
    check(bool(s['completion_test']) and bool(s['tasks']),f"{s['id']} missing gate/task")
    for t in s['tasks']: check(t in tasks,f"{s['id']} missing task {t}")
modes=unique(catalog['configuration_modes'],'name','configuration modes')
for m in modes.values():
    check(m['task'] in tasks and bool(m['source']) and bool(m['completion_test']),f"unassigned configuration {m['name']}")
for source in catalog['configuration_sources']:
    path=ROOT/source['path']
    check(path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest()==source['sha256'],f"stale configuration source {source['path']}")
current_modes=set()
for path in (ROOT/'src').rglob('*'):
    if path.suffix in ('.h','.cpp'):
        current_modes.update(re.findall(r'REXCVAR_DEFINE_\w+\s*\(\s*(edf_native_\w+|edf_fps_cap)\s*,',path.read_text(encoding='utf-8-sig')))
check(set(modes)==current_modes,'configuration definition coverage drift')
assignments=rows(OUT/'boundary-tasks.csv')
unique(assignments,'id','boundary IDs')
assigned=Counter((r['source'],int(r['row'])) for r in assignments)
check(all(n==1 for n in assigned.values()),'duplicate source-row assignment')
expected=set()
for source in catalog['sources']:
    path=ROOT/source['path']
    check(path.is_file(),f'missing source {path}')
    if not path.is_file(): continue
    check(hashlib.sha256(path.read_bytes()).hexdigest()==source['sha256'],f'stale source {path.name}')
    rs=rows(path)
    check(len(rs)==source['rows'],f'row count drift {path.name}')
    expected.update((path.name,i) for i in range(2,len(rs)+2))
check(set(assigned)==expected,f'source-row coverage mismatch: missing={len(expected-set(assigned))}, extra={len(set(assigned)-expected)}')
for r in assignments:
    for k in ('task','locator','action','completion_test','evidence_status'): check(bool(r.get(k)),f"{r['id']} missing {k}")
    check(r['task'] in tasks,f"{r['id']} unknown task")
    check(r['status']=='open',f"{r['id']} unjustified closed disposition")
# Require the known authoritative surfaces independently of generator choices.
required={'complete-function-inventory.csv','native-boundary-routes.csv',
          'retained-call-effect-reviews.csv','macro-original-dependencies.csv',
          'native-original-references.csv','native-reentry-sites.csv',
          'indirect-site-ledger.csv','renderable-method-review.csv',
          'retained-adapter-terminal-edges.csv','retained-callback-review.csv',
          'state-write-ledger.csv','adapter-nonscalar-operations.csv',
          'ghidra-unresolved-direct-targets.csv','decompiler-quality.csv'}
present={Path(s['path']).name for s in catalog['sources']}
check(required<=present,'missing required coverage surfaces: '+str(sorted(required-present)))
inv=ROOT/'out/renderer-inventory'
# The old audit prints structural_pass but does not return a failure exit code.
# Always execute it and inspect its JSON; a cached report can hide stale sources.
upstream_run=subprocess.run([sys.executable,str(ROOT/'tools/audit-renderer-inventory.py')],cwd=ROOT,capture_output=True,text=True)
try:
    upstream=json.loads(upstream_run.stdout)
except (json.JSONDecodeError,TypeError):
    upstream={}
check(upstream_run.returncode==0 and upstream.get('structural_pass') is True,
      'upstream source/structural inventory audit failed: '+upstream_run.stderr[-500:])
for path in inv.glob('*.csv'):
    with path.open(encoding='utf-8-sig',newline='') as f:
        fields=next(csv.reader(f),[])
    if 'remaining' in fields: check(path.name in present,'unmapped obligation table '+path.name)
summary=json.loads((inv/'complete-summary.json').read_text())
sizes={Path(s['path']).name:s['rows'] for s in catalog['sources']}
for name,key in [('native-boundary-routes.csv','native_boundaries'),
                 ('retained-call-effect-reviews.csv','original_dependency_sites'),
                 ('macro-original-dependencies.csv','macro_original_dependency_sites'),
                 ('indirect-site-ledger.csv','indirect_sites'),
                 ('complete-function-inventory.csv','reachable_functions')]:
    check(sizes.get(name)==summary[key],f'{name} does not match authoritative census')
explicit=rows(inv/'native-original-dependencies.csv')
effects=rows(inv/'retained-call-effect-reviews.csv')
check(Counter((r['hook'],r['callee'],r['source']) for r in explicit)==
      Counter((r['hook'],r['callee'],r['source']) for r in effects),'explicit call/effect join drift')
report=dict(passed=not errors,features=len(features),tasks=len(tasks),execution_playbooks=len(PLAYBOOKS),scenarios=len(scenarios),configuration_modes=len(modes),
            source_tables=len(catalog['sources']),assigned_rows=len(assignments),unassigned_rows=len(expected-set(assigned)),
            unjustified_completed_rows=sum(r['status']!='open' for r in assignments),errors=errors,
            implementation_complete=False,runtime_population_complete=False,
            upstream_structural_pass=upstream.get('structural_pass',False),
            limitation='Structural planning coverage for the declared census only. Runtime report and semantic task acceptance are separate gates.')
args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
sys.exit(0 if report['passed'] else 1)
