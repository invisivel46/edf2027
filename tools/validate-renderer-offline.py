"""Fresh, fail-closed offline renderer gate. No game executable or game data."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from renderer_dispatch import digest, source_manifest
from renderer_offline_suites import CPU, RENDER

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'out/build/win-amd64-release')
    parser.add_argument('--suite', choices=['all', 'cpu', 'render'], default='all')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    build = args.build_dir.resolve()
    selected = (CPU if args.suite != 'render' else []) + (RENDER if args.suite != 'cpu' else [])
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = ROOT / 'out/renderer-offline' / stamp
    output.mkdir(parents=True)
    report = dict(passed=False, suite=args.suite, selected=selected, build_dir=str(build),
                  commands=[], game_booted=False, scope='Synthetic contracts and offscreen WARP; not gameplay parity')
    started = time.monotonic()

    def run(command, name, quiet=False):
        print(f'[{name}] {subprocess.list2cmdline([str(x) for x in command])}', flush=True)
        begin = time.monotonic()
        log = output / (name + '.log')
        with log.open('w', encoding='utf-8') as stream:
            process = subprocess.Popen([str(x) for x in command], cwd=ROOT, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
            for line in process.stdout:
                stream.write(line)
                if not quiet:
                    print(line, end='', flush=True)
            code = process.wait()
        report['commands'].append(dict(command=[str(x) for x in command], exit_code=code,
                                       seconds=round(time.monotonic() - begin, 3), log=str(log)))
        if code:
            raise RuntimeError(f'{name} failed with exit {code}; see {log}')
        return log.read_text(encoding='utf-8')

    try:
        cache = build / 'CMakeCache.txt'
        if not cache.is_file():
            raise RuntimeError('Configure first: cmake --preset win-amd64-release -DBUILD_TESTING=ON '
                               '-DCMAKE_PREFIX_PATH=<your-rexglue-sdk> (in a VS developer shell)')
        cache_text = cache.read_text(encoding='utf-8')
        if not re.search(r'^BUILD_TESTING:BOOL=ON$', cache_text, re.M):
            raise RuntimeError('This build must be configured with -DBUILD_TESTING=ON')
        report['cache_sha256_before'] = hashlib.sha256(cache.read_bytes()).hexdigest()
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        report['worktree_status'] = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
        report['source_id_start'] = digest(source_manifest(ROOT))
        fixture = ROOT / 'tests/fixtures/static-pass-replay-v1.txt'
        report['fixture_sha256'] = hashlib.sha256(fixture.read_bytes()).hexdigest()
        # Reconfigure the existing cache; never substitute another SDK or build the game target.
        run(['cmake', '-S', ROOT, '-B', build], 'configure')
        run(['cmake', '--build', build, '--target', *selected, '--parallel', args.jobs], 'build')
        regex = '^(' + '|'.join(re.escape(x) for x in selected) + ')$'
        discovery = json.loads(run(['ctest', '--test-dir', build, '-R', regex, '--show-only=json-v1'], 'discover', quiet=True))
        found = [test['name'] for test in discovery['tests']]
        if sorted(found) != sorted(selected):
            raise RuntimeError(f'CTest selection mismatch: expected {selected}, found {found}')
        junit = output / 'tests.xml'
        run(['ctest', '--test-dir', build, '-R', regex, '--output-on-failure', '--no-tests=error',
             '--parallel', '1', '--timeout', '120', '--output-junit', junit], 'tests')
        cases = list(ET.parse(junit).getroot().iter('testcase'))
        if sorted(case.attrib['name'] for case in cases) != sorted(selected):
            raise RuntimeError('JUnit result does not contain every selected test exactly once')
        if any(case.find(tag) is not None for case in cases for tag in ('skipped', 'failure', 'error')):
            raise RuntimeError('A selected test was skipped or failed')
        report['tests'] = [dict(case.attrib) for case in cases]
        report['source_id_end'] = digest(source_manifest(ROOT))
        if report['source_id_end'] != report['source_id_start']:
            raise RuntimeError('Source changed during validation; rerun against a stable checkout')
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, ET.ParseError) as error:
        report['error'] = str(error)
        print(f'ERROR: {error}', file=sys.stderr)
    finally:
        report['seconds'] = round(time.monotonic() - started, 3)
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'Report: {output / "report.json"}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
