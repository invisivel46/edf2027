"""Inventory named guest calls and resolver sites across native graphics sources.

Includes calls through hooked sub_ entrypoints, which an __imp__-only scan misses.
This lexical census does not prove transitive reachability or expand macros.
"""
import csv,hashlib,json,re
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent; OUT=ROOT/'out/renderer-inventory'
def mask(source):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]),source)
known={(r['source'].replace('\\','/'),r['callee']) for r in csv.DictReader((OUT/'native-original-dependencies.csv').open())}
boundaries={r['function']:r['native_boundary'] for r in csv.DictReader((OUT/'complete-function-inventory.csv').open())}
rows=[];manifest=[]
for path in sorted((ROOT/'src/native_graphics').rglob('*')):
    if path.suffix not in ('.cpp','.h'):continue
    source=path.read_text(encoding='utf-8-sig'); clean=mask(source); rel=path.relative_to(ROOT).as_posix()
    manifest.append(dict(path=rel,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for match in re.finditer(r'(?<![\w])(__imp__\w+|sub_[0-9A-Fa-f]{8})\s*\(',clean):
        line=clean.count('\n',0,match.start())+1; symbol=match[1]
        # REX_EXTERN(symbol) has no '(' after symbol, and is not matched.
        rows.append(dict(source=f'{rel}:{line}',kind='original/adapter/import call' if symbol.startswith('__imp__') else 'hookable guest entry call',
            target=symbol,previous_explicit_hook_ledger=(f'{rel}:{line}',symbol) in known,
            review='call syntax observed; control path and hooked target must be classified',context=source.splitlines()[line-1].strip()))
    for match in re.finditer(r'ResolveIndirectFunction\s*\(([^)]+)\)',clean):
        line=clean.count('\n',0,match.start())+1
        rows.append(dict(source=f'{rel}:{line}',kind='indirect resolver call',target=match[1].strip(),previous_explicit_hook_ledger=False,
            review='target provenance and possible hook dispatch require review',context=source.splitlines()[line-1].strip()))
    for match in re.finditer(r'\boriginal\s*\(',clean):
        line=clean.count('\n',0,match.start())+1
        rows.append(dict(source=f'{rel}:{line}',kind='helper function parameter call',target='original',previous_explicit_hook_ledger=False,
            review='ImportTexture forwarding parameter; bound at call site',context=source.splitlines()[line-1].strip()))
def write(name,items):
    with (OUT/name).open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(items[0]));w.writeheader();w.writerows(items)
for row in rows: row['target_native_boundary']=boundaries.get(row['target'],'')
write('native-reentry-sites.csv',rows);write('native-reentry-source-manifest.csv',manifest)
summary=dict(source_files=len(manifest),sites=len(rows),kinds=dict(Counter(r['kind'] for r in rows)),
    in_previous_explicit_hook_ledger=sum(r['previous_explicit_hook_ledger'] for r in rows),
    limitation='Lexical named-entry/resolver/known-forwarder scan; macro expansions and arbitrary function-pointer aliases are not included.')
(OUT/'native-reentry-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
