"""Build/audit a complete renderer census atlas; no semantic completion inferred."""
import argparse
import csv
from collections import Counter, defaultdict
import hashlib
import html
import json
from pathlib import Path
import re
import sqlite3

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/renderer-atlas'

def sha(data): return hashlib.sha256(data).hexdigest()
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def csvrows(path):
    with Path(path).open(encoding='utf-8-sig',newline='') as stream:return list(csv.DictReader(stream))
def dump(path,data):
    Path(path).parent.mkdir(parents=True,exist_ok=True)
    Path(path).write_text(json.dumps(data,indent=2,sort_keys=True)+'\n',encoding='utf-8')
def esc(value): return html.escape(str(value))
def canonical(address): return 'sub_'+address[-8:].upper()

def decompiler_globals(code):
    # Ghidra's auto-named globals are symbolic evidence, not validated memory ownership.
    return sorted({(m[0],m[1][-8:].upper()) for m in re.findall(
        r'\b((?:[A-Za-z_][A-Za-z_0-9]*Ram|DAT_|PTR_|FLOAT_|DOUBLE_|UNK_)([0-9A-Fa-f]{8,16}))\b',code)})


def cfg(instructions):
    """Instruction-level CFG. Call targets are call edges, not intraprocedural edges."""
    sites={i['address'] for i in instructions};edges=[]
    for ins in instructions:
        targets=[] if ins.get('call') else [(a,'branch') for a in ins.get('flows',[])]
        if ins.get('fallthrough'): targets.append((ins['fallthrough'],'fallthrough'))
        for target,kind in targets:
            edges.append((ins['address'],target,kind,target in sites))
    return sorted(set(edges))


def components(nodes,edges):
    """Iterative Kosaraju: SCCs are dependency cycles, not semantic subsystems."""
    forward={n:set() for n in nodes};reverse={n:set() for n in nodes}
    for a,b in edges:
        if a in forward and b in forward:forward[a].add(b);reverse[b].add(a)
    seen=set();order=[]
    for root in sorted(nodes):
        if root in seen:continue
        stack=[(root,False)]
        while stack:
            node,finish=stack.pop()
            if finish:order.append(node);continue
            if node in seen:continue
            seen.add(node);stack.append((node,True))
            stack.extend((n,False) for n in sorted(forward[node],reverse=True) if n not in seen)
    seen=set();groups=[]
    for root in reversed(order):
        if root in seen:continue
        group=[];stack=[root];seen.add(root)
        while stack:
            node=stack.pop();group.append(node)
            for n in sorted(reverse[node]):
                if n not in seen:seen.add(n);stack.append(n)
        groups.append(sorted(group))
    return sorted(groups,key=lambda g:g[0])


def source_bodies(names):
    found={};pins={}
    pattern=re.compile(r'^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\) \{\n.*?^\}',re.M|re.S)
    for path in sorted((ROOT/'generated/default').glob('*recomp*.cpp')):
        text=path.read_text();pins[path.relative_to(ROOT).as_posix()]=sha(path.read_bytes())
        for match in pattern.finditer(text):
            symbol=match[1]
            if symbol not in names:continue
            if symbol in found:raise ValueError('Duplicate generated body '+symbol)
            found[symbol]=(path.relative_to(ROOT).as_posix(),text.count('\n',0,match.start())+1,match[0])
    return found,pins


def native_refs(names):
    refs=defaultdict(list);pins={}
    paths=list((ROOT/'src/native_graphics').rglob('*.cpp'))+list((ROOT/'src/native_graphics').rglob('*.h'))
    for path in sorted(paths):
        text=path.read_text(encoding='utf-8',errors='replace');relative=path.relative_to(ROOT).as_posix()
        pins[relative]=sha(path.read_bytes())
        for line_no,line in enumerate(text.splitlines(),1):
            for symbol in sorted(set(re.findall(r'\b(?:__imp__)?(sub_[0-9A-Fa-f]{8})\b',line))):
                symbol='sub_'+symbol[4:].upper()
                if symbol in names:refs[symbol].append(dict(path=relative,line=line_no,text=line.strip()))
    return refs,pins


