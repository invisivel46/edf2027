"""Inventory warning evidence in saved decompilations, without judging semantics."""
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GH = ROOT / 'out/ghidra/renderer-inventory'
OUT = ROOT / 'out/renderer-inventory'


def category(message):
    if 'Bad instruction' in message or 'bad instruction data' in message:
        return 'bad instruction / control-flow truncation'
    if message.startswith('Removing unreachable block'):
        return 'removed unreachable block'
    if message.startswith('Inlined function:'):
        return 'inlined helper'
    if message.startswith('Restarted to delay deadcode elimination'):
        return 'decompiler restart'
    return 'other warning'


rows, warnings = [], []
for path in sorted(GH.rglob('*.c')):
    if not re.fullmatch(r'[0-9a-fA-F]{8}', path.stem):
        continue
    source = path.relative_to(ROOT).as_posix()
    counts = Counter()
    for number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        if 'WARNING:' not in line:
            continue
        message = line.split('WARNING:', 1)[1].split('*/', 1)[0].strip()
        kind = category(message)
        counts[kind] += 1
        warnings.append(dict(function='sub_' + path.stem.upper(), source=source,
                             line=number, category=kind, message=message))
    rows.append(dict(function='sub_' + path.stem.upper(), source=source,
                     analysis_set=path.parent.name if path.parent != GH else 'core',
                     sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                     warning_count=sum(counts.values()),
                     categories=' | '.join(sorted(counts)),
                     control_flow_truncation=bool(counts['bad instruction / control-flow truncation']),
                     review_limit='warning scan only; no completeness proof even when warning-free'))

for filename, records in [('decompiler-quality.csv', rows), ('decompiler-warnings.csv', warnings)]:
    with (OUT / filename).open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
summary = dict(output_files=len(rows), unique_functions=len({r['function'] for r in rows}),
               analysis_sets=dict(Counter(r['analysis_set'] for r in rows)),
               outputs_with_warnings=sum(bool(r['warning_count']) for r in rows),
               outputs_with_truncation=sum(r['control_flow_truncation'] for r in rows),
               truncated_functions=sorted({r['function'] for r in rows if r['control_flow_truncation']}),
               warning_categories=dict(Counter(r['category'] for r in warnings)),
               limitation='Saved focused C outputs only; function-definition coverage is not decompilation coverage. Warning-free output is not semantic validation.')
(OUT / 'decompiler-quality-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
