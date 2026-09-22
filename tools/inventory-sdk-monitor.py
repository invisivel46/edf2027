"""Capture local SDK monitor source and installed-library provenance, read-only."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
SDK=Path('D:/roms2/edf3-translation-project/rexglue/rexglue-sdk')
files=[ROOT/'out/build/win-amd64-release/CMakeCache.txt',
 SDK/'out/build/win-amd64/CMakeCache.txt',SDK/'ffx_install.log',
 SDK/'src/kernel/xboxkrnl/xboxkrnl_module.cpp',SDK/'src/kernel/CMakeLists.txt',
 Path('D:/roms2/edf3-translation-project/rexglue/sdk_ffx/win-amd64/lib/rexruntime.lib')]
rows=[]
for path in files:
 data=path.read_bytes()
 rows.append(dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
source=(SDK/'src/kernel/xboxkrnl/xboxkrnl_module.cpp').read_text().splitlines()
result=dict(files=rows,monitor_source_excerpt=[dict(line=i+1,text=source[i]) for i in range(43,108)],
 limitation='Build/install paths connect this source tree to the configured SDK; source-to-installed-binary equivalence and live monitor state are not proven.')
out=ROOT/'out/renderer-inventory/sdk-monitor-provenance.json'
out.write_text(json.dumps(result,indent=2)+'\n');print(f'{len(rows)} files fingerprinted; {out}')
