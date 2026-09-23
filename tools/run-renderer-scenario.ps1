# Run one renderer validation scenario unattended: seed the save it needs, launch
# the game through start-native-binding-validation.ps1 with the variant's flags,
# wait, stop exactly that process, and print where the log and captures are and
# the commands that judge them.
#
#   tools/run-renderer-scenario.ps1 -Scenario cave -Variant ab -Executable out/build/win-amd64-release/edf2027-abc1234.exe
#
# Scenarios and variants live in tools/renderer-scenarios.json. Variants:
#   ab             locked, --edf_native_ab_alternate=1, 64 consecutive output
#                  captures from the scenario's capture_start frame
#   unlocked       native at a 120 FPS cap, no vsync, frame times + GPU timings
#   soak-ab        A/B for the whole run, one capture every 701 frames (128)
#   soak-unlocked  unlocked native with frame times, GPU timings, memory log
# -Seconds and -CaptureStart override the scenario's values. -NoWait launches
# and returns (the caller stops the process). -DryRun prints the plan only.
# -NoMemoryLog drops --edf_native_memory_log for executables built before it.
param(
  [Parameter(Mandatory = $true)][string]$Scenario,
  [Parameter(Mandatory = $true)][ValidateSet('ab', 'unlocked', 'soak-ab', 'soak-unlocked')][string]$Variant,
  [string]$Executable = 'out/build/win-amd64-release/edf2027.exe',
  [int]$Seconds = 0,
  [int]$CaptureStart = 0,
  [string]$Tag = '',
  [string[]]$ExtraArgs = @(),
  [switch]$NoWait,
  [switch]$NoMemoryLog,
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$table = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'renderer-scenarios.json') | ConvertFrom-Json
$entry = $table.scenarios.$Scenario
if ($null -eq $entry) {
  throw "Unknown scenario '$Scenario'. Known: $(($table.scenarios.PSObject.Properties.Name) -join ', ')"
}
$variantEntry = $table.variants.$Variant
if (-not [IO.Path]::IsPathRooted($Executable)) { $Executable = Join-Path $workspace $Executable }
if (-not $Tag) { $Tag = [IO.Path]::GetFileNameWithoutExtension($Executable) }
if ($Seconds -le 0) { $Seconds = [int]$entry.seconds.$Variant }
if ($CaptureStart -le 0) { $CaptureStart = [int]$entry.capture_start }

$outRoot = Join-Path $workspace 'out/renderer-scenarios'
$seedDir = $null
if ($null -ne $entry.seed) {
  $seedDir = Join-Path $outRoot ('seeds/' + $Scenario)
}
$gameArgs = @($variantEntry.args | Where-Object { -not ($NoMemoryLog -and $_ -like '--edf_native_memory_log=*') })
$captureDir = $null
if ($variantEntry.captures) {
  $captureDir = Join-Path $workspace ('out/renderer-ab/scenario-' + $Scenario + '-' + $Variant + '-' + $Tag)
  # Forward slashes and no quotes: the path is passed through verbatim.
  $prefix = (Join-Path $captureDir 'cap') -replace '\\', '/'
  $gameArgs += @(('--edf_native_scene_capture=' + $prefix), ('--edf_native_output_capture_start_frame=' + $CaptureStart))
}
$gameArgs += $ExtraArgs
$plan = [ordered]@{ scenario = $Scenario; variant = $Variant; executable = $Executable; input = $entry.input
  mission = $entry.mission; seed = $seedDir; seed_args = $entry.seed; seconds = $Seconds; captures = $captureDir
  args = $gameArgs }
if ($DryRun) { [pscustomobject]$plan | ConvertTo-Json -Depth 4; return }

if ($seedDir) {
  if (Test-Path -LiteralPath $seedDir) { Remove-Item -LiteralPath $seedDir -Recurse -Force }
  $python = (Get-Command python -ErrorAction Stop).Source
  & $python (Join-Path $PSScriptRoot 'make-edf-save.py') $seedDir @($entry.seed) | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "make-edf-save.py failed for $Scenario" }
}
if ($captureDir) {
  if (Test-Path -LiteralPath $captureDir) { throw "Capture directory exists; pick another -Tag: $captureDir" }
  New-Item -ItemType Directory -Path $captureDir | Out-Null
}
$launch = @{ Executable = $Executable; InputScript = $entry.input; ExtraArgs = $gameArgs }
if ($seedDir) { $launch.SaveSeed = $seedDir }
$run = & (Join-Path $PSScriptRoot 'start-native-binding-validation.ps1') @launch
$exitedEarly = $false
if (-not $NoWait) {
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Id $run.Id -ErrorAction SilentlyContinue)) { $exitedEarly = $true; break }
    Start-Sleep -Seconds 5
  }
  $proc = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
  if ($proc -and $proc.Path -eq $run.Executable) { Stop-Process -Id $run.Id -Force; Start-Sleep -Seconds 3 }
}
$log = $run.Log
$gate = "python tools/renderer-runtime-gate.py $log --baseline <baseline game.log> --expect-mission $($entry.mission)"
$result = [ordered]@{ scenario = $Scenario; variant = $Variant; tag = $Tag; pid = $run.Id; log = $log
  run_directory = $run.RunDirectory; captures = $captureDir; seed = $seedDir; mission = $entry.mission
  exited_early = $exitedEarly; waited = -not $NoWait
  next = @(
    $(if ($Variant -eq 'ab') { "python tools/compare-renderer-ab-captures.py $captureDir $log --diff-image $captureDir/worst.png" }),
    $(if ($Variant -eq 'soak-ab') { "python tools/compare-renderer-ab-captures.py $captureDir $log --max-gap 701 --diff-image $captureDir/worst.png" }),
    $gate,
    $(if ($Variant -ne 'ab') { "python tools/frame-time-report.py $log" }),
    $(if ($Variant -like 'soak-*') { "python tools/soak-report.py $log --require-gameplay" })
  ) | Where-Object { $_ } }
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
([pscustomobject]$result | ConvertTo-Json -Compress -Depth 4) | Add-Content -Encoding utf8 -LiteralPath (Join-Path $outRoot 'runs.jsonl')
[pscustomobject]$result
