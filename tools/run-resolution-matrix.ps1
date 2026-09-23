# Run the Mission 1 benchmark route (tools/native-benchmark-input.txt) on the native
# renderer at each size of a resolution matrix, one run at a time, and collect for
# each: the window capture at a fixed time, render-size output captures, frame times,
# and the render-size / allocation / error lines of its log.
#
#   tools/run-resolution-matrix.ps1 -Executable out/build/win-amd64-release/edf2027.exe
#   tools/run-resolution-matrix.ps1 -Only 2560x1080,1024x768 -HudSafeArea full
#   tools/run-resolution-matrix.ps1 -DryRun
#
# Each size renders at exactly that size (--edf_native_render_width/height) in a
# window of that size, so the field of view and the 2D canvas layout follow the
# size even where Windows keeps a window smaller than the desktop; the window capture
# then shows the frame fitted into the window the OS allowed. 1280x720 resolves to the
# engine's own size: the unmodified path, the baseline the others are compared with.
#
# Per run (out/resolution-matrix/<stamp>/<size>/): host.bmp (the window, at
# -CaptureMs), cap.output.<frame>.bmp (render size, from -CaptureFrame),
# frame-times.txt/.json (tools/frame-time-report.py), and a line in summary.jsonl.
# The runs are hidden and use a fresh profile each; nothing else may be running.
param(
  [string]$Executable = 'out/build/win-amd64-release/edf2027.exe',
  [string]$GameData = 'D:/roms2/edf3-translation-project/work/x360',
  # WIDTHxHEIGHT entries to run, in order; default is the full matrix.
  [string[]]$Only = @(),
  [ValidateSet('native', 'ultrawide', 'letterbox', 'stretch')][string]$Aspect = 'native',
  [ValidateSet('16:9', 'full')][string]$HudSafeArea = '16:9',
  # Seconds each run lasts before its process is stopped. The route reaches street
  # gameplay about 115 s after launch at full speed; slower sizes need longer.
  [int]$Seconds = 240,
  # When the window capture is taken, in milliseconds after the host starts.
  [int]$CaptureMs = 170000,
  # Indexed output frame of the first render-size capture (3D frames since the first
  # scene draw); a second follows -CaptureInterval frames later.
  [int]$CaptureFrame = 4000,
  [int]$CaptureInterval = 1000,
  # Unlocked (default): 240 FPS cap, no vsync, so frame times show the cost of the size.
  # -Locked keeps the game's 60 Hz with vsync, for a like-for-like visual pass.
  [switch]$Locked,
  [string[]]$ExtraArgs = @(),
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent

$matrix = @(
  @{ Name = '1280x720';  Width = 1280; Height = 720;  Note = '16:9 baseline (engine size, unmodified path)' },
  @{ Name = '1920x1080'; Width = 1920; Height = 1080; Note = '16:9' },
  @{ Name = '2560x1440'; Width = 2560; Height = 1440; Note = '16:9' },
  @{ Name = '3840x2160'; Width = 3840; Height = 2160; Note = '16:9 4K' },
  @{ Name = '2560x1080'; Width = 2560; Height = 1080; Note = '21:9 Hor+' },
  @{ Name = '3440x1440'; Width = 3440; Height = 1440; Note = '21:9 Hor+' },
  @{ Name = '5120x1440'; Width = 5120; Height = 1440; Note = '32:9 Hor+' },
  @{ Name = '1920x1200'; Width = 1920; Height = 1200; Note = '16:10 Vert+' },
  @{ Name = '1024x768';  Width = 1024; Height = 768;  Note = '4:3 Vert+' },
  @{ Name = '1366x768';  Width = 1366; Height = 768;  Note = 'odd width, ~16:9' }
)
if ($Only.Count) {
  $wanted = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  $unknown = @($wanted | Where-Object { $name = $_; -not ($matrix | Where-Object { $_.Name -eq $name }) })
  if ($unknown.Count) { throw "Unknown matrix entries: $($unknown -join ', '). Known: $(($matrix | ForEach-Object Name) -join ', ')" }
  $matrix = @($wanted | ForEach-Object { $name = $_; $matrix | Where-Object { $_.Name -eq $name } })
}

if (-not [IO.Path]::IsPathRooted($Executable)) { $Executable = Join-Path $workspace $Executable }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outRoot = Join-Path $workspace ('out/resolution-matrix/' + $stamp)
$python = (Get-Command python -ErrorAction SilentlyContinue).Source

# The 16:9 video mode the guest is told (core_logic.h GuestVideoMode); the render
# size itself is set separately.
function Get-GuestVideoMode([int]$Width, [int]$Height) {
  if ([int64]$Width * 9 -eq [int64]$Height * 16) { return @($Width, $Height) }
  $w = [math]::Floor($Height * 16 / 9)
  if ($w -lt 1280) { return @(1280, 720) }
  return @([int]$w, $Height)
}

$pacing = if ($Locked) {
  @('--edf_native_unlock_framerate=false', '--edf_native_vsync=true')
} else {
  @('--edf_native_unlock_framerate=true', '--edf_fps_cap=240', '--edf_native_vsync=false')
}

$plans = foreach ($entry in $matrix) {
  $runDir = Join-Path $outRoot $entry.Name
  $video = Get-GuestVideoMode $entry.Width $entry.Height
  $hostCapture = (Join-Path $runDir 'host.bmp') -replace '\\', '/'
  $gameArgs = @(
    '--edf_native_renderer=native',
    ('--window_width=' + $entry.Width), ('--window_height=' + $entry.Height),
    ('--edf_native_render_width=' + $entry.Width), ('--edf_native_render_height=' + $entry.Height),
    ('--edf_aspect=' + $Aspect), ('--edf_hud_safe_area=' + $HudSafeArea),
    ('--present_letterbox=' + $(if ($Aspect -eq 'stretch') { 'false' } else { 'true' })),
    '--edf_native_frame_times=true', '--edf_native_gpu_timings=true', '--edf_native_memory_log=true',
    ('--edf_native_host_capture="' + $hostCapture + '"'), ('--edf_native_host_capture_after_ms=' + $CaptureMs),
    '--edf_native_host_capture_require_image=true',
    ('--edf_native_output_capture_start_frame=' + $CaptureFrame),
    ('--edf_native_output_capture_interval=' + $CaptureInterval),
    '--edf_native_output_capture_limit=2'
  ) + $pacing + $ExtraArgs
  [pscustomobject]@{ Name = $entry.Name; Note = $entry.Note; Width = $entry.Width; Height = $entry.Height
    VideoMode = "$($video[0])x$($video[1])"; RunDirectory = $runDir; Arguments = $gameArgs }
}
if ($DryRun) { $plans | ConvertTo-Json -Depth 4; return }

if (Get-Process edf2027* -ErrorAction SilentlyContinue) {
  throw 'An EDF game is already running. Leave that process alone and retry after it exits.'
}
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
$summaryPath = Join-Path $outRoot 'summary.jsonl'
Write-Host "Resolution matrix: $($plans.Count) run(s), $Seconds s each, into $outRoot"

foreach ($plan in $plans) {
  New-Item -ItemType Directory -Force -Path $plan.RunDirectory | Out-Null
  $video = $plan.VideoMode -split 'x'
  Write-Host ("[{0}] {1} ({2}) video mode {3}" -f (Get-Date -Format 'HH:mm:ss'), $plan.Name, $plan.Note, $plan.VideoMode)
  $launch = @{
    Executable = $Executable; GameData = $GameData; InputScript = 'tools/native-benchmark-input.txt'
    RenderWidth = [int]$video[0]; RenderHeight = [int]$video[1]; SceneCapture = $true
    Renderer = 'native'; ExtraArgs = $plan.Arguments
  }
  $run = & (Join-Path $PSScriptRoot 'start-native-binding-validation.ps1') @launch
  $exitedEarly = $false
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Id $run.Id -ErrorAction SilentlyContinue)) { $exitedEarly = $true; break }
    Start-Sleep -Seconds 5
  }
  $proc = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
  if ($proc -and $proc.Path -eq $run.Executable) { Stop-Process -Id $run.Id -Force; Start-Sleep -Seconds 3 }

  # Gather the evidence next to the run's own directory.
  $log = $run.Log
  Copy-Item -LiteralPath $log -Destination (Join-Path $plan.RunDirectory 'game.log') -ErrorAction SilentlyContinue
  $outputs = @(Get-ChildItem -LiteralPath $run.RunDirectory -Filter 'cap.output.*.bmp' -ErrorAction SilentlyContinue)
  foreach ($bmp in $outputs) { Copy-Item -LiteralPath $bmp.FullName -Destination $plan.RunDirectory }
  $lines = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log } else { @() }
  $pick = { param($pattern) @($lines | Select-String -Pattern $pattern | Select-Object -First 3 | ForEach-Object { $_.Line.Trim() }) }
  $errors = @($lines | Select-String -Pattern '\[error\]').Count
  $frameTimes = $null
  if ($python -and (Test-Path -LiteralPath $log)) {
    $report = & $python (Join-Path $PSScriptRoot 'frame-time-report.py') $log 2>&1
    $report | Set-Content -Encoding utf8 -LiteralPath (Join-Path $plan.RunDirectory 'frame-times.txt')
    $json = & $python (Join-Path $PSScriptRoot 'frame-time-report.py') $log --json 2>$null
    if ($LASTEXITCODE -eq 0 -and $json) {
      $json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $plan.RunDirectory 'frame-times.json')
      try { $frameTimes = ($json -join "`n") | ConvertFrom-Json } catch { $frameTimes = $null }
    }
  }
  $gameplay = if ($frameTimes -and $frameTimes.phases -and $frameTimes.phases.gameplay) { $frameTimes.phases.gameplay } else { $null }
  $result = [ordered]@{
    size = $plan.Name; note = $plan.Note; video_mode = $plan.VideoMode; pid = $run.Id; exited_early = $exitedEarly
    run_directory = $run.RunDirectory; evidence = $plan.RunDirectory
    host_capture = $(if (Test-Path -LiteralPath (Join-Path $plan.RunDirectory 'host.bmp')) { Join-Path $plan.RunDirectory 'host.bmp' } else { $null })
    output_captures = @($outputs | ForEach-Object Name)
    render_size = & $pick 'Native render size:'
    render_selected = & $pick 'Native render resolution selected'
    scene_allocated = & $pick 'Native full scene allocated'
    view = & $pick 'Native full frame view:'
    viewport_fallback = @($lines | Select-String -Pattern 'viewport empty').Count
    movie_framing = & $pick 'Native movie framing'
    errors = $errors
    first_errors = & $pick '\[error\]'
    gameplay_frame_times = $gameplay
  }
  ([pscustomobject]$result | ConvertTo-Json -Compress -Depth 6) | Add-Content -Encoding utf8 -LiteralPath $summaryPath
  Write-Host ("    render {0}; errors {1}; host capture {2}; output captures {3}{4}" -f
    $(if ($result.render_size.Count) { ($result.render_size[0] -replace '^.*Native render size: ', '') } else { '(no render size line)' }),
    $errors, [bool]$result.host_capture, $outputs.Count, $(if ($exitedEarly) { '; EXITED EARLY' } else { '' }))
}
Write-Host "Summary: $summaryPath"