STYLE='''<style>body{font:15px system-ui;max-width:1450px;margin:32px auto;padding:0 22px;background:#101725;color:#e6edf7}a{color:#80c9ff}h1{font-size:32px}small,.muted{color:#a5b4c8}input,select{padding:10px;background:#202c40;color:white;border:1px solid #52657e;margin:5px}table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:9px;border-bottom:1px solid #304057;vertical-align:top}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#192438;padding:16px;border-radius:8px}summary{cursor:pointer;padding:12px;background:#24334b}nav{margin-bottom:20px}.card{display:inline-block;padding:18px;background:#23314a;margin:6px;border-radius:8px}button{padding:10px;margin:6px;cursor:pointer}details{margin:14px 0}</style>'''
def page(title,body):return '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>'+esc(title)+'</title>'+STYLE+'<body>'+body+'</body></html>'
def links(names,prefix=''):
    return ' · '.join(f'<a href="{prefix}{esc(n)}.html">{esc(n)}</a>' for n in sorted(set(names))) or 'None recorded'


def build(output=OUT):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    context=read(OUT/'export-context.json');facts_dir=ROOT/context['facts']
    export_summary=read(facts_dir/'export-summary.json')
    census=csvrows(ROOT/'out/renderer-inventory/complete-function-inventory.csv')
    names={r['function'] for r in census}
    if len(names)!=9216 or len(census)!=len(names):raise ValueError('Census must contain exactly9216unique functions')
    if export_summary['context']!=context['context'] or export_summary['processed']!=len(names):raise ValueError('Incomplete export')
    findings={r['function']:r for r in csvrows(ROOT/'out/renderer-inventory/renderer-function-findings.csv')}
    routes={r['function']:r for r in csvrows(ROOT/'out/renderer-inventory/native-boundary-routes.csv')}
    tally=read(ROOT/'out/renderer-tally/tally.json');catalog=read(ROOT/'docs/renderer-coverage.json')
    tasks={r['id']:r for r in catalog['tasks']}
    sources,pins=source_bodies(names);native,npins=native_refs(names);pins.update(npins)
    retry_path=OUT/'retry-context.json'
    if retry_path.exists():
        retry=read(retry_path)
        if retry['context']!=context['context']:raise ValueError('Retry belongs to another export context')
        pins.update(retry['inputs']);pins[retry_path.relative_to(ROOT).as_posix()]=sha(retry_path.read_bytes())
    for path in ['tools/renderer_atlas.py','tools/ghidra/RendererAtlas.java','tools/export-renderer-atlas.ps1',
                 'out/renderer-inventory/complete-function-inventory.csv','out/renderer-inventory/generated-call-edges.csv',
                 'out/renderer-inventory/renderer-function-findings.csv','out/renderer-inventory/native-boundary-routes.csv',
                 'out/renderer-tally/tally.json','docs/renderer-coverage.json','out/renderer-atlas/export-context.json']:
        pins[path]=sha((ROOT/path).read_bytes())
    db_path=output/'atlas.sqlite.tmp'
    if db_path.exists():db_path.unlink()
    db=sqlite3.connect(db_path)
    db.executescript('''
      CREATE TABLE functions(symbol TEXT PRIMARY KEY,entry TEXT,status TEXT,task TEXT,lane TEXT,decompiled INTEGER,
        instruction_count INTEGER,body_bytes INTEGER,quality TEXT,body_hash TEXT,shape_hash TEXT,generated_source TEXT,
        generated_line INTEGER,decompiled_c TEXT,generated_c TEXT,inventory TEXT,findings TEXT,route TEXT,export_hash TEXT);
      CREATE TABLE instructions(function TEXT,address TEXT,bytes TEXT,mnemonic TEXT,text TEXT,pcode TEXT,inputs TEXT,outputs TEXT,PRIMARY KEY(function,address));
      CREATE TABLE cfg(function TEXT,source TEXT,target TEXT,kind TEXT,internal INTEGER);
      CREATE TABLE calls(caller TEXT,callee TEXT,site TEXT,kind TEXT,provenance TEXT,PRIMARY KEY(caller,callee,site,kind,provenance));
      CREATE TABLE refs(function TEXT,site TEXT,target TEXT,space TEXT,type TEXT,is_data INTEGER);
      CREATE TABLE global_symbols(function TEXT,symbol TEXT,address TEXT,provenance TEXT);
      CREATE TABLE native_refs(function TEXT,path TEXT,line INTEGER,text TEXT);
      CREATE TABLE clusters(id TEXT,function TEXT,kind TEXT,PRIMARY KEY(id,function,kind));
      CREATE TABLE tasks(id TEXT PRIMARY KEY,title TEXT,status TEXT,details TEXT);
      CREATE TABLE metadata(key TEXT PRIMARY KEY,value TEXT);
      CREATE VIRTUAL TABLE search USING fts5(symbol UNINDEXED,text);
      CREATE INDEX calls_callee ON calls(callee); CREATE INDEX refs_target ON refs(target);
      CREATE INDEX cfg_function ON cfg(function); CREATE INDEX refs_function ON refs(function);
    ''')
    calls=set();records=[];facts_hashes={};boundary_disagreements=[]
    for r in csvrows(ROOT/'out/renderer-inventory/generated-call-edges.csv'):
        if r['Caller'] not in names:continue
        target=r['Callee'] or '?'+r['IndirectTarget']
        calls.add((r['Caller'],target,r['File']+':offset='+r['SourceOffset'],
                   'direct' if r['Callee'] else 'indirect','generated-source'))
    function_dir=output/'functions';function_dir.mkdir(exist_ok=True)
    for inventory in sorted(census,key=lambda r:r['function']):
        name=inventory['function'];fact_path=facts_dir/(name[4:]+'.json')
        data=fact_path.read_bytes();fact=json.loads(data);facts_hashes[name]=sha(data)
        if fact['context']!=context['context'] or fact['function']!=name:raise ValueError('Stale/mismatched export '+name)
        instructions=fact['instructions'];quality=list(fact['quality']);generated=sources.get(name,('',0,''))
        old_size=inventory.get('ghidra_body_bytes','')
        if old_size and int(old_size)!=fact.get('body_bytes',0):
            quality.append('body-size-differs-from-prior-inventory');boundary_disagreements.append(name)
        if not generated[2]:quality.append('missing-generated-body')
        entity=tally['entities'].get('function:'+name)
        if not entity:raise ValueError('Missing tally entity '+name)
        task=entity['task']
        if task not in tasks:raise ValueError('Missing task '+task)
        edges=cfg(instructions);sites={i['address'] for i in instructions}
        for ins in instructions:
            db.execute('INSERT INTO instructions VALUES(?,?,?,?,?,?,?,?)',(name,ins['address'],ins['bytes'],ins['mnemonic'],ins['text'],json.dumps(ins['pcode_ops']),json.dumps(ins['inputs']),json.dumps(ins['outputs'])))
            if ins.get('call') or (ins.get('jump') and (ins.get('computed') or any(a not in sites for a in ins.get('flows',[])))):
                for target in ins.get('flows',[]):calls.add((name,canonical(target),ins['address'],'call' if ins.get('call') else 'tail/branch','ghidra-flow'))
                if ins.get('computed') and not ins.get('flows'):calls.add((name,'?indirect',ins['address'],'indirect','ghidra-flow'))
            for ref in ins.get('references',[]):
                db.execute('INSERT INTO refs VALUES(?,?,?,?,?,?)',(name,ins['address'],ref['target'],ref['space'],ref['type'],ref['data']))
        db.executemany('INSERT INTO cfg VALUES(?,?,?,?,?)',[(name,*e) for e in edges])
        for ref in native[name]:db.execute('INSERT INTO native_refs VALUES(?,?,?,?)',(name,ref['path'],ref['line'],ref['text']))
        computed=sum(i.get('computed',False) and i.get('call',False) for i in instructions)
        memory=sum('LOAD' in i['pcode_ops'] or 'STORE' in i['pcode_ops'] for i in instructions)
        floating=any(i['mnemonic'].startswith('f') or i['mnemonic'] in ('lfs','lfd','stfs','stfd') for i in instructions)
        branches=sum(i.get('jump',False) for i in instructions)
        lane=('export-gap' if not instructions else 'decompiler-gap' if not fact['decompile_completed'] else
              'indirect-control' if computed else 'floating-point' if floating else 'control-flow' if branches else
              'memory-effects' if memory else 'straight-line')
        body_hash=sha(bytes.fromhex(''.join(i['bytes'] for i in instructions))) if instructions else ''
        shape_hash=sha(json.dumps([(i['mnemonic'],i['pcode_ops'],i['flow_type']) for i in instructions]).encode()) if instructions else ''
        code=fact.get('decompiled_c','');finding=findings.get(name,{})
        globals_=decompiler_globals(code)
        db.executemany('INSERT INTO global_symbols VALUES(?,?,?,?)',[(name,s,a,'decompiler-auto-symbol') for s,a in globals_])
        db.execute('INSERT INTO functions VALUES('+','.join('?'*19)+')',(name,name[4:],entity['status'],task,lane,
            fact['decompile_completed'],len(instructions),fact.get('body_bytes',0),json.dumps(quality),body_hash,shape_hash,
            generated[0],generated[1],code,generated[2],json.dumps(inventory),json.dumps(finding),json.dumps(routes.get(name,{})),facts_hashes[name]))
        db.execute('INSERT INTO search VALUES(?,?)',(name,'\n'.join([code,generated[2],json.dumps(finding),json.dumps(inventory),json.dumps(native[name])])))
        records.append(dict(symbol=name,status=entity['status'],task=task,lane=lane,decompiled=fact['decompile_completed'],
            instructions=len(instructions),quality=quality,body_hash=body_hash,shape_hash=shape_hash,native=len(native[name]),
            code=code,generated=generated,inventory=inventory,finding=finding,fact=fact,cfg=edges,
            native_refs=native[name],route=routes.get(name,{}),globals=globals_))
    db.executemany('INSERT INTO calls VALUES(?,?,?,?,?)',sorted(calls))
    incoming=defaultdict(set);outgoing=defaultdict(set)
    for a,b,site,kind,provenance in calls:
        if not b.startswith('?'):outgoing[a].add(b);incoming[b].add(a)
    sccs=components(names,[(a,b) for a in outgoing for b in outgoing[a]])
    clusters=[]
    for members in sccs:
        if len(members)>1:clusters.append(dict(id='scc-'+members[0],kind='dependency-cycle',members=members))
    for field,kind in [('body_hash','identical-bytes'),('shape_hash','structural-candidate')]:
        groups=defaultdict(list)
        for r in records:
            if r[field]:groups[r[field]].append(r['symbol'])
        for key,members in sorted(groups.items()):
            if len(members)>1:clusters.append(dict(id=kind+'-'+key[:16],kind=kind,members=members))
    shared=defaultdict(set)
    for function,target in db.execute("SELECT function,target FROM refs WHERE is_data=1 AND space NOT IN ('register','stack','constant','unique')"):
        shared[target].add(function)
    for target,members in sorted(shared.items()):
        if len(members)>1:clusters.append(dict(id='data-reference-'+target,kind='shared-data-reference',members=sorted(members)))
    symbolic=defaultdict(set)
    for function,address in db.execute('SELECT function,address FROM global_symbols'):symbolic[address].add(function)
    for address,members in sorted(symbolic.items()):
        if len(members)>1:clusters.append(dict(id='global-symbol-'+address,kind='shared-global-symbol-candidate',members=sorted(members)))
    for c in clusters:db.executemany('INSERT INTO clusters VALUES(?,?,?)',[(c['id'],n,c['kind']) for n in c['members']])
    for task in tasks.values():db.execute('INSERT INTO tasks VALUES(?,?,?,?)',(task['id'],task['title'],task['status'],json.dumps(task)))
    summary=dict(version=1,census=len(names),exported=len(records),decompiled=sum(r['decompiled'] for r in records),
        decompilation_gaps=[r['symbol'] for r in records if not r['decompiled']],
        generated_bodies=len(sources),missing_generated=sorted(names-set(sources)),
        warning_functions=sum(bool(r['quality']) for r in records),body_size_disagreements=boundary_disagreements,
        instructions=sum(r['instructions'] for r in records),call_records=len(calls),
        unresolved_call_records=sum(c[1].startswith('?') for c in calls),
        global_symbol_addresses=len(symbolic),native_mentioned_functions=sum(bool(r['native']) for r in records),
        external_targets=sorted({b for a,b,*_ in calls if not b.startswith('?') and b not in names}),
        lanes=dict(Counter(r['lane'] for r in records)),statuses=dict(Counter(r['status'] for r in records)),
        task_counts=dict(Counter(r['task'] for r in records)),cluster_counts=dict(Counter(c['kind'] for c in clusters)),
        context=context['context'],tally_snapshot=tally['snapshot_id'],
        limits=['Census includes reachable shared utilities; membership is not proof of renderer relevance.',
                'Decompiler completion means C output returned, not semantic completeness. Warnings and gaps are preserved.',
                'CFG is instruction-level; computed targets remain unresolved. Data references are detected references, not all runtime globals.',
                'Clusters are navigation candidates, not equivalence or replacement authority.',
                'Native symbol mentions include comments and declarations; they are not verified execution paths.'])
    manifest=dict(inputs=pins,export_context=context,export_hashes=facts_hashes,summary=summary)
    manifest['atlas_id']=sha(json.dumps(manifest,sort_keys=True).encode())
    for key,value in [('atlas_id',manifest['atlas_id']),('summary',summary),('provenance',manifest)]:db.execute('INSERT INTO metadata VALUES(?,?)',(key,json.dumps(value)))
    db.commit();db.execute("INSERT INTO search(search) VALUES('integrity-check')");db.close();db_path.replace(output/'atlas.sqlite')
    dump(output/'manifest.json',manifest);dump(output/'summary.json',summary);dump(output/'clusters.json',clusters)
    render(output,records,incoming,outgoing,tasks,clusters,summary)
    body='<a href="index.html">Atlas</a><h1>Features, scenarios and modes</h1><p>Planning and recorded runtime status, not inferred implementation parity.</p>'
    for category in ('features','scenarios','configuration_modes'):
        body+='<h2>'+esc(category)+'</h2>'
        for item in catalog[category]:body+='<details><summary>'+esc(item.get('id',item.get('name','entry')))+'</summary><pre>'+esc(json.dumps(item,indent=2))+'</pre></details>'
    (output/'coverage.html').write_text(page('Renderer feature coverage',body),encoding='utf-8')
    manifest['outputs']={p.relative_to(output).as_posix():sha(p.read_bytes()) for p in
                         sorted(list(output.glob('*.html'))+list((output/'functions').glob('*.html'))+[output/'atlas.sqlite',output/'summary.json',output/'clusters.json'])}
    dump(output/'manifest.json',manifest)
    audit(output)
    print(json.dumps({k:summary[k] for k in ('census','exported','decompiled','generated_bodies','instructions','call_records','lanes','cluster_counts')},indent=2))


