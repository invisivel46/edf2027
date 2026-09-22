$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=Join-Path $root 'out/renderer-atlas'
$context=Get-Content -LiteralPath (Join-Path $out 'export-context.json') -Raw | ConvertFrom-Json
$facts=Join-Path $root $context.facts
$install=Join-Path $root 'out/ghidra/installation'
$java='C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/java.exe'
$cp="$install/Ghidra/Framework/Utility/lib/Utility.jar;$install/Ghidra/Framework/Generic/lib/gson-2.13.2.jar"
& $java '-Djava.system.class.loader=ghidra.GhidraClassLoader' "-Dapplication.settingsdir=$root/out/ghidra/settings" "-Dapplication.cachedir=$root/out/ghidra/cache" "-Dapplication.tempdir=$root/out/ghidra/temp" -Xmx3G -cp $cp ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless "$root/out/ghidra/project" edf2027-geometry -process guest_image.bin -noanalysis -readOnly -scriptPath "$root/tools/ghidra" -postScript RendererAtlasRetry.java $facts "$out/addresses.txt" -log "$out/retry.log"
if($LASTEXITCODE -ne 0){throw "Atlas retry failed: $LASTEXITCODE"}
$pins=[ordered]@{}
foreach($name in @('tools/ghidra/RendererAtlasRetry.java','tools/retry-renderer-atlas.ps1')){$pins[$name]=(Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
[ordered]@{context=$context.context;timeout_seconds=120;inputs=$pins} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'retry-context.json') -Encoding utf8
