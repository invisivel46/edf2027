param([switch]$ReuseExport)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    if(-not $ReuseExport){ & ./tools/export-renderer-atlas.ps1 }
    $retryNeeded=& python -c "import json; from pathlib import Path; c=json.loads(Path('out/renderer-atlas/export-context.json').read_text(encoding='utf-8-sig')); p=Path('out/renderer-atlas/retry-context.json'); stale=p.exists() and json.loads(p.read_text(encoding='utf-8-sig'))['context']!=c['context']; print(int(stale or any(json.loads(f.read_text()).get('decompiler_timeout',False) for f in Path(c['facts']).glob('????????.json'))))"
    if($LASTEXITCODE -ne 0){throw 'Cannot inspect atlas export'}
    if($retryNeeded -eq '1'){ & ./tools/retry-renderer-atlas.ps1 }
    & python tools/renderer_atlas.py build
    if($LASTEXITCODE -ne 0){throw 'Atlas build/audit failed'}
    Write-Host 'Open out/renderer-atlas/index.html'
} finally { Pop-Location }
