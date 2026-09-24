# Train the PGO profile the release build uses (pgo/edf2027.profdata), unattended.
#
#   powershell -ExecutionPolicy Bypass -File tools/pgo-train.ps1
#
# 1. Configures and builds the instrumented game (preset win-amd64-pgo-train:
#    the release flags, x86-64-v3, plus -fprofile-generate) in
#    out/build/win-amd64-pgo-train, at low priority with -BuildJobs jobs, and
#    records the build's wall time and the compilers' peak memory.
# 2. Runs each training scenario (tools/renderer-scenarios.json, through
#    tools/run-renderer-scenario.ps1 with its seeded save and input script)
#    with the native renderer unlocked and uncapped (--edf_fps_cap=0, no vsync).
#    Each run writes its own raw profile (EDF_PGO_RAW_FILE, rewritten every
#    EDF_PGO_DUMP_SECONDS=10); the title, menus and loading screens come with
#    every run.
# 3. Converts each raw profile with llvm-profdata and merges them, weighting each
#    scenario by -Share relative to the work it executed (its total block count),
#    so a scenario's weight in the profile does not depend on how long or how
#    fast it ran. Writes -Output and pgo/training.json (what was trained, when,
#    from which commit, with which weights).
#
# Waits for any running game to exit before each launch: one game at a time.
# -SkipBuild reuses the instrumented build; -SkipRuns merges the raw profiles of
# an earlier run in -WorkDir. -DryRun prints the plan. docs/pgo.md has the why.
param(
  [string[]]$Scenarios = @('benchmark', 'ufo-swarm', 'cave', 'vehicle'),
  # Relative share of each scenario in the merged profile. Mission 1 counts
  # double: it is the benchmark and the route most players see first.
  [hashtable]$Share = @{ 'benchmark' = 2; 'ufo-swarm' = 1; 'cave' = 1; 'vehicle' = 1 },
  # Seconds per scenario; 0 uses the scenario's 'unlocked' length (600-720 s).
  [int]$Seconds = 0,
  [int]$BuildJobs = $(if ($env:BUILD_JOBS) { [int]$env:BUILD_JOBS } else { 2 }),
  [string]$Preset = 'win-amd64-pgo-train',
  [string]$Output = 'pgo/edf2027.profdata',
  [string]$WorkDir = 'out/pgo-train',
  [string]$RexglueSdk = $(if ($env:REXGLUE_SDK) { $env:REXGLUE_SDK } else { 'D:\roms2\edf3-translation-project\rexglue\sdk_ffx\win-amd64' }),
  [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community',
  [switch]$SkipBuild,
  [switch]$SkipRuns,
  [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
function Resolve-InWorkspace([string]$Path) {
  if ([IO.Path]::IsPathRooted($Path)) { return $Path }
  return (Join-Path $workspace $Path)
}
$WorkDir = Resolve-InWorkspace $WorkDir
$Output = Resolve-InWorkspace $Output
$buildDir = Join-Path $workspace ('out/build/' + $Preset)
$exe = Join-Path $buildDir 'edf2027.exe'
$table = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'renderer-scenarios.json') | ConvertFrom-Json
foreach ($s in $Scenarios) {
  if ($null -eq $table.scenarios.$s) { throw "Unknown scenario '$s'" }
  if (-not $Share.ContainsKey($s)) { $Share[$s] = 1 }
}

# llvm-profdata from the same LLVM as the compiler: VS's bundled clang, which
# VsDevCmd puts on PATH for the build.
$vsDevCmd = Join-Path $VsRoot 'Common7\Tools\VsDevCmd.bat'
$profdata = Join-Path $VsRoot 'VC\Tools\Llvm\x64\bin\llvm-profdata.exe'
if (-not (Test-Path -LiteralPath $profdata)) {
  $found = Get-Command llvm-profdata -ErrorAction SilentlyContinue
  if (-not $found) { throw "llvm-profdata not found at $profdata or on PATH" }
  $profdata = $found.Source
}

$plan = [ordered]@{ preset = $Preset; build_dir = $buildDir; jobs = $BuildJobs; scenarios = $Scenarios
  share = $Share; seconds = $Seconds; output = $Output; work_dir = $WorkDir; llvm_profdata = $profdata
  skip_build = [bool]$SkipBuild; skip_runs = [bool]$SkipRuns }
if ($DryRun) { [pscustomobject]$plan | ConvertTo-Json -Depth 4; return }

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$logFile = Join-Path $WorkDir 'train.log'
function Say([string]$Text) {
  $line = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ' + $Text
  Write-Host $line
  try { Add-Content -Encoding utf8 -LiteralPath $logFile -Value $line } catch { Write-Warning "train.log: $_" }
}
$total = [Diagnostics.Stopwatch]::StartNew()
$summary = [ordered]@{ started = (Get-Date).ToString('s'); commit = (git -C $workspace rev-parse --short HEAD)
  dirty = [bool](git -C $workspace status --porcelain --untracked-files=no); preset = $Preset
  llvm_profdata = (& $profdata --version | Select-String 'LLVM version' | ForEach-Object { $_.Line.Trim() }) }

# ---- 1. Instrumented build -------------------------------------------------
if (-not $SkipBuild -and -not $SkipRuns) {
  if (-not (Test-Path -LiteralPath $vsDevCmd)) { throw "VsDevCmd not found: $vsDevCmd" }
  $buildCmd = Join-Path $WorkDir 'build.cmd'
  $buildLog = Join-Path $WorkDir 'build.log'
  @(
    '@echo off',
    ('call :main > "' + $buildLog + '" 2>&1'),
    'exit /b %errorlevel%',
    ':main',
    ('call "' + $vsDevCmd + '" -arch=amd64 -host_arch=amd64 >nul'),
    ('cd /d "' + $workspace + '"'),
    ('cmake --preset ' + $Preset + ' -DCMAKE_PREFIX_PATH="' + $RexglueSdk + '"'),
    'if errorlevel 1 exit /b 1',
    ('cmake --build --preset ' + $Preset + ' --target edf2027 --parallel ' + $BuildJobs),
    'exit /b %errorlevel%'
  ) | Set-Content -Encoding ascii -LiteralPath $buildCmd
  Say "building $Preset with $BuildJobs job(s) at low priority; log: $buildLog"
  $watch = [Diagnostics.Stopwatch]::StartNew()
  # start /low: the whole compiler tree inherits idle priority.
  $proc = Start-Process -FilePath $env:ComSpec -WindowStyle Hidden -PassThru `
    -ArgumentList ('/c start "pgo-train-build" /low /b /wait cmd /c "' + $buildCmd + '"')
  $null = $proc.Handle  # keeps ExitCode readable after the process exits
  [long]$peakTotal = 0; [long]$peakOne = 0
  while (-not $proc.HasExited) {
    # Only this build's process tree: other builds may be running beside it.
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, Name, WorkingSetSize -ErrorAction SilentlyContinue)
    $children = @{}
    foreach ($p in $all) { $children[[int]$p.ParentProcessId] += @($p) }
    $mine = @(); $queue = @([int]$proc.Id)
    while ($queue.Count) {
      $next = @()
      foreach ($id in $queue) { foreach ($c in @($children[$id])) { if ($c) { $mine += $c; $next += [int]$c.ProcessId } } }
      $queue = $next
    }
    $compilers = @($mine | Where-Object { $_.Name -match '^(clang|clang\+\+|clang-cl|lld-link|ld\.lld)\.exe$' })
    if ($compilers.Count) {
      [long]$sum = ($compilers | Measure-Object WorkingSetSize -Sum).Sum
      [long]$max = ($compilers | Measure-Object WorkingSetSize -Maximum).Maximum
      $peakTotal = [Math]::Max($peakTotal, $sum); $peakOne = [Math]::Max($peakOne, $max)
    }
    Start-Sleep -Seconds 2
  }
  $proc.WaitForExit()
  $watch.Stop()
  $summary.build = [ordered]@{ jobs = $BuildJobs; seconds = [int]$watch.Elapsed.TotalSeconds; exit = $proc.ExitCode
    peak_compiler_working_set_mb = [int]($peakTotal / 1MB); peak_single_process_mb = [int]($peakOne / 1MB) }
  Say ("build exit {0} in {1:n0} s; peak compiler working set {2} MB total, {3} MB in one process" -f `
    $proc.ExitCode, $watch.Elapsed.TotalSeconds, $summary.build.peak_compiler_working_set_mb, $summary.build.peak_single_process_mb)
  if ($proc.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
    throw "Instrumented build failed (clang out of memory? retry with -BuildJobs 1); see $buildLog"
  }
}
if (-not $SkipRuns -and -not (Test-Path -LiteralPath $exe)) { throw "No instrumented game at $exe (drop -SkipBuild)" }

# ---- 2. Training runs --------------------------------------------------------
$runs = @()
foreach ($s in $Scenarios) {
  $raw = Join-Path $WorkDir ($s + '.profraw')
  if (-not $SkipRuns) {
    foreach ($stale in @($raw, "$raw.tmp")) { if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force } }
    while (Get-Process edf2027* -ErrorAction SilentlyContinue) {
      Say 'waiting for a running game to exit'
      Start-Sleep -Seconds 30
    }
    $saved = @{ raw = $env:EDF_PGO_RAW_FILE; dump = $env:EDF_PGO_DUMP_SECONDS }
    $env:EDF_PGO_RAW_FILE = $raw
    $env:EDF_PGO_DUMP_SECONDS = '10'
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Say "training run: $s"
    try {
      $runArgs = @{ Scenario = $s; Variant = 'unlocked'; Executable = $exe; Tag = 'pgo-train'
        ExtraArgs = @('--edf_fps_cap=0') }
      if ($Seconds -gt 0) { $runArgs.Seconds = $Seconds }
      $result = & (Join-Path $PSScriptRoot 'run-renderer-scenario.ps1') @runArgs
    } finally {
      $env:EDF_PGO_RAW_FILE = $saved.raw
      $env:EDF_PGO_DUMP_SECONDS = $saved.dump
    }
    $watch.Stop()
    Say ("  {0}: {1:n0} s, exited early: {2}, log {3}" -f $s, $watch.Elapsed.TotalSeconds, $result.exited_early, $result.log)
    $runs += [ordered]@{ scenario = $s; seconds = [int]$watch.Elapsed.TotalSeconds; exited_early = $result.exited_early; run = (Split-Path (Split-Path $result.log) -Leaf) }
  }
}

