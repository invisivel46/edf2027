"""Coverage report: what the full-frame renderer drew and what it silently dropped.

Reads a game.log (with its rotated parts game.1.log .. game.N.log, oldest
first, as renderer-runtime-gate.py does) from a run with
--edf_native_coverage_census=true and reports the census's items
(src/native_graphics/native_coverage_census.h):

  Native coverage: <status> class=<name> vtable=0x<hex> reason=<reason> frames=F objects=O peak=K
      first_seen=S.Ss last_seen=S.Ss detail=<text>
  Native coverage summary: kind=<window|final> seconds=S frames=F ... coverage=X%

Every value is cumulative from the census start, so the last line of each
(status, class, vtable, reason) is its total. Statuses: covered (a native pass
drew it), uncovered (the guest render helper would have drawn it and no native
pass did) and parity (dropped where the guest drops it too). Coverage is covered
objects over covered plus uncovered objects.

Gate: every uncovered item must match an --allow CLASS:REASON (repeatable).
CLASS is a class name or a 0x vtable; either part may use fnmatch wildcards;
the split is at the last ':', so 'pass:sky:declined' and 'pass:*:declined'
name pass classes. --allow-file reads one pattern per line ('#' comments).
--min-coverage PCT also fails a run whose coverage is lower; --require-final
fails a run without the exit summary (killed, crashed).

Prints a table, or JSON with --json. Exits 0 on pass, 1 on an unallowed
uncovered item or a failed gate, 2 when the log has no census lines.
"""
import argparse
import fnmatch
import importlib.util
import json
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, TOOLS / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gate = _load('renderer_runtime_gate', 'renderer-runtime-gate.py')

ITEM = re.compile(r'Native coverage: (covered|uncovered|parity) class=(\S+) vtable=0x([0-9A-Fa-f]+) reason=(\S+) '
                  r'frames=(\d+) objects=(\d+) peak=(\d+) first_seen=([\d.]+)s last_seen=([\d.]+)s detail=(.*)$')
SUMMARY = re.compile(r'Native coverage summary: kind=(\S+) seconds=([\d.]+) frames=(\d+)')
STATUSES = ('uncovered', 'parity', 'covered')


def parse(lines):
    """(items, summaries): the last line of each item key, in first-seen order,
    and every summary line's (kind, seconds, frames)."""
    items = {}
    summaries = []
    for line in lines:
        m = SUMMARY.search(line)
        if m:
            summaries.append({'kind': m.group(1), 'seconds': float(m.group(2)), 'frames': int(m.group(3))})
            continue
        m = ITEM.search(line)
        if not m:
            continue
        status, name, vtable, reason = m.group(1), m.group(2), int(m.group(3), 16), m.group(4)
        items[(status, name, vtable, reason)] = {
            'status': status, 'class': name, 'vtable': f'0x{vtable:08X}', 'reason': reason,
            'frames': int(m.group(5)), 'objects': int(m.group(6)), 'peak': int(m.group(7)),
            'first_seen': float(m.group(8)), 'last_seen': float(m.group(9)), 'detail': m.group(10).strip()}
    return list(items.values()), summaries


def parse_allow(text):
    """CLASS:REASON, split at the last ':' (class names like pass:sky hold one)."""
    if ':' not in text:
        raise argparse.ArgumentTypeError(f'expected CLASS:REASON, got {text!r}')
    name, reason = text.rsplit(':', 1)
    if not name or not reason:
        raise argparse.ArgumentTypeError(f'expected CLASS:REASON, got {text!r}')
    return name, reason


def read_allow_file(path):
    patterns = []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            patterns.append(parse_allow(line))
    return patterns


def allowed(item, patterns):
    """The first pattern that matches the item, or None. The class part matches
    the class name or the vtable (case-insensitive hex)."""
    for name, reason in patterns:
        if not fnmatch.fnmatchcase(item['reason'], reason):
            continue
        if fnmatch.fnmatchcase(item['class'], name) or fnmatch.fnmatchcase(item['vtable'].lower(), name.lower()):
            return f'{name}:{reason}'
    return None


