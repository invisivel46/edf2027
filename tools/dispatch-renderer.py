"""Manage renderer slices. Only the coordinator runs mutating subcommands."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from renderer_dispatch import (ROOT, advance, artifact, assign, digest, file_hash, git, initialize,
                               integrate, local, locked, now, packet, read_json, readiness,
                               require, review, source_manifest, submit, unchanged_inputs,
                               validate_result, write_json)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ledger', type=Path, default=ROOT/'out/renderer-dispatch/ledger.json')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('init'); p.add_argument('--spec', type=Path, default=ROOT/'docs/renderer-dispatch-first-wave.json')
    sub.add_parser('status')
    p = sub.add_parser('fingerprint'); p.add_argument('--workspace', type=Path, default=ROOT)
    p = sub.add_parser('prepare'); p.add_argument('workspace', type=Path)
    p = sub.add_parser('advance'); p.add_argument('--reason', required=True)
    for name in ('ready', 'packet', 'result-template', 'assign', 'validate-result', 'submit', 'review', 'integrate', 'block'):
        p = sub.add_parser(name); p.add_argument('slice')
        if name in ('packet', 'result-template'): p.add_argument('--output', type=Path)
        if name == 'assign':
            p.add_argument('--owner', required=True); p.add_argument('--workspace', type=Path, required=True)
        if name in ('validate-result', 'submit'): p.add_argument('--result', type=Path, required=True)
        if name == 'review':
            p.add_argument('--reviewer', required=True); p.add_argument('--record', required=True)
        if name == 'integrate': p.add_argument('--offline-report')
        if name == 'block': p.add_argument('--reason', required=True)
    args = parser.parse_args()
    try:
        if args.command == 'fingerprint':
            print(digest(source_manifest(args.workspace))); return 0
        with locked(args.ledger):
            if args.command in ('init', 'ready', 'advance'):
                audit_path = args.ledger.resolve().parent / ('coverage-audit-' + uuid.uuid4().hex + '.json')
                before = digest(source_manifest(ROOT))
                subprocess.run([sys.executable, str(ROOT/'tools/audit-renderer-coverage.py'), '--report', str(audit_path)],
                               cwd=ROOT, check=True)
                require(read_json(audit_path).get('passed') is True, 'Coverage audit did not pass')
                require(digest(source_manifest(ROOT)) == before, 'Source changed during coverage audit')
            if args.command == 'init':
                require(not args.ledger.exists(), 'Ledger already exists; use a new --ledger for a new baseline')
                spec = read_json(args.spec)
                require('tally' in spec, 'New CLI campaigns require a top-level tally snapshot reference')
                ledger = initialize(ROOT, spec)
            else:
                ledger = read_json(args.ledger)
                require(Path(ledger['root']).resolve() == ROOT, 'Run dispatcher from its coordinator checkout')
            if args.command == 'status':
                print(json.dumps({key: dict(status=v['status'], model=v['spec']['model'],
                      owner=v['attempts'][-1]['owner'] if v['attempts'] else None)
                      for key, v in ledger['slices'].items()}, indent=2)); return 0
            if args.command == 'result-template':
                item = ledger['slices'][args.slice]
                require(item['status'] == 'assigned', 'Result template requires an assigned slice')
                attempt = item['attempts'][-1]
                current = source_manifest(attempt['workspace'])
                baseline = ledger['baseline']['source']
                result = dict(slice_id=args.slice, attempt_id=attempt['id'], owner=attempt['owner'],
                              packet_id=attempt['packet_id'], baseline_id=ledger['baseline']['id'],
                              source_id=digest(current), disposition='incomplete', parent_complete=False,
                              changed_files=sorted(p for p in set(current) | set(baseline) if current.get(p) != baseline.get(p)),
                              epistemic_session='', evidence_ids=[], artifacts=[],
                              cases={c['id']: dict(status='not_run', details='', artifact='') for c in item['spec']['cases']},
                              checks={name: dict(command=cmd, exit_code=None, log=dict(path='', sha256=''))
                                      for name, cmd in item['spec']['checks'].items()})
                if item['spec']['kind'] == 'implementation': result['offline_report'] = dict(path='', sha256='')
                if 'tally' in ledger:
                    result['tally_updates'] = [dict(id=identifier, disposition='unchanged', evidence_ids=[], details='')
                                               for identifier in item['spec']['tally_ids']]
                    result['discoveries'] = []
                if args.output:
                    require(not args.output.exists(), 'Result file already exists')
                    write_json(args.output, result); print(args.output.resolve())
                else: print(json.dumps(result, indent=2))
                return 0
            if args.command == 'packet':
                rendered = packet(ledger, args.slice)
                if args.output:
                    require(not args.output.exists(), 'Packet output already exists; choose a new filename')
                    args.output.parent.mkdir(parents=True, exist_ok=True)
                    args.output.write_text(rendered, encoding='utf-8')
                    print(args.output.resolve())
                else: print(rendered)
                return 0
            if args.command == 'prepare':
                # Detached local clone; preserve root HEAD/index and overlay pinned dirty/untracked inputs.
                destination = args.workspace.resolve()
                require(destination.is_relative_to(ROOT/'out/renderer-dispatch/workspaces'),
                        'Workspace must be under out/renderer-dispatch/workspaces')
                require(not destination.exists(), 'Workspace destination already exists')
                require(source_manifest(ROOT) == ledger['baseline']['source'], 'Root baseline changed; create a new ledger')
                unchanged_inputs(ledger, ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                subprocess.run(['git', 'clone', '--shared', '--no-checkout', str(ROOT), str(destination)], check=True)
                subprocess.run(['git', '-C', str(destination), 'checkout', '--detach', ledger['baseline']['head']], check=True)
                for name, expected in {**ledger['baseline']['source'], **ledger['baseline']['inputs']}.items():
                    target = local(destination, name)
                    if expected is None:
                        if target.is_file(): target.unlink()
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(local(ROOT, name), target)
                        require(file_hash(target) == expected, 'Input changed while copying: ' + name)
                require(source_manifest(destination) == ledger['baseline']['source'], 'Prepared source mismatch')
                unchanged_inputs(ledger, destination)
                print('Prepared isolated checkout: ' + str(destination)); return 0
            if args.command == 'ready': readiness(ledger, args.slice)
            if args.command == 'advance': advance(ledger, args.reason)
            if args.command == 'assign': print(json.dumps(assign(ledger, args.slice, args.owner, args.workspace), indent=2))
            if args.command in ('validate-result', 'submit'):
                result = read_json(args.result)
                if args.command == 'validate-result':
                    validate_result(ledger, args.slice, result); print('Result structurally valid; review still required'); return 0
                submit(ledger, args.slice, result)
            if args.command == 'review':
                workspace = Path(ledger['slices'][args.slice]['attempts'][-1]['workspace'])
                review(ledger, args.slice, args.reviewer, dict(path=args.record, sha256=file_hash(local(workspace, args.record))))
            if args.command == 'integrate':
                ref = None if not args.offline_report else dict(path=args.offline_report, sha256=file_hash(local(ROOT, args.offline_report)))
                integrate(ledger, args.slice, ref)
            if args.command == 'block':
                item = ledger['slices'][args.slice]
                require(item['status'] != 'integrated', 'Integrated slice cannot be blocked retroactively')
                require(args.reason.strip(), 'Block needs a concrete reason')
                item['status'] = 'blocked'; item['blocked_reason'] = args.reason
            ledger['revision'] += 1
            if args.command in ('init', 'ready', 'advance'):
                ledger['coverage_audit'] = dict(path=str(audit_path), sha256=file_hash(audit_path), source_id=before)
            ledger['events'].append(dict(at=now(), actor=ledger['coordinator'], command=args.command,
                                          slice=getattr(args, 'slice', None)))
            write_json(args.ledger, ledger)
            print(f'{args.command}: saved revision {ledger["revision"]} to {args.ledger}')
        return 0
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        print('ERROR: ' + str(error), file=sys.stderr); return 1


if __name__ == '__main__':
    sys.exit(main())
