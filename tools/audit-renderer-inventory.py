"""Check inventory artifacts against current source and explicit coverage scopes.

Passing structural checks is not semantic renderer completion. Outstanding
contracts are reported separately and cannot be erased by a green census.
"""
import ast,csv,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
GH=ROOT/'out/ghidra/renderer-inventory'
def read(path,delimiter=','):
 with path.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f,delimiter=delimiter))
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
checks=[]
def check(name,actual,expected):checks.append(dict(check=name,actual=actual,expected=expected,passed=actual==expected))
summary=json.loads((OUT/'complete-summary.json').read_text())
functions=read(OUT/'complete-function-inventory.csv');names={r['function'] for r in functions}
check('complete function row count',len(functions),summary['reachable_functions'])
check('unique function names',len(names),len(functions))
check('Ghidra definition coverage',sum(r['ghidra_function_present']=='True' for r in functions),len(functions))
check('indirect site row count',len(read(OUT/'complete-indirect-sites.csv')),summary['indirect_sites'])
tables=read(OUT/'verified-vtable-pointers.csv')
check('verified table pointer row count',len(tables),summary['database_table_pointers'])
check('verified table pointer matches',sum(r['pointer_matches']=='True' for r in tables),len(tables))
dependencies=read(OUT/'native-dependency-classification.csv')
check('classified explicit hook calls',len(dependencies),summary['original_dependency_sites'])
check('macro original call rows',len(read(OUT/'macro-original-dependencies.csv')),summary['macro_original_dependency_sites'])
for manifest in ['native-source-manifest.csv','native-reentry-source-manifest.csv']:
 rows=read(OUT/manifest);bad=[]
 for row in rows:
  path=ROOT/row['path']
  if not path.is_file() or digest(path)!=row['sha256']:bad.append(row['path'])
 check(manifest+' current hashes',bad,[])
retained_manifest=(GH/'retained-functions.txt').read_text().splitlines()
check('retained manifest unique addresses',len(set(retained_manifest)),len(retained_manifest))
effect_tree=ast.parse((ROOT/'tools/inventory-small-retained.py').read_text())
effect_dict=next(node.value for node in effect_tree.body if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='effects' for t in node.targets))
effect_keys=[ast.literal_eval(key) for key in effect_dict.keys]
check('retained effects unique dictionary keys',len(set(effect_keys)),len(effect_keys))
for directory,expected in [('renderables',131),('state',45),('contracts',26),('helpers',7),('dispatch',211),('passes',6),('retained',247)]:
 rows=read(GH/directory/'decompilation.tsv','\t')
 check(directory+' successful decompilations',sum(r['status']=='ok' for r in rows),expected)
 check(directory+' body files present',sum((GH/directory/(r['address']+'.c')).is_file() for r in rows),len(rows))
methods=read(OUT/'renderable-method-review.csv')
check('renderable method rows',len(methods),47)
check('renderable methods in broad census',sorted(r['method'] for r in methods if r['method'] not in names),[])
constructors=read(OUT/'renderable-constructor-call-arguments.csv')
check('same-object constructor edges',sum(r['same_entry_object']=='True' for r in constructors),83)
material=read(OUT/'material-setter-population.csv')
check('material setter population',len(material),36)
check('material ordinary callback population',[r['setter'] for r in material if r['offset']=='0xc8'],['sub_82137978'])
findings=read(OUT/'renderer-function-findings.csv')
indexed={r['function']:r for r in findings}
check('findings index row count',len(findings),len(functions))
check('findings index census coverage',sorted(set(indexed).symmetric_difference(names)),[])
focused={r['method'] for r in methods}
focused.update(r['constructor'] for r in read(OUT/'renderable-constructor-review.csv'))
focused.update(r['candidate_target'] for r in read(OUT/'core-callback-candidates.csv'))
focused.update(r['setter'] for r in material)
focused.update(r['hook'] for r in dependencies)
for filename in ['registration-instructions.csv','renderer-state-instructions.csv']:
 focused.update('sub_'+r['function'] for r in read(OUT/filename))
