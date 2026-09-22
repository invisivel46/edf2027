"""Raw-image-verified direct helper closure, stopping at native hook boundaries.

Witnesses prove syntactic graph paths, not feasible runtime execution. Indirect
transfers and branches to non-entry addresses remain explicit frontiers.
"""
import csv
import hashlib
import json
import sqlite3
import struct
from collections import Counter, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
db = sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1', uri=True)
image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()


def read(name):
    with (OUT / name).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


entries = {addr: name for name, addr in db.execute('select name,addr from funcs')}
native = {r['function']: r['native_boundary'] for r in read('complete-function-inventory.csv') if r['native_boundary']}
methods = read('renderable-method-review.csv')
symbols = {int(r['address'],16): r['generated_symbol'] for r in read('ghidra-unresolved-direct-targets.csv') if r['generated_symbol']}
for row in read('ghidra-additional-functions.csv'):
    symbols.setdefault(int(row['address'],16), row['name'])
cache, raw_rows, edge_rows = {}, [], []


def decode(fn):
    if fn in cache:
        return cache[fn]
    body = list(db.execute('select addr,raw from instrs where func=? order by addr', (fn,)))
    sites = {addr for addr, _ in body}
    direct, frontier = [], []
    if not body:
        frontier.append(('missing body', '', fn))
    for addr, raw in body:
        raw &= 0xffffffff
        assert struct.unpack_from('>I', image, addr - 0x82000000)[0] == raw
        raw_rows.append(dict(function=fn, site=f'{addr:08X}', raw=f'{raw:08X}'))
        op = raw >> 26
        if op in (16, 18):
            bits = 16 if op == 16 else 26
            delta = raw & ((1 << bits) - 4)
            if delta & (1 << (bits - 1)):
                delta -= 1 << bits
            target = (delta if raw & 2 else addr + delta) & 0xffffffff
            # Intra-body branches are control flow, not new helper dependencies.
            if target in sites and entries.get(target) != fn:
                continue
            name = entries.get(target)
            edge_rows.append(dict(function=fn, site=f'{addr:08X}', raw=f'{raw:08X}',
                                  target=f'{target:08X}', target_function=name or '', link=bool(raw & 1)))
            if name:
                direct.append((name, f'{addr:08X}'))
            else:
                symbol = symbols.get(target, '')
                kind = ('register helper' if any(part in symbol for part in ('savegpr','restgpr','savefpr','restfpr')) else
                        'mapped external/generated symbol' if symbol else 'non-entry branch target')
                frontier.append((kind, f'{addr:08X}', f'{target:08X}' + (':' + symbol if symbol else '')))
        elif op == 19:
            xo = (raw >> 1) & 1023
            if xo == 528 or (xo == 16 and raw & 1):
                frontier.append(('indirect CTR transfer' if xo == 528 else 'linked LR transfer', f'{addr:08X}', 'unresolved'))
    cache[fn] = direct, frontier
    return cache[fn]


witnesses, summaries = [], []
for method in methods:
    root = method['method']
    pending = deque([(root, root)])
    visited, stops, unresolved = set(), set(), set()
    while pending:
        fn, path = pending.popleft()
        if fn in visited:
            continue
        visited.add(fn)
        if fn in native:
            stops.add(fn)
            witnesses.append(dict(method=root, kind='native boundary', function=fn, site='',
                                  target=fn, witness=path, source=native[fn]))
            continue
        if 'savegpr' in fn.lower() or 'restgpr' in fn.lower():
            witnesses.append(dict(method=root, kind='register helper', function=fn, site='',
                                  target=fn, witness=path, source='database function name; not expanded'))
            continue
        direct, frontier = decode(fn)
        for kind, site, target in frontier:
            if kind != 'register helper':
                unresolved.add((fn, site, kind))
            witnesses.append(dict(method=root, kind=kind, function=fn, site=site,
                                  target=target, witness=path, source='renderable-helper-instructions.csv:' + site))
        for target, site in direct:
            if target not in visited:
                pending.append((target, path + ' --' + site + '--> ' + target))
    summaries.append(dict(method=root, classes=method['classes'], visited_functions=len(visited),
                          native_boundaries=' | '.join(sorted(stops)), unresolved_frontiers=len(unresolved),
                          limitation='first native boundaries only; no indirect target expansion or branch feasibility proof'))

for filename, rows in [('renderable-helper-routes.csv', summaries), ('renderable-helper-witnesses.csv', witnesses),
                       ('renderable-helper-instructions.csv', raw_rows), ('renderable-helper-edges.csv', edge_rows)]:
    with (OUT / filename).open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
summary = dict(methods=len(summaries), expanded_function_bodies=len(cache), raw_verified_instructions=len(raw_rows),
               direct_edges=len(edge_rows), unique_native_boundaries=len({r['target'] for r in witnesses if r['kind'] == 'native boundary'}),
               witness_kinds=dict(Counter(r['kind'] for r in witnesses)),
               methods_with_frontiers=sum(bool(r['unresolved_frontiers']) for r in summaries),
               image_sha256=hashlib.sha256(image).hexdigest(),
               limitation='Syntactic direct closure stops at all recorded native hooks, including forwarding hooks. Paths can be infeasible; unresolved frontiers prevent a complete runtime closure claim.')
(OUT / 'renderable-helper-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
