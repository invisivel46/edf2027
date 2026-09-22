"""Reproduce scalar contracts or emit a fixture of actual generated guest bodies."""
import argparse
from collections import Counter
import json
from pathlib import Path
import time

from renderer_scalar_analysis import (ROOT, MANIFEST, analyze, emit_cpp, freeze, load_manifest,
                                      sha, validate_ghidra, validate_sources)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    parser.add_argument('--emit-cpp', type=Path)
    parser.add_argument('--freeze', action='store_true')
    parser.add_argument('--ghidra', type=Path, default=ROOT/'out/renderer-automation-pilot/ghidra/facts.json')
    parser.add_argument('--output', type=Path, default=ROOT/'out/renderer-automation-pilot/contracts.json')
    parser.add_argument('--tally', type=Path, default=ROOT/'out/renderer-tally/tally.json')
    args = parser.parse_args()
    start = time.perf_counter()
    if args.freeze:
        print(freeze(output=args.manifest)['function_count'])
        return
    manifest = load_manifest(args.manifest)
    if args.emit_cpp:
        print('Extracted original guest getters:', emit_cpp(manifest, args.emit_cpp))
        return
    validate_sources(manifest)
    ghidra = validate_ghidra(manifest, args.ghidra)
    contracts = analyze(manifest, ghidra)
    tally = json.loads(args.tally.read_text())
    boundary_ids = {row['subject']: identifier for identifier, row in tally['entities'].items()
                    if row['kind'] == 'boundary' and row.get('source') == 'scalar-store-function-summary.csv'}
    for item in contracts:
        identifier = 'function:' + item['function']
        if identifier not in tally['entities'] or item['function'] not in boundary_ids:
            raise ValueError('Scalar function missing from pinned tally: ' + item['function'])
        item['tally_ids'] = [identifier, boundary_ids[item['function']]]
        item['proposed_tally_disposition'] = 'partial' if item['status'] == 'local_contract' else 'unchanged'
    payload = dict(version=1, manifest_sha256=sha(args.manifest.read_bytes()),
        analyzer_sha256=sha((ROOT/'tools/renderer_scalar_analysis.py').read_bytes()),
        ghidra_sha256=sha(args.ghidra.read_bytes()), functions=contracts,
        tally_sha256=sha(args.tally.read_bytes()), tally_snapshot_id=tally['snapshot_id'],
        tally_updates_are_proposals=True, parent_complete=False,
        tally_scope=['function:' + r['function'] for r in contracts if r['status'] == 'local_contract'] + ['task:R09.effects'],
        tally_statuses_changed=False,
        tally_boundary_ids_are_evidence_references=True,
        summary=dict(functions=len(contracts), instructions=manifest['instruction_count'],
            statuses=dict(Counter(r['status'] for r in contracts)),
            boundary_discrepancies=sum(not r['boundary_matches_inventory'] for r in contracts),
            rules=dict(Counter(r['rule'] for r in contracts if 'rule' in r)),
            accepted_partitions=dict(Counter(r['partition'] for r in contracts if 'rule' in r))),
        limitations=['Local contracts under explicit preconditions; no receiver/lifetime/mode closure.',
                     'No native getter adapter; original execution is compared with an independent contract evaluator.',
                     'Deterministic reserved partition, not a blinded model benchmark.'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2)+'\n')
    print(json.dumps(dict(**payload['summary'], elapsed_seconds=round(time.perf_counter()-start, 4)), indent=2))


if __name__ == '__main__':
    main()
