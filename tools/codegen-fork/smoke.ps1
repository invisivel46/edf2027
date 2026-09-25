# Correctness smoke run for a codegen-fork build (no timing): wait until no other EDF
# game runs, launch one scenario muted and uncapped, let it run, stop it, and report
# whether it reached its markers and what the log says about faults.
#
#   tools/codegen-fork/smoke.ps1 -Exe out/exp/ipa1 -Scenario benchmark-skipintro -Seconds 200 -Tag ipa1-m1
#   tools/codegen-fork/smoke.ps1 -Exe out/exp/ipa1 -Scenario horde-ants-1000 -Seconds 240 -Tag ipa1-horde
#
# -Extra passes more game flags. The log is copied to out/codegen-fork/smoke/<Tag>.log.
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [string]$Scenario = 'benchmark-skipintro',
  [int]$Seconds = 200,
  [Parameter(Mandatory = $true)][string]$Tag,
  [string[]]$Extra = @()
)
$ErrorActionPreference = 'Continue'
$wt = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = Join-Path $wt 'out\codegen-fork\smoke'
New-Item -ItemType Directory -Force $out | Out-Null
while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep 5 }
$exePath = Join-Path $Exe 'edf2027.exe'
$args2 = @('--edf_fps_cap=0') + $Extra
Push-Location $wt
$r = & "$wt\tools\run-renderer-scenario.ps1" -Scenario $Scenario -Variant unlocked -Executable $exePath -Tag $Tag -NoWait -Seconds ($Seconds + 60) -ExtraArgs $args2
Pop-Location
$start = Get-Date
$exited = $false
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
  if (-not (Get-Process -Id $r.pid -ErrorAction SilentlyContinue)) { $exited = $true; break }
  Start-Sleep 2
}
Stop-Process -Id $r.pid -Force -ErrorAction SilentlyContinue
Start-Sleep 3
$log = Join-Path $out "$Tag.log"
Copy-Item $r.log $log -ErrorAction SilentlyContinue
$text = Get-Content -Raw -LiteralPath $log -ErrorAction SilentlyContinue
function Has($p) { if ($text) { return $text.Contains($p) } else { return $false } }
$faults = @()
foreach ($p in @('SEH exception caught', 'FATAL', 'Unhandled exception', 'access violation', 'Access violation', 'EXCEPTION_', 'Invalid function', 'crash')) {
  $n = ([regex]::Matches($text, [regex]::Escape($p))).Count
  if ($n -gt 0) { $faults += "$p x$n" }
}
[pscustomobject]@{
  tag = $Tag; exe = $exePath; scenario = $Scenario; seconds = [int]((Get-Date) - $start).TotalSeconds
  exited_by_itself = $exited; mission_cam = (Has "MISSION.CAM"); horde_hold_end = (Has "horde hold-end")
  faults = ($faults -join '; '); log = $log
} | ConvertTo-Json
