"""Partition every current native boundary using existing source evidence."""
import csv,hashlib,json
from collections import Counter,defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
def read(name):
 with (OUT/name).open(encoding='utf-8-sig') as f:return list(csv.DictReader(f))
explicit=defaultdict(list);macros=defaultdict(list);references=defaultdict(list)
for r in read('native-dependency-classification.csv'):explicit[r['hook']].append(r)
for r in read('macro-original-dependencies.csv'):macros[r['hook']].append(r)
for r in read('native-original-references.csv'):references[r['hook']].append(r)
special={
'sub_8213BD90':('native cursor update','Snapshots/publishes native cursor and mirrors device+10820; returns (old cursor+words)&mask. Hook has no explicit guest call.','guest_shader_bridge.cpp:5657-5670; helper and value equivalence remain scoped'),
'sub_8213C410':('native submission with guest observer callbacks','Copies descriptor word-count/address pairs before SubmitOwnedNativeDescriptors; helper invokes ResolveIndirectFunction for observers, validates cursor generation and submits native frame/ranges.','guest_shader_bridge.cpp:5672-5738; observer target population remains open'),
'sub_82201458':('original passed to texture helper','ImportTexture receives __imp__sub_82201458; invokes original on disabled bridge path and upload path. Absence of direct hook call is not replacement.','guest_shader_bridge.cpp:6404,1254,1278'),
'sub_821409A0':('native untiled replacement','Known-caller guard, paired device insertion, success return; no original call.','untiled-boundary-contracts.csv:sub_821409A0'),
'sub_82140E98':('native untiled replacement','Known-caller guard, paired device erasure, success return; no original call.','untiled-boundary-contracts.csv:sub_82140E98')}
bridge=ROOT/'src/native_graphics/guest_shader_bridge.cpp';sha=hashlib.sha256(bridge.read_bytes()).hexdigest()
rows=[]
for r in read('complete-function-inventory.csv'):
 fn=r['function']
 if not r['native_boundary']:continue
 groups=sum(bool(x) for x in (explicit[fn],macros[fn],fn in special));assert groups==1,(fn,groups)
 if explicit[fn]:
  route='explicit original/adapter/import call paths'
  detail=' | '.join(sorted({p['path_class'] for p in explicit[fn]}));source='native-dependency-classification.csv:'+fn
 elif macros[fn]:
  route='macro original forwarding';detail=' | '.join(sorted({p['macro'] for p in macros[fn]}));source='macro-original-dependencies.csv:'+fn
 else:route,detail,source=special[fn]
 rows.append(dict(function=fn,route=route,detail=detail,evidence=source,
  explicit_call_sites=len(explicit[fn]),macro_call_sites=len(macros[fn]),original_reference_sites=len(references[fn]),
  bridge_sha256=sha,remaining='route inventory only; branch activation, transitive helper effects and implementation parity are not inferred'))
assert {r['function'] for r in rows if r['function'] in special}==set(special)
with (OUT/'native-boundary-routes.csv').open('w',encoding='utf-8',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=dict(boundaries=len(rows),routes=dict(Counter(r['route'] for r in rows)),
 explicit_call_sites=sum(r['explicit_call_sites'] for r in rows),macro_call_sites=sum(r['macro_call_sites'] for r in rows),
 original_reference_sites=sum(r['original_reference_sites'] for r in rows),
 limitation='Exhaustive partition of current src/native_graphics boundary census only; category counts are not missing-port counts.')
(OUT/'native-boundary-route-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
