"""Join focused findings without converting evidence coverage into port status."""
import csv,json
from collections import Counter,defaultdict
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
def read(name):
 with (OUT/name).open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
inventory=read('complete-function-inventory.csv');known={r['function'] for r in inventory}
notes=defaultdict(set);sources=defaultdict(set);roles=defaultdict(set);obligations=defaultdict(set)
def add(fn,role,note,source,remaining):
 assert fn in known, f'Focused finding absent from broad census: {fn}'
 roles[fn].add(role);notes[fn].add(note);sources[fn].add(source)
 if remaining:obligations[fn].add(remaining)
groups={
 'static list selection':'820B2670 820BAF90',
 'model-backed object':'820BB270 820BBA48 821152D0 82115AE8 82117298 82118648 82120168 8218A658',
 'animated actor/vehicle':'820D7448 820DEA08 820E7FB8 820EA398 820ECCC0 820F5630 820F9928 820FFFC0 82100D00 82108BE8 8210E6C0 8219A2D0 821E2250 821E5810',
 'composite/broken model':'820EC180 8211FAA8',
 'projectile/particle/glass':'82113308 82114A98 82117CD0 82119A10 8211A0E8 8211B088 8211BB80 8211D250 8211DB70 8217D6E0',
 'line/spark/muzzle effect':'820B8D28 8211E7A0 8211F540 82121848 821897A8',
 'grass/other effect':'82172698 8217C4A0 8217ECB8',
 'debug/test object':'8210E220 821E5558',
 'empty return stub':'8252B718'}
families={'sub_'+address:family for family,addresses in groups.items() for address in addresses.split()}
methods=read('renderable-method-review.csv');assert set(families)=={r['method'] for r in methods}
for row in methods:
 fn=row['method'];add(fn,families[fn],
  'verified single blr' if fn=='sub_8252B718' else 'render-method route mapped; no claim of native independence',
  row['decompilation'],'runtime receiver population and transitive state ownership' if fn!='sub_8252B718' else '')
for row in read('renderable-constructor-review.csv'):
 fn=row['constructor']
 note=row['result'] if row['install_sites'] else 'table assignment manually inspected after conditional branch; automated prefix incomplete'
 add(fn,'renderable construction',note,'renderable-constructor-review.csv:'+fn,'creation modes, later mutations, object lifetime')
for row in read('core-callback-candidates.csv'):
 add(row['candidate_target'],'frame callback candidate',row['status'],
  'core-callback-candidates.csv:'+row['site']+'/'+row['table'],'exhaustive receiver population and callback ownership')
for row in read('native-dependency-classification.csv'):
 add(row['hook'],'native hook call-path review',row['path_class'],row['source'],row['callee_effect_contract'])
for row in read('retained-callee-contracts.csv'):
 if row['scoped_effects']:
  for fn in row['callers'].split(' | '):
   add(fn,'retained callee scoped effects',row['scoped_effects'],
    'retained-callee-contracts.csv:'+row['callee'],row['effect_status'])
for row in read('material-setter-population.csv'):
 add(row['setter'],'material state setter',row['activation_route'],
  'material-setter-population.csv:'+row['offset'],'per-value PPC equivalence; residual CPU consumers')
for row in read('static-pass-contracts.csv'):
 add(row['function'],'static pass scoped effects',row['direct_effects']+'; '+row['retained_route'],row['evidence'],row['remaining'])
for filename in ['registration-instructions.csv','renderer-state-instructions.csv']:
 for fn in sorted({r['function'] for r in read(filename)}):
  add('sub_'+fn,'focused instruction evidence','raw instruction export verified; see scoped findings in report',
   filename+':'+fn,'retain function-specific limitations; decompilation alone is not ownership proof')
for row in read('content-boundary-contracts.csv'):
 add(row['function'],'content retained boundary',row['retained_behavior'],row['source'],row['remaining'])
for row in read('renderable-content-routes.csv'):
 if row['reviewed_retained_targets']:
  add(row['method'],'direct retained content route',row['reviewed_retained_targets'],row['evidence'],row['remaining'])
for row in read('renderable-helper-routes.csv'):
 add(row['method'],'direct helper route census',
  'first native boundaries: '+row['native_boundaries']+'; unresolved frontier count: '+row['unresolved_frontiers'],
  'renderable-helper-witnesses.csv:'+row['method'],row['limitation'])
for row in read('helper-callback-review.csv'):
 add(row['function'],'helper callback local contract',row['role']+': '+row['target_expression']+'; '+row['effects'],
  row['decompilation']+'; helper-callback-review.csv:'+row['site'],row['remaining'])
