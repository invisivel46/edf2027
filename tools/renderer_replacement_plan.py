"""Source-verified replacement planning over the atlas; no completion authority."""
import csv
from collections import Counter,defaultdict,deque
import hashlib
import json
from pathlib import Path
import re
import sqlite3

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/renderer-replacement'
INV=ROOT/'out/renderer-inventory'
PACKAGES={
 'P00':('Boundary contracts and acceptance harness',['R01.scope','R01.callbacks','R01.modes','R10.trace','R10.capture','R10.input','R10.scenarios']),
 'P01':('Resource, state and retirement contracts',['R09.effects','R09.resources','R09.retirement','R09.shader','R09.dynamic','R09.assets']),
 'P02':('Producer publication, camera and view ownership',['R02.publication','R02.camera','R05.pass','R05.post']),
 'P03':('Complete retained static rendering',['R03.selection','R03.mutation','R04.pass']),
 'P04':('Animated models and remaining world families',['R06.pose','R06.families','R07.effects','R07.environment','R07.shadows','R07.special']),
 'P05':('Composition, loading, presentation and synchronization',['R08.ui','R08.loading','R08.movie','R08.present','R09.sync']),
 'P06':('Compatibility and diagnostic mode policy',['R09.compat']),
 'P07':('Independent cadence and final performance acceptance',['R10.cadence','R10.performance'])}
BATCHES={
 'worker callback registration':'P05','shared producer dependency':'P02','producer and scheduling':'P02',
 'scene producers and lifetime':'P02','static selection and execution':'P03','simulation/render separation':'P02',
 'static pass CPU state':'P03','static selection':'P03','callback closure':'P00','animated content':'P04',
 'view and pass boundaries':'P02','camera producer':'P02','device/resource CPU contracts':'P01',
 'compatibility mode':'P06','resource production and retirement':'P01','material compatibility mode':'P06',
 'static instance state':'P03','completion contract':'P05','worker callback closure':'P05','platform contract':'P05',
 'submission and waits':'P05','render state production':'P01','profiling mode coverage':'P06',
 'profiling control':'P06','scheduling':'P07','texture production':'P01','profiling callback closure':'P06',
 'resource lock coverage':'P01','dynamic geometry production':'P01','viewport coverage':'P02',
 'draw CPU state':'P03','movie content production':'P05','CPU state and packet coverage':'P01','CPU state contracts':'P01'}

