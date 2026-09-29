param([string]$Name, [string[]]$Extra = @(), [int]$Seconds = 330, [long]$Affinity = 0, [string]$Exe = '',
      [string]$Scenario = 'benchmark', [int]$W = 1280, [int]$H = 720)
$ErrorActionPreference = 'Stop'
$ws = 'D:\roms2\edf2027\.claude\worktrees\m1-intro-perf'
$s = Join-Path $env:TEMP 'edf2027-scratch'
if (-not $Exe) { $Exe = "$ws\out\build\win-amd64-release\edf2027.exe" }
# wait for any other agent's game to exit
while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }
$outDir = "$s\runs\$Name"
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
New-Item -ItemType Directory -Force $outDir | Out-Null
$trace = "$outDir\trace.csv" -replace '\\','/'
$table = Get-Content -Raw "$ws\tools\renderer-scenarios.json" | ConvertFrom-Json
$entry = $table.scenarios.$Scenario
$args2 = @('--edf_native_renderer=native','--edf_native_unlock_framerate=true','--edf_fps_cap=0','--edf_native_vsync=false',
  '--edf_native_frame_times=true','--edf_native_gpu_timings=true','--edf_frametime_log=true',
  ('--edf_native_frame_trace=' + $trace))
foreach ($e in $Extra) { $n = ($e -split '=',2)[0]; $args2 = @($args2 | Where-Object { (($_ -split '=',2)[0]) -ne $n }); $args2 += $e }
$launch = @{ Executable = $Exe; InputScript = $entry.input; ExtraArgs = $args2; RenderWidth = $W; RenderHeight = $H }
Push-Location $ws
$run = $null
while (-not $run) { while (Get-Process edf2027* -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }; try { $run = & "$ws\tools\start-native-binding-validation.ps1" @launch } catch { $run = $null; Start-Sleep -Seconds 2 } }
Pop-Location
$p = Get-Process -Id $run.Id
if ($Affinity -ne 0) { $p.ProcessorAffinity = [IntPtr]$Affinity }
$sampler = Start-Process powershell -ArgumentList '-NoProfile','-File',"$s\threadsample.ps1",'-ProcessId',$run.Id,'-Out',"$outDir\threads.csv",'-Seconds',$Seconds -PassThru -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  if (-not (Get-Process -Id $run.Id -ErrorAction SilentlyContinue)) { break }
  Start-Sleep -Seconds 5
}
$proc = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
if ($proc -and $proc.Path -eq $run.Executable) { Stop-Process -Id $run.Id -Force; Start-Sleep -Seconds 3 }
Start-Sleep -Seconds 2
Copy-Item $run.Log "$outDir\game.log"
@{ name=$Name; log=$run.Log; pid=$run.Id; args=$run.Arguments; affinity=$Affinity } | ConvertTo-Json | Set-Content "$outDir\run.json"
"$Name done: $($run.Log)"
