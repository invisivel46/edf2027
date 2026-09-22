"""Regenerate renderer adapters from current source and inventory retained code."""
import csv,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'; DEST=OUT/'adapters';DEST.mkdir(exist_ok=True)
cmake=(ROOT/'CMakeLists.txt').read_text()
recipes=re.findall(r'set\((\w+) \$\{CMAKE_CURRENT_BINARY_DIR\}/(native_[\w]+\.cpp)\)\s*add_custom_command\(OUTPUT.*?-P \$\{CMAKE_SOURCE_DIR\}/(tools/extract-native-[\w-]+\.cmake)',cmake,re.S)
manifest=[];functions=[];calls=[];stores=[]
for variable,output,recipe in recipes:
 if '_fixture' in variable: continue
 if (variable,output,recipe) in [(x['variable'],x['output'],x['recipe']) for x in manifest]: continue
 path=DEST/output
 run=subprocess.run(['cmake',f'-DSOURCE_DIR={ROOT.as_posix()}',f'-DOUTPUT={path.as_posix()}','-P',str(ROOT/recipe)],capture_output=True,text=True)
 (DEST/(output+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
 if run.returncode: raise RuntimeError(f'{recipe} failed: {run.stderr}')
 text=path.read_text(encoding='utf-8-sig')
 manifest.append(dict(variable=variable,output=output,recipe=recipe,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),exit_code=run.returncode))
 plain=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),text,flags=re.S)
 for match in re.finditer(r'(?:DEFINE_REX_FUNC|REX_EXTERN)\((\w+)\)\s*\{',plain):
  start=match.end();end=start;depth=1
  while end<len(plain) and depth:
   depth+=(plain[end]=='{')-(plain[end]=='}');end+=1
  assert not depth,match[1]
  body=plain[start:end-1];loc=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,match.start())+1}'
  functions.append(dict(function=match[1],source=loc,recipe=recipe,review='fresh extraction; full state ownership not yet assigned'))
  for call in re.finditer(r'\b(__imp__\w+|(?:sub_|edf_native_)\w+|REX_CALL_INDIRECT_FUNC)\s*\(',body):
   calls.append(dict(function=match[1],callee=call[1],source=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,start+call.start())+1}'))
  for store in re.finditer(r'\b(REX_STORE_\w+)\s*\(([^\n]+)',body):
   stores.append(dict(function=match[1],operation=store[1],expression=store[2].strip(),source=f'{path.relative_to(ROOT)}:{text.count(chr(10),0,start+store.start())+1}'))
def write(name,rows):
 with (OUT/name).open('w',newline='',encoding='utf-8') as stream:
  writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
write('adapter-extraction-manifest.csv',manifest);write('adapter-functions.csv',functions)
write('adapter-calls.csv',calls);write('adapter-store-sites.csv',stores)
deps=list(csv.DictReader((OUT/'native-original-dependencies.csv').open(encoding='utf-8-sig')))
needed={r['callee'][7:] for r in deps if r['callee'].startswith('__imp__edf_')}
defined={r['function'] for r in functions}
summary=dict(recipes=len(manifest),functions=len(functions),calls=len(calls),scalar_store_sites=len(stores),
 platform_import_calls=sum(r['callee'].startswith('__imp__') and not r['callee'].startswith(('__imp__sub_','__imp__edf_')) for r in calls),
 explicit_hook_adapter_symbols=len(needed),missing_explicit_hook_adapters=sorted(needed-defined),
 note='Store syntax inventory excludes inline vector/atomic writes and native helper effects; closure requires following exported call edges.')
(OUT/'adapter-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