check('focused findings attached',sorted(fn for fn in focused if fn not in indexed or indexed[fn]['evidence_status']!='focused findings attached'),[])
reentry={r['target'].removeprefix('__imp__') for r in read(OUT/'native-reentry-sites.csv')}
check('named native reentry targets in census',sorted(fn for fn in reentry if fn.startswith('sub_') and fn not in names),[])
contracts=read(OUT/'retained-callee-contracts.csv')
check('retained callee grouping covers every symbol',sorted({r['callee'] for r in contracts}.symmetric_difference({r['callee'] for r in dependencies})),[])
check('retained callee grouping covers every call site',sorted(site for r in contracts for site in r['source_sites'].split(' | ')),sorted(r['source'] for r in dependencies))
callback_reviews=read(OUT/'retained-callback-review.csv')
callback_sites={r['source'].replace('\\','/') for r in callback_reviews}
terminal_callbacks={r['source'].replace('\\','/') for r in read(OUT/'retained-adapter-terminal-edges.csv') if r['route']=='indirect unresolved'}
check('retained callback provenance coverage',sorted(callback_sites.symmetric_difference(terminal_callbacks)),[])
raw_contracts={r['site']:r['raw'] for r in read(OUT/'contract-callback-instructions.csv')}
producer_contracts=read(OUT/'retained-callback-producers.csv')
check('retained callback producer evidence',sorted(r['site'] for r in producer_contracts if raw_contracts.get(r['site'])!=r['raw']),[])
adapter_calls=read(OUT/'adapter-calls.csv')
import_syntax=set()
adapter_hash_errors=[]
for entry in read(OUT/'adapter-extraction-manifest.csv'):
 path=OUT/'adapters'/entry['output']
 if digest(path)!=entry['sha256']:adapter_hash_errors.append(entry['output'])
 body=path.read_text(encoding='utf-8-sig')
 plain=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),body,flags=re.S)
 for match in re.finditer(r'\b(__imp__\w+)\s*\(',plain):
  import_syntax.add((match[1],str(path.relative_to(ROOT)).replace('\\','/')+':'+str(body.count('\n',0,match.start())+1)))
