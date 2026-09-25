param([string]$Name, [string[]]$Extra = @(), [int]$Seconds = 300, [long]$Affinity = 0,
      [int[]]$Windows = @(15, 130), [int]$Length = 30)
# Like run.ps1, plus xperf CPU sampling windows at the given seconds after the mission-load marker (MISSION.CAM).
$ErrorActionPreference = 'Continue'
$ws = 'D:\roms2\edf2027\.claude\worktrees\m1-intro-perf'
$s = '%USERPROFILE%\AppData\Local\Temp\claude\D--roms2-edf2027\c0bb9598-13c3-4be9-9d79-1b99d8f65ac7\scratchpad'
$xperf = 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$Exe = "$ws\out\build\win-amd64-release\edf2027.exe"
while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }
$outDir = "$s\runs\$Name"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force $outDir | Out-Null
$trace = "$outDir\trace.csv" -replace '\\','/'
$table = Get-Content -Raw "$ws\tools\renderer-scenarios.json" | ConvertFrom-Json
$entry = $table.scenarios.benchmark
$args2 = @('--edf_native_renderer=native','--edf_native_unlock_framerate=true','--edf_fps_cap=0','--edf_native_vsync=false',
  '--edf_native_frame_times=true','--edf_native_gpu_timings=true','--edf_frametime_log=true',
  ('--edf_native_frame_trace=' + $trace))
foreach ($e in $Extra) { $n = ($e -split '=',2)[0]; $args2 = @($args2 | Where-Object { (($_ -split '=',2)[0]) -ne $n }); $args2 += $e }
Push-Location $ws
$run = $null
while (-not $run) { while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }; try { $run = & "$ws\tools\start-native-binding-validation.ps1" -Executable $Exe -InputScript $entry.input -ExtraArgs $args2 -ErrorAction Stop } catch { $run = $null; Start-Sleep -Seconds 2 } }
Pop-Location
$p = Get-Process -Id $run.Id
if ($Affinity -ne 0) { $p.ProcessorAffinity = [IntPtr]$Affinity }
$sampler = Start-Process powershell -ArgumentList '-NoProfile','-File',"$s\threadsample.ps1",'-ProcessId',$run.Id,'-Out',"$outDir\threads.csv",'-Seconds',$Seconds -PassThru -WindowStyle Hidden
$start = Get-Date
$marker = $null
while (-not $marker -and ((Get-Date) - $start).TotalSeconds -lt $Seconds) {
  Start-Sleep -Seconds 1
  if (Test-Path $run.Log) {
    $hit = Select-String -LiteralPath $run.Log -Pattern "MISSION.CAM'" -SimpleMatch -List -ErrorAction SilentlyContinue
    if ($hit) { $marker = Get-Date }
  }
}
$i = 0
foreach ($w in $Windows) {
  if (-not $marker) { break }
  while (((Get-Date) - $marker).TotalSeconds -lt $w) { Start-Sleep -Milliseconds 200 }
  $k = "$outDir\w$i.kernel.etl"
  & $xperf -on PROC_THREAD+LOADER+PROFILE -stackwalk Profile -f $k 2>&1 | Out-Null
  Add-Content "$outDir\windows.txt" "w$i start $((Get-Date).ToString('o'))"
  Start-Sleep -Seconds $Length
  & $xperf -stop 2>&1 | Out-Null
  Add-Content "$outDir\windows.txt" "w$i stop $((Get-Date).ToString('o'))"
  & $xperf -merge $k "$outDir\w$i.etl" 2>&1 | Out-Null
  Remove-Item $k -ErrorAction SilentlyContinue
  $i++
}
while (((Get-Date) - $start).TotalSeconds -lt $Seconds) {
  if (-not (Get-Process -Id $run.Id -ErrorAction SilentlyContinue)) { break }
  Start-Sleep -Seconds 5
}
$proc = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
if ($proc -and $proc.Path -eq $run.Executable) { Stop-Process -Id $run.Id -Force; Start-Sleep -Seconds 3 }
Start-Sleep -Seconds 2
Copy-Item $run.Log "$outDir\game.log"
"$Name done: $($run.Log)"