def report(items, summaries, patterns):
    totals = {status: sum(i['objects'] for i in items if i['status'] == status) for status in STATUSES}
    drawn, all_objects = totals['covered'], totals['covered'] + totals['uncovered']
    coverage = 100.0 * drawn / all_objects if all_objects else 100.0
    for item in items:
        item['allowed'] = allowed(item, patterns) if item['status'] == 'uncovered' else None
    ordered = sorted(items, key=lambda i: (STATUSES.index(i['status']), -i['objects'], i['class'], i['reason']))
    unallowed = [i for i in ordered if i['status'] == 'uncovered' and not i['allowed']]
    used = {i['allowed'] for i in items if i['allowed']}
    last = summaries[-1] if summaries else None
    return {
        'items': ordered,
        'unallowed': unallowed,
        'unused_allows': [f'{n}:{r}' for n, r in patterns if f'{n}:{r}' not in used],
        'objects': totals,
        'coverage': coverage,
        'covered_classes': sorted({i['class'] for i in items if i['status'] == 'covered'}),
        'uncovered_classes': sorted({i['class'] for i in items if i['status'] == 'uncovered'}),
        'frames': last['frames'] if last else max((i['frames'] for i in items), default=0),
        'seconds': last['seconds'] if last else None,
        'final': any(s['kind'] == 'final' for s in summaries),
    }


def print_table(result, out=None):
    out = out or sys.stdout
    print(f"coverage-report: frames={result['frames']} seconds={result['seconds']} final={result['final']} "
          f"coverage={result['coverage']:.3f}% covered_objects={result['objects']['covered']} "
          f"uncovered_objects={result['objects']['uncovered']} parity_objects={result['objects']['parity']}", file=out)
    print(f"classes: covered={len(result['covered_classes'])} uncovered={len(result['uncovered_classes'])}", file=out)
    header = f"{'status':<10} {'class':<34} {'vtable':<10} {'reason':<34} {'frames':>8} {'objects':>10} {'peak':>6}  note"
    print(header, file=out)
    for item in result['items']:
        if item['status'] == 'uncovered':
            note = f"allowed by {item['allowed']}" if item['allowed'] else 'NOT ALLOWED'
        else:
            note = ''
        if item['detail'] not in ('', '-'):
            note = (note + ' ' if note else '') + item['detail']
        print(f"{item['status']:<10} {item['class']:<34} {item['vtable']:<10} {item['reason']:<34} {item['frames']:>8} "
              f"{item['objects']:>10} {item['peak']:>6}  {note}", file=out)
    for pattern in result['unused_allows']:
        print(f'note: --allow {pattern} matched nothing', file=out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('log')
    ap.add_argument('--allow', type=parse_allow, action='append', default=[], metavar='CLASS:REASON',
                    help='an accepted gap (repeatable; fnmatch wildcards; CLASS may be a 0x vtable)')
    ap.add_argument('--allow-file', action='append', default=[], metavar='PATH',
                    help='CLASS:REASON patterns, one per line (# comments)')
    ap.add_argument('--min-coverage', type=float, default=None, metavar='PCT',
                    help='fail when covered / (covered + uncovered) objects is below PCT')
    ap.add_argument('--require-final', action='store_true', help='fail without the exit summary')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args(argv)
    patterns = list(args.allow)
    for path in args.allow_file:
        patterns += read_allow_file(path)
    items, summaries = parse(gate.read_log_lines(args.log))
    if not items and not summaries:
        print('coverage-report: no "Native coverage" lines (run with --edf_native_coverage_census=true)', file=sys.stderr)
        return 2
    result = report(items, summaries, patterns)
    failures = [f"uncovered {i['class']}:{i['reason']} ({i['objects']} objects over {i['frames']} frames)"
                for i in result['unallowed']]
    if args.min_coverage is not None and result['coverage'] < args.min_coverage:
        failures.append(f"coverage {result['coverage']:.3f}% below {args.min_coverage}%")
    if args.require_final and not result['final']:
        failures.append('no final summary (the run did not exit cleanly)')
    result['failures'] = failures
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_table(result)
        for failure in failures:
            print(f'FAIL: {failure}')
        if not failures:
            print('PASS')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
