"""Expand renderer inventory with verified vtables and current native boundaries.

All classifications are inventory evidence, not assertions of runtime completeness.
The external analysis corpus is read-only; its port-status labels are not imported.
"""
import csv, hashlib, json, re, sqlite3, struct
from collections import defaultdict, deque
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
GHIDRA=ROOT/'out/ghidra/renderer-inventory'
ANALYSIS=Path('D:/roms2/edf2027-analysis')

def rows(path):
    with path.open(encoding='utf-8-sig',newline='') as stream: return list(csv.DictReader(stream))

def write(name, items, fields):
    with (OUT/name).open('w',encoding='utf-8',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields); writer.writeheader(); writer.writerows(items)

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

edges=rows(OUT/'generated-call-edges.csv')
graph=defaultdict(list)
for edge in edges: graph[edge['Caller']].append(edge)
reasons=defaultdict(set)
for function in ('sub_820A03F8','sub_820A60A0'):
    reasons[function].add('list-limit diagnostic table 8200142C verified slot target')
reasons['sub_8213BEC0'].add('graphics interrupt callback passed to VdSetGraphicsInterruptCallback by 821476A8')
reasons['sub_821BEA98'].add('vblank ticker callback registered through 82139928 by 821BEBF0')
for function,reason in [('sub_8214EBA0','worker signal callback encoded by 8213C9F0 and supported by native signal delivery'),('sub_8214EAD0','worker thread entry passed to ExCreateThread by 8214EE50')]:
    reasons[function].add(reason)
for address in json.loads((OUT/'summary.json').read_text())['roots']:
    reasons['sub_'+address].add('explicit renderer/producer entry point')
external=[]
for subsystem in ('render','model'):
    path=ANALYSIS/f'reports/subsystems/{subsystem}.csv'
    for row in rows(path):
        function=row['guest_function']
        reasons[function].add('external '+subsystem+' classification; role needs review')
        external.append(dict(function=function,subsystem=subsystem,reported_class=row['class'],
                             reported_name=row['semantic_name'],source=str(path)))

hooks=defaultdict(list); originals=[]; manifests=[]; macro_dependencies=[]; indirect_originals=[]
for path in sorted((ROOT/'src/native_graphics').glob('*')):
    if path.suffix not in ('.cpp','.h'): continue
    text=path.read_text(encoding='utf-8-sig')
    manifests.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path)))
    # Preserve character positions so source lines/offsets remain exact.
    plain=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                 lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),text,flags=re.S)
    for match in re.finditer(r'REX_HOOK\w*\(\s*(sub_[0-9A-Fa-f]{8})\s*\)\s*\{',plain):
        function='sub_'+match[1][4:].upper(); start=match.end(); end=start; depth=1
        while end<len(plain) and depth:
            depth+=(plain[end]=='{')-(plain[end]=='}'); end+=1
        if depth: raise ValueError(f'Unclosed hook body: {path}:{start}')
        body=plain[start:end-1]
        dependencies=list(re.finditer(r'\b(__imp__\w+)\s*\(',body))
        references=list(re.finditer(r'\b(__imp__\w+)\b(?!\s*\()',body))
        loc=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,match.start())+1}'
        hooks[function].append((loc,'original/CPU-tail calls present' if dependencies else 'no explicit __imp__ call in hook body'))
        reasons[function].add('current native graphics hook boundary')
        for dep in dependencies:
            offset=start+dep.start()
            originals.append(dict(hook=function,callee=dep[1],source=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,offset)+1}'))
        for dep in references:
            offset=start+dep.start()
            indirect_originals.append(dict(hook=function,callee=dep[1],source=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,offset)+1}',review='original function reference; helper invocation requires review'))
    for match in re.finditer(r'\b(EDF_[A-Z_]+)\((?:sub_)?([0-9A-Fa-f]{8})(?:,|\))',plain):
        function='sub_'+match[2].upper()
        loc=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,match.start())+1}'
        hooks[function].append((loc,'macro '+match[1]+'; inspect expansion'))
        reasons[function].add('current graphics macro boundary')
        if match[1] in ('EDF_RENDER_PHASE','EDF_MAP_TIMED_HOOK','EDF_TREE_MUTATION','EDF_RENDER_STATE_SETTER'):
            macro_dependencies.append(dict(hook=function,macro=match[1],callee='__imp__'+function,source=loc))

dbpath=ANALYSIS/'edfdb.sqlite'; imagepath=ANALYSIS/'guest_image.bin'
db=sqlite3.connect(dbpath.as_uri()+'?mode=ro&immutable=1',uri=True)
image=imagepath.read_bytes(); tables=defaultdict(list); validated=[]
names=dict(db.execute('select tbl,name from classes'))
for table,slot,function in db.execute('select tbl,slot,func from vtables order by tbl,slot'):
    address=table+slot*4; offset=address-0x82000000
    value=struct.unpack_from('>I',image,offset)[0] if 0<=offset<=len(image)-4 else 0
    matches=value==int(function[4:],16)
    row=dict(table=f'{table:08X}',slot=slot,address=f'{address:08X}',function=function,
             class_name=names.get(table,''),image_pointer=f'{value:08X}',pointer_matches=matches)
    validated.append(row)
    if matches: tables[table].append(row)