def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def rows(p):
    with Path(p).open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def digest(x):return hashlib.sha256(json.dumps(x,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def write(p,x):Path(p).write_text(json.dumps(x,indent=2,sort_keys=True)+'\n')
def mask(text):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)
def symbol(value):
    m=re.fullmatch(r'(?:__imp__)?(sub_[0-9A-Fa-f]{8})',value)
    return 'sub_'+m[1][4:].upper() if m else None

def closure(seeds,graph,boundaries):
    seen=set();cuts=set();external=set();queue=deque(sorted(seeds))
    while queue:
        n=queue.popleft()
        if n in seen:continue
        if n not in graph:external.add(n);continue
        seen.add(n)
        for target in sorted(graph[n]):
            if target in boundaries and target not in seeds:cuts.add(target)
            elif target not in graph:external.add(target)
            elif target not in seen:queue.append(target)
    return dict(functions=sorted(seen),boundary_cuts=sorted(cuts),external_targets=sorted(external))

def waves(dependencies):
    result={};pending=set(dependencies)
    while pending:
        ready=sorted(n for n in pending if set(dependencies[n])<=set(result))
        if not ready:raise ValueError('Package dependency cycle or missing package')
        for n in ready:result[n]=1+max((result[d] for d in dependencies[n]),default=-1)
        pending-=set(ready)
    return result

def disposition(category):
    if category=='audit oracle':return 'audit-only reference; retain validation oracle'
    if category=='audio observation':return 'non-renderer producer candidate; preserve at explicit interface'
    if 'disabled fallback' in category or category=='material activation disabled fallback':return 'conditional compatibility; mode policy must decide'
    if 'producer' in category or category in ('observational forwarding','engine producer forwarding','shared memory writer forwarding'):
        return 'producer/interface candidate; do not remove simulation or lifetime effects'
    if 'fallback' in category:return 'conditional unresolved path; cannot claim bypassed'
    if category=='platform synchronization import':return 'platform contract; not generated renderer code'
    return 'native-path dependency or extraction; effect contract required'

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    atlas=read(ROOT/'out/renderer-atlas/manifest.json');pins={}
    # The atlas audit must precede this command. Verify actual database identity here too.
    dbpath=ROOT/'out/renderer-atlas/atlas.sqlite'
    if sha(dbpath)!=atlas['outputs']['atlas.sqlite']:raise ValueError('Atlas database drift')
    for path,expected in atlas['inputs'].items():
        if sha(ROOT/path)!=expected:raise ValueError('Atlas input drift: '+path)
    catalog=read(ROOT/'docs/renderer-coverage.json');task_owner={t:p for p,(_,ts) in PACKAGES.items() for t in ts}
    tasks={t['id']:t for t in catalog['tasks']}
    if set(tasks)!=set(task_owner):raise ValueError('Tasks not partitioned exactly')
    for source in catalog['sources']:
        if sha(ROOT/source['path'])!=source['sha256']:raise ValueError('Stale coverage source '+source['path'])
        pins[source['path']]=source['sha256']
    paths=['native-dependency-classification.csv','native-original-dependencies.csv','native-original-references.csv',
           'macro-original-dependencies.csv','native-reentry-sites.csv','native-reentry-source-manifest.csv',
           'native-boundary-routes.csv','retained-adapter-terminal-edges.csv']
    for name in paths:pins['out/renderer-inventory/'+name]=sha(INV/name)
    for path in ['docs/renderer-coverage.json','tools/renderer_replacement_plan.py','out/renderer-atlas/manifest.json']:
        pins[path]=sha(ROOT/path)
    source_cache={}
    def line_at(locator):
        path,num=locator.replace('\\','/').rsplit(':',1)
        if path not in source_cache:
            text=(ROOT/path).read_text(encoding='utf-8-sig');source_cache[path]=(text.splitlines(),mask(text).splitlines());pins[path]=sha(ROOT/path)
        original,clean=source_cache[path]
        return original[int(num)-1],clean[int(num)-1],pins[path]
    for r in rows(INV/'native-reentry-source-manifest.csv'):
        if sha(ROOT/r['path'])!=r['sha256']:raise ValueError('Reentry source drift '+r['path'])
    sites=[];explicit={}
    for r in rows(INV/'native-dependency-classification.csv'):
        text,clean,pin=line_at(r['source'])
        if pin!=r['reviewed_source_sha256'] or not re.search(re.escape(r['callee'])+r'\s*\(',clean):raise ValueError('Unverified explicit call '+r['source'])
        p=BATCHES[r['work_batch']]
        record=dict(id=digest(['explicit',r['source'],r['callee']]),kind='explicit-call',package=p,
            hook=r['hook'],callee=r['callee'],source=r['source'].replace('\\','/'),source_text=text.strip(),
            path_class=r['path_class'],guard_evidence=r['observed_control_flow'],disposition=disposition(r['path_class']))
        sites.append(record);explicit[(record['source'],r['callee'])]=record
    for r in rows(INV/'macro-original-dependencies.csv'):
        text,clean,pin=line_at(r['source']);macro=r['macro']
        if macro not in clean or r['hook'][4:] not in clean:raise ValueError('Stale macro invocation '+r['source'])
        source_path=r['source'].replace('\\','/').rsplit(':',1)[0]
        source=(ROOT/source_path).read_text();definition=re.search(r'^#define '+macro+r'\([^\n]*\n(?:.*\\\n)*.*',source,re.M)
        if not definition or not re.search(r'__imp__.*\(ctx,\s*base\)',mask(definition[0])):raise ValueError('Macro does not forward original '+macro)
        if macro=='EDF_MAP_TIMED_HOOK':p='P02';category='engine producer forwarding'
        elif macro=='EDF_TREE_MUTATION':p='P02';category='scene producer/lifetime forwarding'
        elif macro=='EDF_RENDER_STATE_SETTER':p='P01';category='render-state producer forwarding'
        else:
            phase=re.search(r',\s*(\w+)\)',clean)
            phase=phase[1] if phase else ''
            p={'RenderBuckets':'P03','RenderMesh':'P04','RenderPose':'P04','RenderOverlay':'P05','RenderSceneEnd':'P02','RenderFinish':'P05'}.get(phase,'P00')
            category='phase forwarding: '+phase
        sites.append(dict(id=digest(['macro',r['source'],r['callee']]),kind='macro-forwarding',package=p,hook=r['hook'],
            callee=r['callee'],source=r['source'].replace('\\','/'),source_text=text.strip(),path_class=category,
            guard_evidence=definition[0],disposition=disposition(category)))
    for r in rows(INV/'native-original-references.csv'):
        text,clean,_=line_at(r['source'])
        if r['callee'] not in clean:raise ValueError('Stale helper reference')
        sites.append(dict(id=digest(['reference',r['source'],r['callee']]),kind='original-function-argument',package='P01',
            hook=r['hook'],callee=r['callee'],source=r['source'].replace('\\','/'),source_text=text.strip(),
            path_class='helper-mediated original',disposition='review helper guard; reference alone is not execution',guard_evidence=r['review']))
    for r in rows(INV/'native-reentry-sites.csv'):
        locator=r['source'].replace('\\','/')
        if (locator,r['target']) in explicit:continue
        text,clean,_=line_at(locator)
        needle='ResolveIndirectFunction' if r['kind']=='indirect resolver call' else r['target']
        if needle not in clean:raise ValueError('Stale reentry locator '+locator)
        sites.append(dict(id=digest(['reentry',locator,r['target']]),kind=r['kind'],package='P00',hook='',callee=r['target'],
            source=locator,source_text=text.strip(),path_class='unclassified helper/hook/import dispatch',
            disposition='bounded call-path investigation; no original dependency inferred from name alone',guard_evidence=r['review']))
    routes=rows(INV/'native-boundary-routes.csv');boundaries={r['function'] for r in routes}
    covered={r['hook'] for r in sites}
    native_only=[]
    for r in routes:
        if r['function'] in covered:continue
        if r['route'] not in ('native cursor update','native submission with guest observer callbacks','native untiled replacement'):
            raise ValueError('Unassigned native route '+r['function'])
        native_only.append(dict(function=r['function'],package='P05' if 'submission' in r['route'] or 'cursor' in r['route'] else 'P01',
            route=r['route'],evidence=r['evidence'],status='native route recorded; helper/observer obligations retained'))
    db=sqlite3.connect(dbpath);db.row_factory=sqlite3.Row
    functions={r['symbol']:dict(r) for r in db.execute('SELECT symbol,quality,instruction_count,status FROM functions')}
    graph={n:set() for n in functions};indirect=defaultdict(list)
    for r in db.execute('SELECT * FROM calls'):
        if r['callee'].startswith('?'):indirect[r['caller']].append(dict(r))
        elif r['caller'] in graph:graph[r['caller']].add(r['callee'])
    adapters=defaultdict(set);adapter_rows=rows(INV/'retained-adapter-terminal-edges.csv')
    for r in adapter_rows:
        _,clean,_=line_at(r['source'])
        if r['callee'] not in clean:raise ValueError('Stale adapter terminal '+r['source'])
        if symbol(r['callee']):adapters[r['retained_callee']].add(symbol(r['callee']))
    packages={};package_deps={p:sorted({task_owner[d] for t in ts for d in tasks[t]['dependencies'] if task_owner[d]!=p}) for p,(_,ts) in PACKAGES.items()}
    depth=waves(package_deps)
    for p,(title,ts) in PACKAGES.items():
        assigned=[r for r in sites if r['package']==p];seeds=set()
        for r in assigned:
            if symbol(r['callee']):seeds.add(symbol(r['callee']))
            seeds.update(adapters[r['callee']])
        reach=closure(seeds,graph,boundaries)
        features=[f for f in catalog['features'] if set(f['tasks'])&set(ts)]
        issue_functions=[n for n in reach['functions'] if json.loads(functions[n]['quality'])]
        packages[p]=dict(id=p,title=title,tasks=ts,depends_on=package_deps[p],wave=depth[p],
            features=[dict(id=f['id'],name=f['name'],acceptance_tasks=sorted(set(f['tasks'])&set(ts)),scenarios=f['scenarios']) for f in features],
            boundary_site_ids=[r['id'] for r in assigned],native_roots=sorted({r['hook'] for r in assigned if r['hook']}),
            adapter_terminal_obligations=[r for r in adapter_rows if r['retained_callee'] in {s['callee'] for s in assigned}],
            dispositions=dict(Counter(r['disposition'] for r in assigned)),dependency_slice=reach,
            indirect_sites=[s for n in reach['functions'] for s in indirect[n]],quality_flagged_functions=issue_functions,
            direct_boundary_site_count=len(assigned),potential_boundary_roots=len({r['hook'] for r in assigned if r['hook']}),
            owned_files=sorted({path for t in ts for path in tasks[t]['execution']['input_paths'] if path.startswith(('src/','tests/'))}),
            test_targets=sorted({target for t in ts for target in tasks[t]['execution']['test_targets']}),
            changes=[dict(task=t,change=tasks[t]['change'],completion_test=tasks[t]['completion_test'],prerequisites=tasks[t]['dependencies']) for t in ts],
            acceptance='All listed feature/task cases pass for declared modes; required consumer calls removed or moved behind explicit producer contracts. No closure inferred from reduced call count.')
    memberships=defaultdict(list)
    for p,r in packages.items():
        for n in r['dependency_slice']['functions']:memberships[n].append(p)
    shared=[dict(function=n,packages=owners,disposition='shared dependency; assign contract owner before replacement, not automatically renderer work') for n,owners in sorted(memberships.items()) if len(owners)>1]
    for p,r in packages.items():
        r['shared_dependency_count']=sum(p in s['packages'] for s in shared)
        downstream={p};changed=True
        while changed:
            previous=len(downstream);downstream.update(n for n,ds in package_deps.items() if set(ds)&downstream);changed=len(downstream)>previous
        r['downstream_features']=len({f['id'] for n in downstream for f in packages[n]['features']})
        r['risk_counts']=dict(indirect_records=len(r['indirect_sites']),quality_functions=len(r['quality_flagged_functions']),external_targets=len(r['dependency_slice']['external_targets']))
        r['risk_counts']['adapter_indirect_records']=sum('INDIRECT' in s['callee'] for s in r['adapter_terminal_obligations'])
    ranked=sorted(packages,key=lambda p:(depth[p],-packages[p]['downstream_features'],-packages[p]['potential_boundary_roots'],len(packages[p]['indirect_sites']),p))
    for i,p in enumerate(ranked,1):packages[p]['rank']=i
    result=dict(version=1,atlas_id=atlas['atlas_id'],source_pins=pins,ranking='dependency wave, downstream feature breadth, boundary-root count, then indirect risk; no time estimate',
        scope='Original producers may remain only behind explicit ownership/synchronization contracts; static reachability is not replacement scope.',
        boundary_sites=sites,native_routes_without_named_original=native_only,packages=[packages[p] for p in ranked],shared_dependencies=shared,
        coverage=dict(tasks=len(tasks),features=len(catalog['features']),native_boundary_routes=len(routes),source_sites=len(sites),
                      source_site_kinds=dict(Counter(r['kind'] for r in sites)),unclassified_reentry_sites=sum(r['package']=='P00' and not r['hook'] for r in sites),
                      unique_slice_functions=len(memberships),shared_functions=len(shared)),
        limitations=['Exact sets cover observed direct edges; unresolved indirect targets can expand them.',
                     'Stopping at another native hook exposes a cut obligation, not proof that it is already safe.',
                     'Source call sites and reviewed guards are verified, but active runtime paths still need milestone evidence.',
                     'Each task and feature is assigned; full parent tasks need not finish before a bounded slice whose actual prerequisites are satisfied.'])
    result['plan_id']=digest(result)
    write(OUT/'plan.json',result)
    audit(result,catalog,routes)
    render(result,tasks)
    first=dict(id='P03.first-retained-group',package='P03',status='ready for bounded fixture work; production cutover gated',
        task=tasks['R04.pass'],roots=['sub_821D96D8','sub_821B94E8','sub_821B8E48','sub_821FE358'],
        dependency_slice=packages['P03']['dependency_slice'],
        adapter_terminal_obligations=packages['P03']['adapter_terminal_obligations'],
        sites=[s for s in sites if s['package']=='P03'],
        reuse=['tests/native_immediate_tail_tests.cpp: indexed original-vs-CPU-prefix differential cases',
               'tests/native_static_pass_replay_tests.cpp: retained static geometry to GPU readback',
               'tests/native_scene_tests.cpp: material/geometry handoff tests'],
        owned_files=['tests/native_immediate_tail_tests.cpp','tests/native_static_pass_replay_tests.cpp',
                     'tests/native_scene_tests.cpp','src/native_graphics/native_scene_handoff.h'],
        integrator_owned=['src/native_graphics/guest_shader_bridge.cpp','CMakeLists.txt'],
        completion_gate='Supported retained group reaches native GPU output with no legacy setup/activation/draw consumer call, and all required CPU writes/lifetime effects match the reference; fallback groups remain explicit.',
        fixture_gate='Exercise the actual native handoff path and real extracted CPU helper chain. A renderer-only replay cannot establish absence of guest ingestion calls.',
        no_edit=['generated/default/*','out/renderer-inventory/adapters/*'],
        provenance=dict(plan_id=result['plan_id'],atlas_id=result['atlas_id']))
    write(OUT/'first-task.json',first)
    print(json.dumps(dict(plan_id=result['plan_id'],coverage=result['coverage'],packages=[{k:p[k] for k in ('id','wave','potential_boundary_roots','downstream_features','risk_counts')} for p in result['packages']]),indent=2))

def audit(plan,catalog,routes):
    errors=[];packages={p['id']:p for p in plan['packages']}
    assigned=[t for p in packages.values() for t in p['tasks']]
    if len(assigned)!=len(set(assigned)) or set(assigned)!={t['id'] for t in catalog['tasks']}:errors.append('task partition')
    if {f['id'] for p in packages.values() for f in p['features']}!={f['id'] for f in catalog['features']}:errors.append('feature coverage')
    if {r['function'] for r in routes}!={s['hook'] for s in plan['boundary_sites'] if s['hook']}|{r['function'] for r in plan['native_routes_without_named_original']}:errors.append('boundary route coverage')
    site_ids=[s['id'] for s in plan['boundary_sites']]
    if len(site_ids)!=len(set(site_ids)):errors.append('duplicate site')
    assigned_ids=[s for p in packages.values() for s in p['boundary_site_ids']]
    if Counter(assigned_ids)!=Counter(site_ids):errors.append('site partition')
    for path,pin in plan['source_pins'].items():
        if sha(ROOT/path)!=pin:errors.append('stale '+path)
    waves({p:r['depends_on'] for p,r in packages.items()})
    report=dict(passed=not errors,errors=errors,plan_id=plan['plan_id'],coverage=plan['coverage'])
    write(OUT/'audit.json',report)
    if errors:raise ValueError(str(errors))

def render(plan,tasks):
    lines=['# Renderer critical-path replacement backlog','',
        'Generated from the source-verified atlas and coverage catalog. Counts are planning reach, not missing-function totals or completion percentages.',
        '',f"Plan `{plan['plan_id']}`; atlas `{plan['atlas_id']}`.",'',
        '## Execution order','',
        '| Rank | Package | Wave | Boundary roots | Features downstream | Indirect records / flagged functions |',
        '|---|---|---:|---:|---:|---:|']
    for p in plan['packages']:
        lines.append(f"| {p['rank']} | {p['id']} {p['title']} | {p['wave']} | {p['potential_boundary_roots']} | {p['downstream_features']} | {len(p['indirect_sites'])} / {len(p['quality_flagged_functions'])} |")
    lines+=['','Wave is dependency order, not a day estimate. Feature counts overlap and must not be summed.',
        'P00 starts with scoped contract/fixture gates; it does not require narrating all 9,216 functions before implementation.',
        'The exact site, function, boundary-cut, external-target and shared-helper sets live in `out/renderer-replacement/plan.json`.',
        'A days-scale delivery date remains unsubstantiated until the first end-to-end slice passes.','']
    for p in plan['packages']:
        lines += [f"## {p['id']} — {p['title']}",'', 'Depends on: '+(', '.join(p['depends_on']) or 'none')+'.',
            'Tasks: '+', '.join(p['tasks'])+'.',
            'Features: '+', '.join(f['id']+' '+f['name'] for f in p['features'])+'.',
            'Owner files: '+', '.join('`'+s+'`' for s in p['owned_files'])+'.',
            'Existing targets: '+', '.join('`'+s+'`' for s in p['test_targets'])+'.','',
            f"Observed sites: {p['direct_boundary_site_count']}; direct dependency slice: {len(p['dependency_slice']['functions'])} functions; shared with other packages: {p['shared_dependency_count']}.",
            'Dispositions: '+json.dumps(p['dispositions'],sort_keys=True)+'.','']
        for change in p['changes']:lines+=['- **'+change['task']+'**: '+change['change']+' Completion: '+change['completion_test']]
        lines+=['']
    lines+=['## Coordination','',
        'One integrator owns guest_shader_bridge.cpp, shared resource/retirement interfaces and acceptance. Parallel workers own bounded files/tests; shared helpers receive one contract owner before edits. No agent gets an unbounded whole-package rewrite.',
        'Run the focused target while iterating and `validate-renderer-offline.cmd` at integration. Use game boots only for declared feature/lifecycle milestones. Record failures and provenance in Epistemic.',
        'No fallback, producer, shared utility or audit call is removed merely to reduce the counts. Compatibility deletion requires a declared supported-mode decision.','']
    (ROOT/'docs/renderer-critical-path.md').write_text('\n'.join(lines),encoding='utf-8')

if __name__=='__main__':build()
