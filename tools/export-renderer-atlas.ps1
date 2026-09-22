param([string]$Java='C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/java.exe')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=Join-Path $root 'out/renderer-atlas'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$census=Join-Path $root 'out/renderer-inventory/complete-function-inventory.csv'
$entries=@(Import-Csv -LiteralPath $census | ForEach-Object { $_.function.Substring(4).ToUpperInvariant() } | Sort-Object -Unique)
if($entries.Count -ne 9216) { throw "Expected 9216 census functions, got $($entries.Count)" }
$addresses=Join-Path $out 'addresses.txt'
$entries | Set-Content -LiteralPath $addresses -Encoding ascii
$pins=[ordered]@{}
$inputs=@((Join-Path $root 'tools/ghidra/RendererAtlas.java'),$census)
$inputs+=@(Get-ChildItem -LiteralPath (Join-Path $root 'out/ghidra/project') -Recurse -File | Where-Object { $_.Name -notmatch '\.lock' } | Sort-Object FullName | ForEach-Object { $_.FullName })
foreach($path in $inputs){ $pins[$path.Substring($root.Length+1).Replace('\','/')]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
$pins['ghidra-version']='12.1.3';$pins['timeout-seconds']=30
$json=$pins | ConvertTo-Json -Compress
$sha=[Security.Cryptography.SHA256]::Create()
$key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','').ToLowerInvariant()
$facts=Join-Path $out "exports/$key"
New-Item -ItemType Directory -Force -Path $facts | Out-Null
[ordered]@{context=$key;inputs=$pins;facts="out/renderer-atlas/exports/$key"} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'export-context.json') -Encoding utf8
$install=Join-Path $root 'out/ghidra/installation'
$cp="$install/Ghidra/Framework/Utility/lib/Utility.jar;$install/Ghidra/Framework/Generic/lib/gson-2.13.2.jar"
& $Java '-Djava.system.class.loader=ghidra.GhidraClassLoader' "-Dapplication.settingsdir=$root/out/ghidra/settings" "-Dapplication.cachedir=$root/out/ghidra/cache" "-Dapplication.tempdir=$root/out/ghidra/temp" -Xmx3G -cp $cp ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless "$root/out/ghidra/project" edf2027-geometry -process guest_image.bin -noanalysis -readOnly -scriptPath "$root/tools/ghidra" -postScript RendererAtlas.java $facts $addresses $key -log "$out/export.log"
if($LASTEXITCODE -ne 0){throw "Atlas export failed: $LASTEXITCODE"}
if(-not (Test-Path -LiteralPath (Join-Path $facts 'export-summary.json'))){throw 'Atlas export incomplete; rerun resumes cached functions'}