def render(output,records,incoming,outgoing,tasks,clusters,summary):
    names={r['symbol'] for r in records};memberships=defaultdict(list)
    for c in clusters:
        for n in c['members']:memberships[n].append(c['id'])
    index=[]
    for r in records:
        name=r['symbol'];fact=r['fact'];task=tasks[r['task']]
        body='<nav><a href="../index.html">All functions</a> · <a href="../clusters.html">Clusters</a> · <a href="../tasks.html#'+esc(r['task'])+'">Task</a></nav>'
        body+='<h1>'+name+'</h1><p>'+esc(r['status'])+' · '+esc(r['lane'])+' · '+str(r['instructions'])+' instructions</p>'
        body+='<p>Decompiled: '+str(r['decompiled'])+' · Quality: '+esc(', '.join(r['quality']) or 'No recorded warnings; not a correctness proof')+'</p>'
        body+='<p>'+esc(fact.get('decompile_error',''))+'</p><p>Task '+esc(r['task'])+': '+esc(task['title'])+'</p>'
        body+='<p>'+esc(fact.get('signature',''))+'</p><p>Body ranges: '+esc(json.dumps(fact['body_ranges']))+'</p>'
        body+='<p><a href="../exports/'+esc(fact['context'])+'/'+name[4:]+'.json">Raw export / provenance</a></p>'
        body+='<h2>Callers</h2>'+links(incoming[name]&names)+'<h2>Callees / branch targets</h2>'+links(outgoing[name]&names)
        body+='<p>Outside census: '+esc(', '.join(sorted(outgoing[name]-names)) or 'None recorded')+'</p>'
        body+='<details><summary>Inventory, findings and cluster membership</summary><pre>'+esc(json.dumps(dict(inventory=r['inventory'],findings=r['finding'],clusters=memberships[name]),indent=2))+'</pre></details>'
        body+='<details><summary>Native references and recorded boundary route</summary><pre>'+esc(json.dumps(dict(references=r['native_refs'],route=r['route']),indent=2))+'</pre></details>'
        body+='<details><summary>Decompiler global symbols (candidate references)</summary><pre>'+esc(json.dumps(r['globals'],indent=2))+'</pre></details>'
        body+='<details open><summary>Decompiled C (analysis aid)</summary><pre>'+esc(r['code'] or 'No C output; see explicit error above.')+'</pre></details>'
        body+='<details><summary>Generated executable body — '+esc(r['generated'][0])+':'+str(r['generated'][1])+'</summary><pre>'+esc(r['generated'][2] or 'No generated body matched')+'</pre></details>'
        body+='<details><summary>Instructions / raw bytes / p-code operations</summary><pre>'+esc('\n'.join(i['address']+' '+i['bytes']+' '+i['text']+'  ['+','.join(i['pcode_ops'])+']' for i in fact['instructions']))+'</pre></details>'
        body+='<details><summary>Instruction CFG edges (external edges retained)</summary><pre>'+esc('\n'.join(f'{a} → {b} {kind} {"internal" if internal else "external"}' for a,b,kind,internal in r['cfg']))+'</pre></details>'
        body+='<details><summary>References and input/output operands</summary><pre>'+esc(json.dumps([dict(site=i['address'],refs=i['references'],inputs=i['inputs'],outputs=i['outputs']) for i in fact['instructions'] if i['references']],indent=2))+'</pre></details>'
        (output/'functions'/f'{name}.html').write_text(page(name,body),encoding='utf-8')
        index.append([name,r['lane'],r['status'],r['task'],r['instructions'],len(r['quality']),r['native'],r['decompiled'],r['inventory'].get('root_reasons','')])
    body='<h1>Renderer atlas</h1><p>Complete declared census · static evidence, not completion claims</p>'
    body+=''.join('<span class="card">'+esc(k)+'<br><b>'+str(summary[k])+'</b></span>' for k in ('census','decompiled','generated_bodies','instructions'))
    body+='<nav><a href="clusters.html">Dependency and similarity clusters</a> · <a href="tasks.html">Tasks and acceptance</a> · <a href="coverage.html">Features / modes / scenarios</a> · <a href="summary.json">Coverage / gaps</a> · <a href="atlas.sqlite">SQLite / full-text index</a></nav>'
    body+='<p>Search symbols and root reasons below. Full-text C / source search: <code>python tools/renderer_atlas.py search "query"</code>.</p><input id="q" placeholder="Symbol or root reason" aria-label="Search functions"><select id="lane"><option value="">All lanes</option></select><select id="status"><option value="">All statuses</option></select><label><input type="checkbox" id="gaps">Only warnings / gaps</label><p id="count"></p><button id="prev">Previous</button><button id="next">Next</button><table><thead><tr><th>Function</th><th>Lane</th><th>Status</th><th>Task</th><th>Instructions</th><th>Warnings</th><th>Native mentions</th></tr></thead><tbody id="rows"></tbody></table>'
    data=json.dumps(index).replace('<','\\u003c')
    body+='<script>const data='+data+''';let pageNo=0;const $=id=>document.getElementById(id);
for(const [id,col] of [['lane',1],['status',2]])for(const value of [...new Set(data.map(r=>r[col]))].sort()){let o=document.createElement('option');o.value=o.textContent=value;$(id).append(o)}
function draw(){let q=$('q').value.toLowerCase();let items=data.filter(r=>(!q||(r[0]+' '+r[8]).toLowerCase().includes(q))&&(!$('lane').value||r[1]===$('lane').value)&&(!$('status').value||r[2]===$('status').value)&&(!$('gaps').checked||r[5]||!r[7]));pageNo=Math.min(pageNo,Math.max(0,Math.ceil(items.length/100)-1));$('count').textContent=items.length+' functions · page '+(pageNo+1);$('rows').replaceChildren();for(const r of items.slice(pageNo*100,pageNo*100+100)){let tr=document.createElement('tr');r.slice(0,7).forEach((v,i)=>{let td=document.createElement('td');if(i===0){let a=document.createElement('a');a.href='functions/'+r[0]+'.html';a.textContent=v;td.append(a)}else td.textContent=v;tr.append(td)});$('rows').append(tr)}}
for(const id of ['q','lane','status','gaps'])$(id).addEventListener('input',()=>{pageNo=0;draw()});$('prev').onclick=()=>{pageNo=Math.max(0,pageNo-1);draw()};$('next').onclick=()=>{pageNo++;draw()};draw();</script>'''
    (output/'index.html').write_text(page('Renderer atlas',body),encoding='utf-8')
    body='<a href="index.html">Atlas</a><h1>Dependency and similarity clusters</h1><p>SCCs are call-graph cycles. Identical bytes and structural matches are review candidates, not proven equivalence.</p>'
    for c in sorted(clusters,key=lambda c:(c['kind'],-len(c['members']),c['id'])):
        body+='<details><summary>'+esc(c['kind'])+' · '+str(len(c['members']))+' functions · '+esc(c['id'])+'</summary>'+links(c['members'],'functions/')+'</details>'
    (output/'clusters.html').write_text(page('Renderer clusters',body),encoding='utf-8')
    body='<a href="index.html">Atlas</a><h1>Tasks / feature acceptance</h1>'
    for tid,task in sorted(tasks.items()):
        body+='<section id="'+esc(tid)+'"><h2>'+esc(tid+' — '+task['title'])+'</h2><p>'+esc(task['status'])+'</p><pre>'+esc(json.dumps(task,indent=2))+'</pre></section>'
    (output/'tasks.html').write_text(page('Renderer tasks',body),encoding='utf-8')


