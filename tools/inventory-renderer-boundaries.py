"""Join emitted renderer reachability with native hook evidence; no completeness claim."""
import csv
import json
import re
from collections import defaultdict, deque
from pathlib import Path

root = Path(__file__).resolve().parent.parent
out = root / 'out/renderer-inventory'
edges = list(csv.DictReader((out / 'generated-call-edges.csv').open(encoding='utf-8-sig')))
roots = ['821A5080', '821A3BA0', '820B4310', '820B4250', '821C3BB8', '821D96D8',
         '821BE8D0', '821BE9D8', '820B0B80', '820D3FD0', '821C9478', '821C9C20',
         '821D9600', '821B94E8', '821CDDF8', '821A4DE8']
graph = defaultdict(list)
for edge in edges:
    graph[edge['Caller']].append(edge)
hooks = defaultdict(list)
for path in (root / 'src').rglob('*'):
    if path.suffix not in ('.cpp', '.h'):
        continue
    text = path.read_text(encoding='utf-8-sig')
    # A declaration is evidence of a hook, not of native completion.
    for match in re.finditer(r'REX_HOOK\w*\(\s*(sub_[0-9A-Fa-f]+)', text):
        hooks[match[1].upper()].append(f'{path.relative_to(root)}:{text.count(chr(10), 0, match.start())+1}')
    for match in re.finditer(r'EDF_RENDER_PHASE\(([0-9A-Fa-f]{8}),', text):
        hooks[('sub_'+match[1]).upper()].append(f'{path.relative_to(root)}:{text.count(chr(10), 0, match.start())+1} [original forwarding macro]')
reachable = set()
for seed in roots:
    todo = deque(['sub_'+seed])
    while todo:
        function = todo.popleft()
        if function in reachable:
            continue
        reachable.add(function)
        todo.extend(edge['Callee'] for edge in graph[function] if edge['Callee'].startswith('sub_'))
rows = []
for function in sorted(reachable):
    calls = graph[function]
    rows.append(dict(function=function, hook=' | '.join(hooks.get(function.upper(), [])),
                     direct_sites=sum(bool(x['Callee']) for x in calls),
                     indirect_sites=sum(bool(x['IndirectTarget']) for x in calls),
                     generated_file=calls[0]['File'] if calls else ''))
with (out/'renderer-boundaries.csv').open('w', newline='', encoding='utf-8') as stream:
    writer=csv.DictWriter(stream, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
with (out/'renderer-indirect-sites.csv').open('w', newline='', encoding='utf-8') as stream:
    writer=csv.DictWriter(stream, fieldnames=edges[0].keys()); writer.writeheader()
    writer.writerows(edge for edge in edges if edge['Caller'] in reachable and edge['IndirectTarget'])
summary=dict(roots=roots, reachable_functions=len(reachable), with_explicit_hook=sum(bool(x['hook']) for x in rows),
             without_explicit_hook=sum(not x['hook'] for x in rows),
             reachable_indirect_sites=sum(x['indirect_sites'] for x in rows),
             caveat='Direct reachability includes common utilities. Hooks can retain originals. Indirect destinations are not included unless separately seeded.')
(out/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
addresses=sorted(set(re.findall(r'\{\s*0x([0-9a-fA-F]{8}),\s*sub_', (root/'generated/default/edf2017_init.cpp').read_text())))
ghidra=root/'out/ghidra/renderer-inventory'
ghidra.mkdir(parents=True, exist_ok=True)
(ghidra/'generated-function-addresses.txt').write_text('\n'.join(x.lower() for x in addresses)+'\n')
