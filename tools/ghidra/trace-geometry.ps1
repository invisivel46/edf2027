param(
    [string]$Java = 'C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/java.exe',
    [string]$Installation = 'out/ghidra/installation',
    [string]$Output = 'out/ghidra/geometry-writers-inline'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$installationPath = [IO.Path]::GetFullPath((Join-Path $root $Installation))
$outputPath = [IO.Path]::GetFullPath((Join-Path $root $Output))
& $Java '-Djava.system.class.loader=ghidra.GhidraClassLoader' `
    "-Dapplication.settingsdir=$root/out/ghidra/settings" `
    "-Dapplication.cachedir=$root/out/ghidra/cache" `
    "-Dapplication.tempdir=$root/out/ghidra/temp" -Xmx2G `
    -cp "$installationPath/Ghidra/Framework/Utility/lib/Utility.jar" `
    ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless `
    "$root/out/ghidra/project" edf2027-geometry -process guest_image.bin -noanalysis `
    -scriptPath $PSScriptRoot -postScript GeometryWriterTrace.java $outputPath `
    -log "$root/out/ghidra/headless-inline.log"
if ($LASTEXITCODE -ne 0) { throw "Ghidra exited with code $LASTEXITCODE" }