import_rows={(r['callee'],r['source'].replace('\\','/')) for r in adapter_calls if r['callee'].startswith('__imp__')}
check('fresh adapter extraction hashes',adapter_hash_errors,[])
check('adapter named import syntax coverage',sorted(import_syntax.symmetric_difference(import_rows)),[])
check('static pass contracts joined',sorted(r['function'] for r in read(OUT/'static-pass-contracts.csv') if 'static pass scoped effects' not in indexed.get(r['function'],{}).get('roles','')),[])
content_contracts=read(OUT/'content-boundary-contracts.csv')
check('content contract source hashes',[r['function'] for r in content_contracts if digest(ROOT/r['source'].rsplit(':',1)[0])!=r['source_sha256']],[])
content_routes=read(OUT/'renderable-content-routes.csv')
check('content route method coverage',sorted({r['method'] for r in content_routes}.symmetric_difference({r['method'] for r in methods})),[])
contract_targets={r['function'] for r in content_contracts}
method_names={r['method'] for r in methods}
expected_edges={(r['function'],r['site'],r['target'],r['link']) for r in read(OUT/'renderable-direct-branches.csv') if r['function'] in method_names and r['target'] in contract_targets}
actual_edges={(r['method'],r['site'],r['target'],r['link']) for r in read(OUT/'renderable-content-route-sites.csv')}
check('content direct route instruction coverage',sorted(expected_edges.symmetric_difference(actual_edges)),[])
check('content boundary contracts joined',[r['function'] for r in content_contracts if 'content retained boundary' not in indexed[r['function']]['roles']],[])
helper_routes=read(OUT/'renderable-helper-routes.csv')
check('helper route method coverage',sorted({r['method'] for r in helper_routes}.symmetric_difference(method_names)),[])
helper_raw={r['site']:r['raw'] for r in read(OUT/'renderable-helper-instructions.csv')}
check('helper branch instruction evidence',[r['site'] for r in read(OUT/'renderable-helper-edges.csv') if helper_raw.get(r['site'])!=r['raw']],[])
check('helper routes joined',[r['method'] for r in helper_routes if 'direct helper route census' not in indexed[r['method']]['roles']],[])
helper_callbacks=read(OUT/'helper-callback-review.csv')
helper_ctr={r['site'] for r in read(OUT/'renderable-helper-witnesses.csv') if r['kind']=='indirect CTR transfer'}
check('helper callback review site coverage',sorted(helper_ctr.symmetric_difference({r['site'] for r in helper_callbacks})),[])
check('helper callback decompilation hashes',[r['site'] for r in helper_callbacks if digest(ROOT/r['decompilation'])!=r['decompilation_sha256']],[])
check('helper callback reviews joined',[r['function'] for r in helper_callbacks if 'helper callback local contract' not in indexed[r['function']]['roles']],[])
save_calls=read(OUT/'state-save-callers.csv')
save_words={r['site'] for r in read(OUT/'state-save-argument-instructions.csv')}
check('state save caller argument locators',[r['site'] for r in save_calls if not {r['site'],*r['argument_sites'].split(' | ')}<=save_words],[])
check('state save offset summaries joined',[fn for fn in ['sub_8219CE18','sub_8219C9D8'] if 'state save offset population' not in indexed[fn]['roles']],[])
dispatch_targets={r[role] for filename in ['device-state-table.csv','device-sampler-table.csv'] for r in read(OUT/filename) for role in ['getter','setter']}
check('initialized device dispatch targets in census',sorted(dispatch_targets-names),[])
check('initialized device dispatch targets joined',sorted(fn for fn in dispatch_targets if 'initialized device dispatch target' not in indexed.get(fn,{}).get('roles','')),[])
sampler_contracts=read(OUT/'sampler-contracts.csv')
sampler_targets={r[role] for r in read(OUT/'device-sampler-table.csv') for role in ['getter','setter']}
check('sampler contract table coverage',sorted(sampler_targets.symmetric_difference({r['function'] for r in sampler_contracts})),[])
check('sampler contract output hashes',[r['function'] for r in sampler_contracts if digest(ROOT/r['decompilation'])!=r['sha256']],[])
check('sampler local contracts joined',[r['function'] for r in sampler_contracts if 'sampler local effects' not in indexed[r['function']]['roles']],[])
scalar_review=read(OUT/'scalar-dispatch-review.csv')
scalar_targets={r[role] for r in read(OUT/'device-state-table.csv') for role in ['getter','setter']}
check('scalar local census target coverage',sorted(scalar_targets.symmetric_difference({r['function'] for r in scalar_review})),[])
check('scalar local census output hashes',[r['function'] for r in scalar_review if digest(ROOT/r['decompilation'])!=r['sha256']],[])
check('scalar local census joined',[r['function'] for r in scalar_review if 'scalar dispatch instruction classification' not in indexed[r['function']]['roles']],[])
store_owners=read(OUT/'scalar-store-owners.csv')
check('scalar store provenance exact coverage',sorted((r['function'],r['site'],r['raw']) for r in store_owners),sorted((r['function'],r['site'],r['raw']) for r in read(OUT/'scalar-dispatch-stores.csv')))
check('scalar store provenance joined',sorted({r['function'] for r in store_owners if 'scalar store address provenance' not in indexed[r['function']]['roles']}),[])
pass_formats=read(OUT/'pass-format-contracts.csv')
check('pass format output hashes',[r['function'] for r in pass_formats if digest(ROOT/r['decompilation'])!=r['sha256']],[])
check('pass format contracts joined',[r['function'] for r in pass_formats if 'pass format transition' not in indexed[r['function']]['roles']],[])
untiled=read(OUT/'untiled-boundary-contracts.csv')
check('untiled boundary source hashes',[r['function'] for r in untiled if digest(ROOT/r['source'].rsplit(':',1)[0])!=r['sha256']],[])
check('untiled boundary contracts joined',[r['function'] for r in untiled if 'native untiled boundary replacement' not in indexed[r['function']]['roles']],[])
boundary_routes=read(OUT/'native-boundary-routes.csv')
check('native boundary route coverage',sorted({r['function'] for r in boundary_routes}.symmetric_difference({r['function'] for r in functions if r['native_boundary']})),[])
check('native boundary route source hashes',[r['function'] for r in boundary_routes if digest(ROOT/'src/native_graphics/guest_shader_bridge.cpp')!=r['bridge_sha256']],[])
check('native boundary routes joined',[r['function'] for r in boundary_routes if 'native boundary route' not in indexed[r['function']]['roles']],[])
call_effects=read(OUT/'retained-call-effect-reviews.csv')
macro_effects=read(OUT/'macro-call-effect-reviews.csv')
for label,joined,original in [('explicit',call_effects,dependencies),('macro',macro_effects,read(OUT/'macro-original-dependencies.csv'))]:
 check(label+' effect join exact call sites',sorted((r['hook'],r['callee'],r['source']) for r in joined),sorted((r['hook'],r['callee'],r['source']) for r in original))