def audit(output=OUT):
    output=Path(output);m=read(output/'manifest.json');errors=[]
    db=sqlite3.connect('file:'+str((output/'atlas.sqlite').resolve()).replace('\\','/')+'?mode=ro',uri=True)
    if db.execute('PRAGMA integrity_check').fetchone()[0]!='ok':errors.append('SQLite integrity')
    names={r[0] for r in db.execute('SELECT symbol FROM functions')}
    if names!={r['function'] for r in csvrows(ROOT/'out/renderer-inventory/complete-function-inventory.csv')}:errors.append('Census mismatch')
    if db.execute('SELECT count(*) FROM search').fetchone()[0]!=len(names):errors.append('FTS coverage mismatch')
    for name in names:
        if not (output/'functions'/f'{name}.html').is_file():errors.append('Missing page '+name)
    for path,expected in m['inputs'].items():
        if sha((ROOT/path).read_bytes())!=expected:errors.append('Stale input '+path)
    for path,expected in m.get('outputs',{}).items():
        if sha((output/path).read_bytes())!=expected:errors.append('Changed atlas output '+path)
    identity={k:v for k,v in m.items() if k not in ('outputs','atlas_id')}
    if sha(json.dumps(identity,sort_keys=True).encode())!=m['atlas_id']:errors.append('Manifest identity mismatch')
    for path,expected in m['export_context']['inputs'].items():
        if '/' in path and sha((ROOT/path).read_bytes())!=expected:errors.append('Stale Ghidra context '+path)
    facts_dir=ROOT/m['export_context']['facts']
    for name,expected in m['export_hashes'].items():
        if sha((facts_dir/(name[4:]+'.json')).read_bytes())!=expected:errors.append('Stale export '+name)
    if db.execute('SELECT count(*) FROM functions f LEFT JOIN tasks t ON f.task=t.id WHERE t.id IS NULL').fetchone()[0]:errors.append('Unassigned tasks')
    if db.execute('SELECT count(*) FROM instructions').fetchone()[0]!=m['summary']['instructions']:errors.append('Instruction tally mismatch')
    db.close();report=dict(passed=not errors,errors=errors,functions=len(names),atlas_id=m['atlas_id'])
    dump(output/'audit.json',report)
    if errors:raise ValueError('; '.join(errors[:10]))
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['build','audit','search']);p.add_argument('query',nargs='?');args=p.parse_args()
    if args.action=='build':build()
    elif args.action=='audit':print(json.dumps(audit(),indent=2))
    else:
        if not args.query:p.error('search requires an FTS5 query')
        db=sqlite3.connect('file:'+str(OUT/'atlas.sqlite').replace('\\','/')+'?mode=ro',uri=True)
        for row in db.execute('SELECT symbol,snippet(search,1,\'[\',\']\',\'…\',24) FROM search WHERE search MATCH ? ORDER BY rank LIMIT 30',(args.query,)):print(row[0],row[1])

if __name__=='__main__':main()
