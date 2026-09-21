"""Report bounded instance identity observations; addresses are not lifetime IDs."""
import argparse
import json
import re
from collections import Counter
from pathlib import Path


def summarize(text):
    pattern = re.compile(
        r"Native instance motion: list=(0x[0-9a-f]+) data=(0x[0-9a-f]+) "
        r"first=(\d+) count=(\d+) name=(\S+) samples=(\d+) changes=(\d+) same_frame_changes=(\d+)"
    )
    rows = []
    for match in pattern.finditer(text):
        owner, data, first, count, name, samples, changes, unstable = match.groups()
        rows.append({"list": owner, "data": data, "first": int(first), "count": int(count),
                     "name": name, "samples": int(samples), "changes": int(changes),
                     "same_frame_changes": int(unstable)})
    completed = re.search(r"Native instance motion: complete frames=(\d+) sources=(\d+)", text)
    names = Counter(row["name"] for row in rows)
    aliases = {}
    for row in rows:
        aliases.setdefault(row["data"], set()).add(row["list"])
    palettes = []
    palette_pattern = re.compile(
        r"Native palette motion: record=(0x[0-9a-f]+) data=(0x[0-9a-f]+) "
        r"registers=(\d+) samples=(\d+) changes=(\d+) same_frame_changes=(\d+)"
    )
    for match in palette_pattern.finditer(text):
        record, data, registers, samples, changes, within = match.groups()
        palettes.append({"record": record, "data": data, "registers": int(registers),
                         "samples": int(samples), "changes": int(changes),
                         "same_frame_changes": int(within)})
    models = []
    model_pattern = re.compile(
        r"Native model motion: source=(0x[0-9a-f]+) vector=(0x[0-9a-f]+) "
        r"bones=(\d+) samples=(\d+) source_changes=(\d+) rendered_changes=(\d+) "
        r"blended=(\d+) tick=(\d+) phase=([0-9.eE+-]+)(?: render_dependent=(true|false))?"
    )
    for match in model_pattern.finditer(text):
        source, vector, bones, samples, original, rendered, blended, tick, phase, dependent = match.groups()
        models.append({"source": source, "vector": vector, "bones": int(bones), "samples": int(samples),
                       "source_changes": int(original), "rendered_changes": int(rendered),
                       "blended": int(blended), "tick": int(tick), "phase": float(phase),
                       "render_dependent": None if dependent is None else dependent == "true"})
    return {
        "complete": completed is not None,
        "observed_frames": int(completed[1]) if completed else None,
        "reported_sources": int(completed[2]) if completed else None,
        "parameters": dict(names),
        "changing_sources": sum(row["changes"] > 0 for row in rows),
        "sources_changing_within_frame": sum(row["same_frame_changes"] > 0 for row in rows),
        "shared_data_addresses": {data: sorted(owners) for data, owners in aliases.items() if len(owners) > 1},
        "sources": rows,
        "palettes": palettes,
        "palette_trace_complete": "Native palette motion: complete" in text,
        "model_motion": models,
        "interpretation": "A stable observed address is not proof of object lifetime or exclusive ownership.",
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
