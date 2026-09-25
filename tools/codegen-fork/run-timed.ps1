# trun.ps1 -Exe <variant dir under out\exp> -Scenario benchmark|horde-ants-1000 -Tag t [-Profile seconds] [-Extra ...]
# Waits for a quiet machine (no other EDF game, no clang/ninja/lld), runs the scenario uncapped
# with --edf_step_timing, optionally samples the engine window with xperf, and copies the log.
param([string]$Out='',[string]$Exe,[string]$Scenario,[string]$Tag,[int]$Profile=0,[string[]]$Extra=@(),[switch]$NoQuietWait)
$ErrorActionPreference='Continue'
$wt=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$s= if ($Out) { $Out } else { Join-Path $wt 'out\codegen-fork\runs' }
$xperf='C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
New-Item -ItemType Directory -Force $s | Out-Null
$note="$s\runs.txt"
function Note($t){ $l="$(Get-Date -Format s) $t"; Add-Content $note $l; Write-Output $l }
function Busy { (@(Get-Process edf2027* -ErrorAction SilentlyContinue).Count + @(Get-Process clang*,ninja*,lld* -ErrorAction SilentlyContinue).Count) }
$args2=@('--edf_step_timing=true','--edf_fps_cap=0','--edf_native_frame_times=true','--edf_native_gpu_timings=false')+$Extra
$r=$null
while (-not $r) {
  if ($NoQuietWait) { while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep 5 } }
  else { $q=0; while ($q -lt 3) { if ((Busy) -eq 0) { $q++ } else { $q=0 }; Start-Sleep 5 } }
  Push-Location $wt
  try { $r=& "$wt\tools\run-renderer-scenario.ps1" -Scenario $Scenario -Variant unlocked -Executable "out\exp\$Exe\edf2027.exe" -Tag $Tag -NoWait -Seconds 900 -ExtraArgs $args2 -ErrorAction Stop }
  catch { Note "launch failed $Tag : $_"; $r=$null; Start-Sleep 20 }
  Pop-Location
}
Note "start $Tag exe=$Exe scenario=$Scenario pid=$($r.pid) log=$($r.log) extra=$($Extra -join ' ')"
$start=Get-Date; $marker=$null; $done=$false; $profiled=($Profile -le 0)
$pat= if ($Scenario -like 'horde*') { "mark 'horde hold-begin'" } else { "MISSION.CAM'" }
$profDelay= if ($Scenario -like 'horde*') { 10 } else { 20 }
$stopAfter= if ($Scenario -like 'horde*') { 160 } else { 100 }
while (-not $done -and ((Get-Date)-$start).TotalSeconds -lt 900) {
  if (-not (Get-Process -Id $r.pid -ErrorAction SilentlyContinue)) { Note "$Tag exited"; break }
  if (-not $marker) {
    $hit=Select-String -LiteralPath $r.log -SimpleMatch $pat -List -ErrorAction SilentlyContinue
    if ($hit) { $marker=Get-Date; Note "$Tag marker" }
  } else {
    $el=((Get-Date)-$marker).TotalSeconds
    if (-not $profiled -and $el -ge $profDelay) {
      $profiled=$true
      $k="$s\$Tag.kernel.etl"
      & $xperf -on PROC_THREAD+LOADER+PROFILE -f $k 2>&1 | Out-Null
      $b1=Busy
      Start-Sleep $Profile
      & $xperf -stop 2>&1 | Out-Null
      & $xperf -merge $k "$s\$Tag.etl" 2>&1 | Out-Null
      Remove-Item $k -ErrorAction SilentlyContinue
      Note "$Tag profiled ${Profile}s busy_at_start=$b1 busy_at_end=$(Busy)"
    }
    if ($el -ge $stopAfter) { $done=$true }
  }
  Start-Sleep 1
}
$busy=Busy
Stop-Process -Id $r.pid -Force -ErrorAction SilentlyContinue
Start-Sleep 3
Copy-Item $r.log "$s\$Tag.log" -ErrorAction SilentlyContinue
Note "end $Tag busy_other=$busy"
