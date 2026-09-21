"""Summarize --edf_native_motion_trace output without equating FPS with motion."""
import argparse
import json
import re
from collections import defaultdict
from pathlib import Path


def summarize(text):
    pattern = re.compile(
        r"Native motion trace: seq=(\d+) us=(\d+) scene=(0x[0-9a-f]+) "
        r"viewport=(\d+) matrix32=(0x[0-9a-f]+) matrix96=(0x[0-9a-f]+)"
    )
    groups = defaultdict(list)
    for match in pattern.finditer(text):
        sequence, timestamp, scene, viewport, first, second = match.groups()
        groups[(scene, int(viewport))].append(
            (int(sequence), int(timestamp), first, second)
        )
    result = []
    for (scene, viewport), samples in groups.items():
        samples.sort(key=lambda sample: sample[0])
        elapsed = (samples[-1][1] - samples[0][1]) / 1_000_000
        transitions = len(samples) - 1
        changed32 = sum(a[2] != b[2] for a, b in zip(samples, samples[1:]))
        changed96 = sum(a[3] != b[3] for a, b in zip(samples, samples[1:]))
        changed = sum(a[2:] != b[2:] for a, b in zip(samples, samples[1:]))
        windows = defaultdict(lambda: {"transitions": 0, "changed_pairs": 0})
        for previous, current in zip(samples, samples[1:]):
            second = (current[1] - samples[0][1]) // 1_000_000
            windows[second]["transitions"] += 1
            windows[second]["changed_pairs"] += previous[2:] != current[2:]
        result.append({
            "scene": scene,
            "viewport": viewport,
            "samples": len(samples),
            "elapsed_seconds": elapsed,
            "submissions_per_second": transitions / elapsed if elapsed > 0 else None,
            "matrix32_changes": changed32,
            "matrix96_changes": changed96,
            "unchanged_matrix_pairs": transitions - changed,
            "changed_pairs_per_second": changed / elapsed if elapsed > 0 else None,
            "one_second_windows": [
                {"second": second, **counts} for second, counts in sorted(windows.items())
            ],
        })
    return {
        "scenes": result,
        "interpretation": (
            "Unchanged camera matrices can be intentional for a stationary camera. "
            "Changes do not prove object animation, interpolation, or scanout cadence. "
            "Scene addresses may be reused across transitions."
        ),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(summarize(args.log.read_text(errors="replace")), indent=2)
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)