check('partial effect reviews have evidence',[r['source'] for r in call_effects+macro_effects if r['joined_callee_effects'] and not r['joined_callee_evidence']],[])
small_retained=read(OUT/'small-retained-contracts.csv')
check('small retained output hashes',[r['function'] for r in small_retained if digest(ROOT/r['decompilation'])!=r['sha256']],[])
check('small retained contracts joined',[r['function'] for r in small_retained if 'small retained local contract' not in indexed[r['function']]['roles']],[])
quality=read(OUT/'decompiler-quality.csv')
saved_outputs={p.relative_to(ROOT).as_posix() for p in GH.rglob('*.c') if re.fullmatch(r'[0-9a-fA-F]{8}',p.stem)}
check('decompiler warning scan output coverage',sorted(saved_outputs.symmetric_difference({r['source'] for r in quality})),[])
check('decompiler warning scan current hashes',[r['source'] for r in quality if digest(ROOT/r['source'])!=r['sha256']],[])
truncated={r['function'] for r in quality if r['control_flow_truncation']=='True'}
check('decompiler truncation flags joined',sorted(fn for fn,row in indexed.items() if (row['decompiler_control_flow_truncation']=='True')!=(fn in truncated)),[])
result=dict(structural_checks=checks,structural_pass=all(r['passed'] for r in checks),
    semantic_completion_proven=False,
    outstanding_contracts=dict(explicit_hook_calls_with_pending_callee_effects=sum(r['callee_effect_contract'].startswith('pending') for r in dependencies),
       explicit_calls_with_partial_local_effects=sum(bool(r['joined_callee_effects']) for r in call_effects),
       macro_calls_with_partial_local_effects=sum(bool(r['joined_callee_effects']) for r in macro_effects),
       census_only_function_rows=sum(r['evidence_status'].startswith('census only') for r in findings)),
    functions_with_focused_findings=sum(r['evidence_status']=='focused findings attached' for r in findings),
    limitations=['Joined findings are scoped evidence, not proof of native implementation completeness; census-only functions are not a missing-implementation count.',
     'Runtime callback populations, external callbacks, and per-family ownership remain unclosed.',
     'Renderer implementation/runtime acceptance gates are future migration work, not requirements to implement during this inventory task.'])
(OUT/'inventory-completion-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
