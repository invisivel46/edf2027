"""Export the bounded native activation setter population from current source."""
import csv,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
path=ROOT/'src/native_graphics/native_material_render_state.h';source=path.read_text()
mapping=source.split('inline uint32_t NativeMaterialStateSetter',1)[1].split('inline uint32_t NativeMaterialUnifiedBlend',1)[0]
pairs=re.findall(r'case (0x[0-9a-f]+):return (0x[0-9a-f]+);',mapping)
cpu=source.split('inline std::optional<std::vector<std::pair<uint32_t,uint32_t>>> NativeMaterialStateCpuWrites',1)[1].split('template<class Reader>',1)[0]
assert re.findall(r'if\(offset==(0x[0-9a-f]+)\) return \{\};',cpu)==['0xc8']
cases=set(re.findall(r'case (0x[0-9a-f]+):',cpu))
assert cases=={o for o,_ in pairs}-{'0xc8'}
rows=[dict(offset=o,setter='sub_'+s[2:].upper(),
    activation_route='hooked guest setter then state publication' if o=='0xc8' else 'native CPU mirror; guest setter only for sampled audit',
    scope='accepted offset only; device table identity checked; unknown offsets throw') for o,s in pairs]
with (OUT/'material-setter-population.csv').open('w',newline='',encoding='utf-8') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=dict(accepted_offsets=len(rows),native_cpu_mirror_offsets=len(cases),non_audit_callback_offsets=['0xc8'],
 source=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
 limitation='Source route coverage, not exhaustive equivalence of CPU mirror values to PPC.')
(OUT/'material-setter-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