save_offsets=read('state-save-offsets.csv')
for fn in ['sub_8219CE18','sub_8219C9D8']:
 add(fn,'state save offset population','10 direct save callers use four static arrays; offsets '+', '.join(r['offset'] for r in save_offsets),
  'state-save-callers.csv | state-save-offsets.csv','device callback table initialization, getter effects and runtime array mutation remain unresolved')
add('sub_821470A8','device state table initialization',
 'Installs97 getter/setter/default triplets from82552518 at device+524/+56; selected12 setters match native map.',
 'device-state-table.csv | device-state-initializer-instructions.csv','runtime device identity and later table mutation; sampler loop separately scoped')
for fn in ['sub_8219CE18','sub_8219C9D8']:
 add(fn,'initialized state dispatch targets','Twelve known save offsets mapped to initialized getter/setter pairs; getters11 read-only,one stack conversion.',
  'device-state-table.csv | device-state-getters.csv','runtime identity and later table mutation')
for filename in ['device-state-table.csv','device-sampler-table.csv']:
 for row in read(filename):
  for role in ['getter','setter']:
   add(row[role],'initialized device dispatch target',role+' at '+row['table_record']+' offset '+row['offset'],
    filename+':'+row['table_record'],row['scope'])
for row in read('device-state-getters.csv'):
 add(row['function'],'state getter local effects',row['result']+'; stores: '+row['stores'],
  'device-state-getter-instructions.csv:'+row['function'],row['remaining'])
for row in read('sampler-contracts.csv'):
 add(row['function'],'sampler local effects',row['effects'],row['decompilation']+' | sampler-instructions.csv:'+row['function'],row['remaining'])
for row in read('scalar-dispatch-review.csv'):
 add(row['function'],'scalar dispatch instruction classification',row['local_classification']+'; '+row['table_uses'],
  row['decompilation']+' | scalar-dispatch-instructions.csv:'+row['function'],row['limitation'])
for row in read('scalar-store-function-summary.csv'):
 if row['destinations']:
  add(row['function'],'scalar store address provenance',row['owner_classes']+': '+row['destinations'],
   'scalar-store-owners.csv:'+row['function'],row['remaining'])
for row in read('pass-format-contracts.csv'):
 add(row['function'],'pass format transition',row['role']+': '+row['effects'],row['decompilation']+' | pass-format-instructions.csv:'+row['site'],row['remaining'])
for row in read('untiled-boundary-contracts.csv'):
 add(row['function'],'native untiled boundary replacement',row['native_effect']+'; original called: '+row['original_called'],row['source'],row['remaining'])
for row in read('native-boundary-routes.csv'):
 add(row['function'],'native boundary route',row['route']+': '+row['detail'],row['evidence'],row['remaining'])
for row in read('macro-call-effect-reviews.csv'):
 if row['joined_callee_effects']:
  add(row['hook'],'macro callee local effects',row['joined_callee_effects'],row['joined_callee_evidence'],row['joined_callee_effect_status'])
for row in read('small-retained-contracts.csv'):
 add(row['function'],'small retained local contract',row['effects'],row['decompilation']+' | small-retained-instructions.csv:'+row['function'],row['remaining'])
quality=defaultdict(list)
for row in read('decompiler-quality.csv'):
 quality[row['function']].append(row)
rows=[]
for row in inventory:
 fn=row['function'];annotated=bool(roles[fn])
 rows.append(dict(function=fn,root_reasons=row['root_reasons'],native_boundary=row['native_boundary'],
  evidence_status='focused findings attached' if annotated else 'census only; renderer relevance not yet classified',
  roles=' | '.join(sorted(roles[fn])),findings=' | '.join(sorted(notes[fn])),
  evidence=' | '.join(sorted(sources[fn])),remaining_contracts=' | '.join(sorted(obligations[fn])) if annotated else
   'classify renderer relevance before assigning implementation work',
  implementation_status='not inferred from inventory evidence',
  decompiler_outputs=' | '.join(r['source'] for r in quality[fn]),
  decompiler_warning_categories=' | '.join(sorted({c for r in quality[fn] for c in r['categories'].split(' | ') if c})),
  decompiler_control_flow_truncation=any(r['control_flow_truncation']=='True' for r in quality[fn]),
  decompiler_review_limit='saved output warning scan only' if quality[fn] else 'no focused C output in this export'))
with (OUT/'renderer-function-findings.csv').open('w',newline='',encoding='utf-8') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=dict(function_rows=len(rows),evidence_status=dict(Counter(r['evidence_status'] for r in rows)),
 renderable_families={family:len(addresses.split()) for family,addresses in groups.items()},
 role_counts=dict(Counter(role for values in roles.values() for role in values)),
 limitation='Evidence index only; focused findings can be partial. Census-only rows are not missing renderer implementations.')
(OUT/'integrated-findings-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
