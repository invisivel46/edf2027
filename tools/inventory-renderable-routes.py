"""Join verified method branches to source-reviewed retained model boundaries.

This is a direct-route census, not transitive reachability or runtime population.
"""
import csv
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
BRIDGE = ROOT / 'src/native_graphics/guest_shader_bridge.cpp'


def read(name):
    with (OUT / name).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


source = BRIDGE.read_text(encoding='utf-8')
sha = hashlib.sha256(BRIDGE.read_bytes()).hexdigest()
contracts = {
    'sub_821C9478': ('pose', 'REX_HOOK_RAW(sub_821C9478)',
                     'Timing wrapper forwards to original pose function unconditionally; inside the 821A4DE8 walk it records the output vector address for model pose publication.'),
    'sub_821B2C28': ('mesh', 'EDF_RENDER_PHASE(821B2C28, RenderMesh)',
                     'Timing wrapper forwards to original mesh function unconditionally.'),
    'sub_820D3FD0': ('overlay', 'EDF_RENDER_PHASE(820D3FD0, RenderOverlay)',
                     'Timing wrapper forwards to original overlay function unconditionally.'),
    'sub_821C9C20': ('model', 'REX_HOOK_RAW(sub_821C9C20)',
                     'Normal paths always invoke original model function: disabled/invalid-range early forwarding or temporary native pose context followed by original call. Exceptions are not modeled as successful rendering.'),
    'sub_821A1738': ('matrix upload', 'REX_HOOK_RAW(sub_821A1738)',
                     'Original upload runs first; matching model source and valid destination/count allow a 12-word-per-matrix CPU scratch overwrite.'),
    'sub_821A17D8': ('single matrix upload', 'REX_HOOK_RAW(sub_821A17D8)',
                     'Original upload runs first; matching aligned source bone and destination allow a 16-word transposed CPU scratch overwrite.'),
}
contract_rows = []
for fn, (role, marker, effect) in contracts.items():
    assert source.count(marker) == 1
    contract_rows.append(dict(function=fn, role=role, retained_behavior=effect,
                              source='src/native_graphics/guest_shader_bridge.cpp:' + str(source.count('\n', 0, source.index(marker)) + 1),
                              source_sha256=sha,
                              remaining='guest callee effects, retained inputs, resource lifetime and complete native pass execution'))

branches = defaultdict(list)
for row in read('renderable-direct-branches.csv'):
    branches[row['function']].append(row)
instructions = defaultdict(list)
for row in read('renderable-instructions.csv'):
    instructions[row['function']].append(row)
routes, edges = [], []
for method in read('renderable-method-review.csv'):
    fn = method['method']
    actual = branches[fn]
    matched = sorted({row['target'] for row in actual if row['target'] in contracts})
    indirect = []
    for row in instructions[fn]:
        raw = int(row['raw'], 16)
        # Opcode 19, XO 528: branch to CTR (conditional or unconditional).
        if raw >> 26 == 19 and (raw >> 1) & 1023 == 528:
            indirect.append(row['site'])
    for row in actual:
        if row['target'] in contracts:
            edges.append(dict(method=fn, site=row['site'], target=row['target'],
                              link=row['link'], role=contracts[row['target']][0]))
    routes.append(dict(method=fn, classes=method['classes'],
                       reviewed_retained_targets=' | '.join(matched),
                       direct_native_boundaries=' | '.join(sorted({r['target'] for r in actual if r['native_boundary']})),
                       indirect_ctr_sites=' | '.join(indirect),
                       route_status='direct retained model/pose/mesh/upload route' if matched else
                       'verified empty return' if fn == 'sub_8252B718' else 'no direct target in reviewed model boundary set; transitive route remains open',
                       evidence='renderable-direct-branches.csv:' + fn + ' | renderable-instructions.csv:' + fn,
                       remaining='runtime receiver population, transitive callbacks and ownership; direct calls do not prove which branch executes'))

for name, records in [('content-boundary-contracts.csv', contract_rows),
                      ('renderable-content-routes.csv', routes), ('renderable-content-route-sites.csv', edges)]:
    with (OUT / name).open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
summary = dict(methods=len(routes), reviewed_boundaries=len(contracts),
               methods_with_direct_retained_route=sum(bool(r['reviewed_retained_targets']) for r in routes),
               direct_retained_branch_sites=len(edges),
               methods_per_target=dict(Counter(target for r in routes for target in r['reviewed_retained_targets'].split(' | ') if target)),
               methods_with_indirect_ctr=sum(bool(r['indirect_ctr_sites']) for r in routes),
               limitation='Direct verified branches only. No-match methods can reach retained boundaries through helpers or indirect calls. Source review is pinned to bridge hash; native hook presence is not replacement.')
(OUT / 'renderable-content-route-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
