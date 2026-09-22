"""Build or audit renderer accounting without booting the game."""
import argparse
import json
from renderer_dispatch import ROOT, read_json, require, write_json
from renderer_tally import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reconciliation', default='docs/renderer-tally-reconciliation.json')
    parser.add_argument('--output', default='out/renderer-tally/tally.json')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    snapshot = build(ROOT, read_json(ROOT / args.reconciliation))
    if args.check:
        require(read_json(ROOT / args.output) == snapshot, 'Tally stale; rebuild and review changes')
    else:
        write_json(ROOT / args.output, snapshot)
    print(json.dumps(dict(snapshot_id=snapshot['snapshot_id'], **snapshot['summary']), indent=2))


if __name__ == '__main__':
    main()