# ---- 3. Convert, weight, merge ----------------------------------------------
$parts = @()
foreach ($s in $Scenarios) {
  $raw = Join-Path $WorkDir ($s + '.profraw')
  $indexed = Join-Path $WorkDir ($s + '.profdata')
  # The periodic write lands at .profraw; a clean exit's last write stays at
  # .profraw.tmp (src/pgo_profile_writer.cpp). Take the newest one that parses.
  $candidates = @(Get-Item -LiteralPath @($raw, "$raw.tmp") -ErrorAction SilentlyContinue |
    Where-Object Length -gt 0 | Sort-Object LastWriteTime -Descending)
  $ok = $false
  foreach ($c in $candidates) {
    & $profdata merge -o $indexed $c.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { $ok = $true; break }
    Say "  $($c.Name) does not parse; trying the next"
  }
  if (-not $ok) { throw "No usable raw profile for $s in $WorkDir" }
  $totalLine = & $profdata show $indexed | Select-String '^Total count: (\d+)'
  $count = [double]$totalLine.Matches[0].Groups[1].Value
  $parts += [pscustomobject]@{ scenario = $s; file = $indexed; total_count = $count; share = [double]$Share[$s] }
}
# weight_i proportional to share_i / total_i, the smallest weight 10, so each
# scenario contributes its share of the merged counts (within 5% rounding).
$ratios = $parts | ForEach-Object { $_.share / $_.total_count }
$minRatio = ($ratios | Measure-Object -Minimum).Minimum
foreach ($p in $parts) { $p | Add-Member weight ([int][Math]::Round(10 * ($p.share / $p.total_count) / $minRatio)) }
$mergeArgs = @('merge', '-o', $Output) + @($parts | ForEach-Object { '--weighted-input=' + $_.weight + ',' + $_.file })
New-Item -ItemType Directory -Force -Path (Split-Path $Output) | Out-Null
& $profdata @mergeArgs
if ($LASTEXITCODE -ne 0) { throw 'llvm-profdata merge failed' }
$merged = Get-Item -LiteralPath $Output
$functions = (& $profdata show $Output | Select-String '^Total functions: (\d+)').Matches[0].Groups[1].Value
$total.Stop()
$summary.finished = (Get-Date).ToString('s')
$summary.runs = $runs
$summary.parts = @($parts | ForEach-Object { [ordered]@{ scenario = $_.scenario; share = $_.share
  total_count = $_.total_count; weight = $_.weight } })
$summary.output = [ordered]@{ path = 'pgo/' + $merged.Name; bytes = $merged.Length; functions = [int]$functions }
$summary.total_seconds = [int]$total.Elapsed.TotalSeconds
if ($Output -eq (Resolve-InWorkspace 'pgo/edf2027.profdata')) {
  ([pscustomobject]$summary | ConvertTo-Json -Depth 5) | Set-Content -Encoding utf8 -LiteralPath (Join-Path $workspace 'pgo/training.json')
}
([pscustomobject]$summary | ConvertTo-Json -Depth 5) | Set-Content -Encoding utf8 -LiteralPath (Join-Path $WorkDir 'training.json')
Say ("merged {0}: {1:n1} MB, {2} functions, weights {3}; total {4:n0} s" -f $Output, ($merged.Length / 1MB), $functions,
  (($parts | ForEach-Object { $_.scenario + '=' + $_.weight }) -join ' '), $total.Elapsed.TotalSeconds)
Say 'next: build.cmd (win-amd64-release) rebuilds against the new profile; commit pgo/edf2027.profdata and pgo/training.json'
