param([string]$Java='C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/java.exe',
      [ValidateSet('RendererInventory.java','RendererRegistration.java','RendererRenderable.java')][string]$Script='RendererInventory.java',
      [ValidateSet('renderable','state','contracts','helpers','dispatch','passes','retained')][string]$AnalysisSet='renderable')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& $Java '-Djava.system.class.loader=ghidra.GhidraClassLoader' `
  "-Dapplication.settingsdir=$root/out/ghidra/settings" `
  "-Dapplication.cachedir=$root/out/ghidra/cache" `
  "-Dapplication.tempdir=$root/out/ghidra/temp" -Xmx2G `
  -cp "$root/out/ghidra/installation/Ghidra/Framework/Utility/lib/Utility.jar" `
  ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless `
  "$root/out/ghidra/project" edf2027-geometry -process guest_image.bin -noanalysis -readOnly `
  -scriptPath $PSScriptRoot -postScript $Script "$root/out/ghidra/renderer-inventory" $AnalysisSet `
  -log "$root/out/ghidra/renderer-inventory.log"
if($LASTEXITCODE -ne 0) { throw "Ghidra renderer inventory failed: $LASTEXITCODE" }
