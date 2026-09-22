"""Collect repeatable runtime evidence; does not infer feature execution from input."""
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'out/renderer-coverage'
results=[]
for metadata in ('process.json','probe-process.json'):
    run=json.loads((OUT/metadata).read_text(encoding='utf-8-sig'))
    logs=sorted(Path(run['RunDirectory']).glob('game*.log'))
    lines=sorted(line for path in logs for line in path.read_text(encoding='utf-8',errors='replace').splitlines() if line.startswith('['))
    errors=[l for l in lines if '[error]' in l or '[critical]' in l]
    nonzero=[l for l in lines if re.search(r'\b(?:\w*mismatch\w*|errors|parameter_errors|texture_missing|texture_binding_errors)=[1-9]\d*',l)
             or ('Native shader bridge:' in l and re.search(r'\bmisses=[1-9]\d*',l))]
    phases=sorted(set(re.findall(r'Native hook timing: phase=([^ ]+)', '\n'.join(lines))))
    inputs=[l for l in lines if re.search(r'Scripted pad: (state|analog|reloaded)',l)]
    captures=[l for l in lines if 'output capture:' in l or 'host capture:' in l]
    loading=[l for l in lines if 'Native loading' in l or 'LoadMap' in l]
    last={}
    for l in lines:
        for marker in ('Native indexed upload:','Native shader bridge:','Native scene queued:',
                       'Native scene published geometry draws:','Native scene geometry preload:',
                       'Native scene material preload:','Native scene transforms:','Native host timing',
                       'Native loading frame:','Native scene adapter:'):
            if marker in l: last[marker]=l
    results.append(dict(metadata=metadata,run=run,log_files=[dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in logs],
                        first_log=lines[0] if lines else None,last_log=lines[-1] if lines else None,
                        errors=errors,nonzero_failure_samples=nonzero,hook_phases=phases,
                        input_events=inputs,captures=captures,loading_samples=loading[:8]+loading[-8:],latest_samples=last,
                        scope='Script delivery and named hook phases are observations, not complete receiver/family or visual-parity coverage. Zero mismatches with zero checks is not validation.'))
(OUT/'runtime-results.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
print(json.dumps([dict(run=r['metadata'],errors=len(r['errors']),nonzero_failure_samples=len(r['nonzero_failure_samples']),phases=r['hook_phases'],captures=len(r['captures']),first=r['first_log'],last=r['last_log']) for r in results],indent=2))