# Only expand full class tables that contain an already selected renderer/model
# method. Shared destructor/stub membership alone does not recursively select
# every class in the executable.
selected_tables={table for table,entries in tables.items() if any(e['function'] in reasons for e in entries)}
# Integrate subsequent constructor/receiver investigations explicitly. Do not
# recursively select unrelated tables through their shared empty stubs.
family_path=OUT/'renderable-constructor-candidates.csv'
if family_path.exists():
    for row in rows(family_path):
        selected_tables.add(int(row['table'],16))
for table in selected_tables:
    for row in tables[table]: reasons[row['function']].add(f'verified table {table:08X} ({row["class_name"]}) slot {row["slot"]}')
if family_path.exists():
    for row in rows(family_path):
        reasons[row['candidate_constructor']].add('renderable family constructor investigation')
for filename in ('registration-instructions.csv','renderer-state-instructions.csv'):
    path=OUT/filename
    if path.exists():
        for function in {r['function'] for r in rows(path)}:
            reasons['sub_'+function].add('focused Ghidra investigation: '+filename)
reentry_path=OUT/'native-reentry-sites.csv'
if reentry_path.exists():
    for row in rows(reentry_path):
        target=row['target'].removeprefix('__imp__')
        if re.fullmatch(r'sub_[0-9A-Fa-f]{8}',target):
            reasons[target].add('native source guest reentry target')

for filename in ('device-state-table.csv','device-sampler-table.csv'):
    path=OUT/filename
    if path.exists():
        for row in rows(path):
            for role in ('getter','setter'):
                target=row[role]
                reasons[target].add('initialized device '+role+' from '+filename+' record '+row['table_record'])

reached=set(); todo=deque(reasons)
while todo:
    function=todo.popleft()
    if function in reached: continue
    reached.add(function)
    todo.extend(e['Callee'] for e in graph[function] if e['Callee'].startswith('sub_'))
inventory=[]
for function in sorted(reached):
    calls=graph[function]; owners=hooks.get(function,[])
    inventory.append(dict(function=function,root_reasons=' | '.join(sorted(reasons[function])),
        native_boundary=' | '.join(x[0] for x in owners),boundary_evidence=' | '.join(x[1] for x in owners),
        direct_sites=sum(bool(e['Callee']) for e in calls),indirect_sites=sum(bool(e['IndirectTarget']) for e in calls),
        generated_file=calls[0]['File'] if calls else '',review='not yet semantically classified'))
write('complete-function-inventory.csv',inventory,list(inventory[0]))
write('native-original-dependencies.csv',originals,['hook','callee','source'])
write('native-original-references.csv',indirect_originals,['hook','callee','source','review'])
write('macro-original-dependencies.csv',macro_dependencies,['hook','macro','callee','source'])
write('external-role-evidence.csv',external,list(external[0]))
write('verified-vtable-pointers.csv',validated,list(validated[0]))
write('native-source-manifest.csv',manifests,['path','sha256'])
indirect=[e for e in edges if e['Caller'] in reached and e['IndirectTarget']]
write('complete-indirect-sites.csv',indirect,list(edges[0]))
write('root-manifest.csv',[dict(function=k,reasons=' | '.join(sorted(v))) for k,v in sorted(reasons.items()) if v],['function','reasons'])
GHIDRA.mkdir(parents=True,exist_ok=True)
(GHIDRA/'inventory-roots.txt').write_text('\n'.join(f[4:] for f in sorted(reasons) if reasons[f])+'\n')
(GHIDRA/'inventory-functions.txt').write_text('\n'.join(f[4:] for f in sorted(reached))+'\n')
# Verify every database pointer against the currently loaded Ghidra image too.
with (GHIDRA/'candidate-vtables.tsv').open('w') as stream:
    for row in validated: stream.write(f'{row["address"]}\t{row["image_pointer"]}\n')
summary=dict(root_functions=sum(bool(v) for v in reasons.values()),reachable_functions=len(reached),
    indirect_sites=len(indirect),native_boundaries=len(hooks),original_dependency_sites=len(originals),macro_original_dependency_sites=len(macro_dependencies),original_function_references=len(indirect_originals),
    selected_tables=len(selected_tables),database_table_pointers=len(validated),
    mismatching_image_pointers=sum(not x['pointer_matches'] for x in validated),
    external_database_sha256=sha(dbpath),external_image_sha256=sha(imagepath),
    note='Expanded coverage census; semantic role and indirect-target resolution remain required. External completion labels excluded.')
(OUT/'complete-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
