"""One-command deterministic scalar analysis, execution, evidence and partial acceptance."""
import argparse
from collections import Counter, defaultdict
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

from renderer_dispatch import ROOT, digest, file_hash, local, read_json, require, write_json, locked
from renderer_scalar_analysis import load_manifest, validate_sources
from renderer_scalar_acceptance import verify_policy

MANIFEST = 'tests/fixtures/renderer-scalar-pilot.json'
BOUNDARIES = 'tests/fixtures/renderer-scalar-boundaries.json'
POLICY = 'docs/renderer-scalar-policy.json'
LANE = 'out/renderer-scalar-pipeline'


def stable_write(path, value):
    path = Path(path)
    if path.exists() and read_json(path) == value:
        return
    write_json(path, value)


def cached(cache, key, compute):
    path = cache / (key + '.json')
    if path.exists():
        item = read_json(path)
        require(item.get('key') == key and digest(item['value']) == item.get('digest'),
                'Corrupt scalar cache: ' + str(path))
        return item['value'], True
    value = compute()
    stable_write(path, dict(key=key, value=value, digest=digest(value)))
    return value, False


def run(args, cwd=ROOT, log=None):
    result = subprocess.run([str(a) for a in args], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log:
        Path(log).write_text(result.stdout)
    require(result.returncode == 0, 'Command failed: ' + ' '.join(map(str,args)) + '\n' + result.stdout[-5000:])
    return result.stdout


def runtime_dependencies(build):
    """Ninja's actual header dependencies plus the exact compile command/toolchain.

    Generated fixture changes are represented per function by body/contract keys.
    Other compile/header changes conservatively invalidate every execution case.
    """
    cache = (build/'CMakeCache.txt').read_text()
    ninja_line = next(s for s in cache.splitlines() if s.startswith('CMAKE_MAKE_PROGRAM:'))
    ninja = ninja_line.split('=',1)[1]
    obj = 'CMakeFiles/edf_renderer_scalar_pipeline_tests.dir/tests/renderer_scalar_pipeline_tests.cpp.obj'
    listing = run([ninja,'-C',build,'-t','deps',obj])
    headers = {}
    for line in listing.splitlines():
        if not line.startswith('    '): continue
        path = Path(line.strip())
        if not path.is_absolute(): path = build/path
        path = path.resolve()
        if path.name == 'renderer_scalar_effect_fixture.inc': continue
        if path.is_file(): headers[str(path)] = file_hash(path)
    require(headers, 'No compiler header dependencies found')
    command = run([ninja,'-C',build,'-t','commands',obj])
    compiler_value = next(s for s in cache.splitlines() if s.startswith('CMAKE_CXX_COMPILER:')).split('=',1)[1]
    compiler = compiler_value if Path(compiler_value).is_file() else shutil.which(compiler_value)
    require(compiler, 'Compiler environment unavailable; use run-renderer-scalars.cmd')
    return dict(headers=headers, compile_command=command, compiler=str(Path(compiler).resolve()),
                compiler_sha256=file_hash(compiler))


def exceptions(rows):
    groups = defaultdict(list)
    for r in rows:
        if r['status'] != 'local_contract':
            reason=r.get('reason','unsupported')
            match=re.match(r'([0-9A-F]+): opcode ([0-9A-F]{8}) (.*)',reason)
            if match:
                word=int(match[2],16)
                reason=f'{match[3]} (primary opcode {word >> 26})'
            groups[reason].append(r['function'])
    return [dict(reason=reason, functions=names, count=len(names), task='R09.effects',
                 action='Add and independently review the missing instruction/effect semantics, or bound the control-flow/receiver dependency.',
                 completion_test='Reprocess all members; exact-boundary contracts must pass three-way state, memory and ordered-write validation. Otherwise retain a specific rejection.')
            for reason,names in sorted(groups.items(),key=lambda kv:(-len(kv[1]),kv[0]))]


def execute(args):
    require(os.name != 'nt' or os.environ.get('VSCMD_VER'),
            'Compiler environment unavailable; use run-renderer-scalars.cmd')
    import renderer_scalar_effects as engine
    started=time.perf_counter()
    lane=ROOT/LANE
    lane.mkdir(parents=True,exist_ok=True)
    policy=None if args.no_accept else verify_policy(ROOT,POLICY)
    manifest=load_manifest(ROOT/MANIFEST)
    validate_sources(manifest)
    boundaries=read_json(ROOT/BOUNDARIES)
    require(len(manifest['functions'])==170 and set(boundaries['functions'])==
            {r['function'] for r in manifest['functions']},'Incomplete scalar input census')
    model_hash=file_hash(ROOT/'tools/renderer_scalar_effects.py')
    rows=[]; analysis_hits=0
    for record in manifest['functions']:
        boundary=boundaries['functions'][record['function']]
        key=digest(dict(record=record,boundary=boundary,engine=model_hash,
                        helper=file_hash(ROOT/'tools/renderer_scalar_analysis.py')))
        row,hit=cached(lane/'cache/analysis',key,lambda:engine.analyze_record(record,boundary))
        require(row['function']==record['function'] and row['status'] in ('local_contract','unsupported'),
                'Invalid scalar analyzer result')
        rows.append(row);analysis_hits+=hit
    analysis=dict(version=1,manifest_sha256=file_hash(ROOT/MANIFEST),
        boundaries_sha256=file_hash(ROOT/BOUNDARIES),engine_sha256=model_hash,
        functions=rows,counts=dict(Counter(r['status'] for r in rows)),
        exceptions=exceptions(rows),parent_complete=False)
    stable_write(lane/'analysis.json',analysis)
    from renderer_scalar_planner import write_plan
    write_plan(manifest,boundaries,lane/'planner.json')
    # CMake generates the same deterministic include from these reviewed inputs.
    build=(ROOT/args.build_dir).resolve()
    require((build/'CMakeCache.txt').is_file(), 'Configure the project SDK/build before running the scalar pipeline')
    configured=(build/'CMakeCache.txt').read_text()
    require('rexglue_DIR:PATH=' in configured or 'CMAKE_PREFIX_PATH:' in configured,
            'Configured SDK is missing; restore the normal project preset before running this command')
    run(['cmake','-S',ROOT,'-B',build],log=lane/'configure.log')
    run(['cmake','--build',build,'--target','edf_renderer_scalar_pipeline_tests','--parallel','2'],log=lane/'build.log')
    runtime=runtime_dependencies(build)
    executable=build/'edf_renderer_scalar_pipeline_tests.exe'
    runtime_id=digest(runtime)
    accepted=[r for r in rows if r['status']=='local_contract']
    records={r['function']:r for r in manifest['functions']}
    wanted=[];tests={};execution_keys={}
    for row in accepted:
        name=row['function']
        key=digest(dict(contract=row,body=records[name]['body_sha256'],runtime=runtime_id,
                        engine=model_hash,
                        oracle=file_hash(ROOT/'tests/renderer_scalar_pipeline_tests.cpp')))
        execution_keys[name]=key
        path=lane/'cache/execution'/(key+'.json')
        if path.exists() and not args.force_tests:
            value,hit=cached(lane/'cache/execution',key,lambda:None)
            tests[name]=value
        else:wanted.append(name)
    if wanted:
        (lane/'selected.txt').write_text('\n'.join(wanted)+'\n')
        run([executable,'--only',lane/'selected.txt','--report',lane/'executed.json'],log=lane/'execution.log')
        executed=read_json(lane/'executed.json')
        require(executed['passed'] is True,'Scalar harness failed')
        got={r['function']:r for r in executed['functions']}
        require(set(got)==set(wanted) and len(got)==len(executed['functions']),'Harness selected set mismatch')
        for row in accepted:
            name=row['function']
            if name not in got:continue
            result=got[name]
            require(result['contract_sha256']==digest(row) and result['body_sha']==records[name]['body_sha256'],
                    'Compiled fixture differs from analyzed contract or original body')
            require(result['passed'] is True and result['invocations']>0 and result['negative_controls']>0,
                    'Insufficient scalar harness evidence')
            tests[name]=result
            key=execution_keys[name]
            stable_write(lane/'cache/execution'/(key+'.json'),dict(key=key,value=result,digest=digest(result)))
    require(all(tests[n]['contract_sha256']==digest(row) for row in accepted for n in [row['function']]),
            'Cached contract validation mismatch')
    validation=dict(version=1,passed=True,functions=[tests[r['function']] for r in accepted],
                    oracle_sha256=file_hash(ROOT/'tests/renderer_scalar_pipeline_tests.cpp'),
                    pipeline_sha256=file_hash(Path(__file__)),
                    runtime=runtime,execution_keys=execution_keys)
    stable_write(lane/'validation.json',validation)
    # Evidence identity excludes timings/cache-hit counts, allowing exact reruns.
    evidence_id=digest(dict(analysis=analysis,validation=validation,
                            policy=file_hash(ROOT/POLICY) if policy else None))
    bundle_dir=lane/'bundles'/evidence_id
    bundle_dir.mkdir(parents=True,exist_ok=True)
    for name,value in [('analysis.json',analysis),('validation.json',validation)]:
        stable_write(bundle_dir/name,value)
    rel=lambda p:p.relative_to(ROOT).as_posix()
    if policy:
        # Detect any edits made while build/execution was running before accepting.
        verify_policy(ROOT,POLICY)
        validate_sources(manifest)
        require(runtime_dependencies(build)==runtime,'Runtime dependencies changed during execution')
        artifact_names=[rel(bundle_dir/'analysis.json'),rel(bundle_dir/'validation.json'),MANIFEST]
        bundle=dict(version=1,id=evidence_id,disposition='partial',parent_complete=False,
            policy=POLICY,policy_sha256=file_hash(ROOT/POLICY),
            analysis=artifact_names[0],validation=artifact_names[1],manifest=MANIFEST,
            functions=[r['function'] for r in accepted],
            artifacts={p:file_hash(ROOT/p) for p in artifact_names})
        stable_write(bundle_dir/'acceptance.json',bundle)
        config_path=ROOT/'docs/renderer-tally-reconciliation.json'
        config=read_json(config_path)
        config['accepted_scalar_pipeline']=[rel(bundle_dir/'acceptance.json')]
        # Compute and validate the full prospective tally BEFORE publishing config.
        from renderer_tally import build as tally_build
        tally=tally_build(ROOT,config)
        stable_write(config_path,config)
        stable_write(ROOT/'out/renderer-tally/tally.json',tally)
    report=dict(version=1,evidence_id=evidence_id,accepted=policy is not None,
        functions=170,counts=analysis['counts'],analysis_cache_hits=analysis_hits,
        execution_cache_hits=len(accepted)-len(wanted),executed=len(wanted),
        seconds=round(time.perf_counter()-started,4),game_booted=False,
        evidence_path=rel(bundle_dir))
    stable_write(lane/'latest-run.json',report)
    print(json.dumps(report,indent=2))
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',default='out/build/win-amd64-release')
    p.add_argument('--no-accept',action='store_true',help='Development run: never changes tally')
    p.add_argument('--force-tests',action='store_true',help='Reexecute all eligible cases')
    args=p.parse_args()
    (ROOT/LANE).mkdir(parents=True,exist_ok=True)
    with locked(ROOT/LANE/'pipeline'):
        execute(args)


if __name__=='__main__':main()
