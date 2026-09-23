"""Loading-screen timeline from a game.log (and its rotated game.N.log parts).

  python tools/load-trace-report.py <game.log> [<game.log> ...]

Works on any log from the markers every build writes: each loading presenter
start (the scene viewport logged from caller=0x8219c828 on a new thread), the
mission .Cam lookup, the static world's group order publications and frames
over 100 ms. With --edf_native_load_trace=true it also prints the trace's
phase events, per-phase category times (engine thread + other threads) and
each loading window's totals. Several logs are reported one after another,
e.g. the benchmark and benchmark-skipintro runs.
"""
import re
import sys
from datetime import datetime
from pathlib import Path

STAMP = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\] \[\w+\] \[\w+\] \[t(\d+)\] (.*)$')


def parts(path):
    """game.log plus its rotated parts, oldest first (game.N.log with the highest N is oldest)."""
    path = Path(path)
    rotated = sorted(path.parent.glob(path.stem + '.*' + path.suffix),
                     key=lambda p: -int(p.suffixes[-2][1:]) if p.suffixes[-2][1:].isdigit() else 0)
    return [p for p in rotated if p != path] + [path]


def lines(path):
    for part in parts(path):
        with open(part, encoding='utf-8', errors='replace') as handle:
            for line in handle:
                match = STAMP.match(line)
                if match:
                    yield datetime.strptime(match[1], '%Y-%m-%d %H:%M:%S.%f'), int(match[2]), match[3]


def report(path):
    print(f'== {path}')
    origin = None
    presenters, events = [], []
    for when, thread, text in lines(path):
        origin = origin or when
        t = (when - origin).total_seconds()
        if 'caller=0x8219c828' in text and 'untiled scene viewport' in text:
            presenters.append(t)
            events.append((t, 'marker', f'loading presenter frame 1 (thread {thread})'))
        elif '.CAM' in text.upper() and 'NtCreateFile' in text:
            events.append((t, 'marker', text.split('path=')[1].split(' ')[0]))
        elif 'group order publication' in text:
            groups = re.search(r'groups=(\d+)', text)
            events.append((t, 'marker', f'static world groups={groups[1]}'))
        elif 'Native frame spike' in text:
            ms = float(re.search(r' ms=([\d.]+)', text)[1])
            if ms >= 100:
                events.append((t, 'marker', f'frame {ms:.0f} ms (thread {thread})'))
        elif text.startswith('Native load trace: event='):
            events.append((t, 'trace', text[len('Native load trace: '):]))
        elif text.startswith('Native load trace: window'):
            events.append((t, 'window', text[len('Native load trace: '):]))
        elif text.startswith('Native load trace: tick'):
            events.append((t, 'tick', text[len('Native load trace: '):]))
        elif text.startswith('Scripted pad: state buttons=') and 'buttons=0x0000' not in text:
            events.append((t, 'pad', text[len('Scripted pad: state '):]))
    show_ticks = '--ticks' in sys.argv
    for t, kind, text in events:
        if kind == 'tick' and not show_ticks:
            continue
        print(f'{t:9.3f} s  {kind:6} {text}')
    # Loading screens by marker: presenter start to the next static world publication.
    print('-- loading screens (presenter start -> next static-world publication with groups > 0)')
    publications = [(t, text) for t, kind, text in events if kind == 'marker' and text.startswith('static world groups=')
                    and not text.endswith('=0')]
    # Menu transitions use the same presenter; a published world only follows
    # a mission's (or the intro's) loading screen, so the others read long.
    cams = [t for t, kind, text in events if kind == 'marker' and text.upper().endswith(".CAM'")]
    for start in presenters:
        end = next((t for t, _ in publications if t > start), None)
        kind = 'mission' if any(abs(start - t) < 1.0 for t in cams) else 'menu/intro'
        print(f'  {start:9.3f} s  {kind:10} ' +
              (f'{end - start:6.2f} s to a published world' if end else 'no world published after it'))


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 2
    for path in paths:
        report(path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
