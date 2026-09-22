# Repeatable renderer A/B: runs a baseline and a candidate executable on the
# same input script, interleaved (A B A B ...), then asks
# tools/renderer-runtime-gate.py whether each candidate run is at least as
# fast as the baseline run it is paired with.
#
#   powershell -File tools/run-renderer-ab.ps1 `
#     -Baseline out/build/win-amd64-release/edf2027-baseline-6e9c94b.exe `
#     -Candidate out/build/win-amd64-release/edf2027.exe -Repeat 2 -Name static-groups
#
# -Baseline and -Candidate take an executable path, a build configuration
# name (win-amd64-release -> out/build/win-amd64-release/edf2027.exe) or an
# executable name inside out/build/win-amd64-release (edf2027-baseline-6e9c94b).
# -BaselineArgs/-CandidateArgs go to start-native-binding-validation.ps1 as
# -ExtraArgs, so each entry replaces the launcher's option of the same name.
# -GateArgs goes verbatim to the gate (for example '--min-native-groups','40').
# With -HookTimings, both runs log "Native hook timing" lines and the gate adds
# a "phases" section (ms per frame and per call for engine.render_helper and
# the render.* phases). Phase gates then work too, for example
#   -HookTimings -GateArgs '--max-phase','render.model=2.5','--expect-drop','render.queued'
# --max-phase PHASE=MS (repeatable) fails when the candidate's ms per frame is
# above MS; --expect-drop PHASE (repeatable) fails unless the candidate's cost is
# below the baseline's. Either one fails if a log has no hook timing lines.
#
# The gate measures from 10 s to 150 s after mission entry (the first scene
# draw), not after launch. Loading plus the scripted menu confirmations take
# up to ~160 s, so -Seconds must cover loading + 150 s; the 360 s default does.
#
# Only one game may run at a time: the runner refuses to start while any
# edf2027* process exists, and stops only the PID it launched after checking
# that the PID's image is the executable it asked for. A run that dies early is
# still gated; the gate then reports "no FPS samples" and the pair fails.
#
# Output: out/renderer-ab/<timestamp>-<Name>/summary.json with the arguments,
# run logs, each pair's gate JSON and the verdict. One line is printed per pair
# plus the median over pairs. Exit code is 1 when any pair fails.
param(
  [Parameter(Mandatory=$true)][string]$Baseline,
  [Parameter(Mandatory=$true)][string]$Candidate,
  [string[]]$BaselineArgs = @(),
  [string[]]$CandidateArgs = @(),
  [string]$Script = 'tools/native-flicker-input.txt',
  [ValidateRange(1,7200)][int]$Seconds = 360,
  [ValidateRange(1,50)][int]$Repeat = 1,
  [switch]$HookTimings,
  [string]$Name = 'ab',
  [string[]]$GateArgs = @(),
  [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$launcher = Join-Path $PSScriptRoot 'start-native-binding-validation.ps1'
$gate = Join-Path $PSScriptRoot 'renderer-runtime-gate.py'

function Resolve-GameExecutable([string]$Spec) {
  $candidates = @()
  if ([IO.Path]::IsPathRooted($Spec)) { $candidates += $Spec } else {
    $candidates += Join-Path $workspace $Spec
    $candidates += Join-Path $workspace ('out/build/' + $Spec + '/edf2027.exe')
    $leaf = if ($Spec.EndsWith('.exe')) { $Spec } else { $Spec + '.exe' }
    $candidates += Join-Path $workspace ('out/build/win-amd64-release/' + $leaf)
  }
  foreach ($path in $candidates) {
    if (Test-Path -LiteralPath $path -PathType Leaf) { return (Resolve-Path -LiteralPath $path).Path }
  }
  throw "No executable for '$Spec'. Tried: $($candidates -join ', ')"
}

function Get-Median([double[]]$Values) {
  if (-not $Values -or $Values.Count -eq 0) { return $null }
  $sorted = @($Values | Sort-Object)
  $mid = [int][Math]::Floor($sorted.Count / 2)
  if ($sorted.Count % 2) { return $sorted[$mid] }
  return ($sorted[$mid - 1] + $sorted[$mid]) / 2
}

function Assert-NoGame {
  $running = @(Get-Process edf2027* -ErrorAction SilentlyContinue)
  if ($running.Count) {
    throw ("An EDF game is already running (PID " + (($running | ForEach-Object Id) -join ', ') +
      "). Leave it alone and retry after it exits.")
  }
}

function Invoke-Gate([string]$CandidateLog, [string]$BaselineLog) {
  $argv = @($gate, $CandidateLog, '--baseline', $BaselineLog) + $GateArgs
  $info = New-Object System.Diagnostics.ProcessStartInfo
  $info.FileName = $Python
  $info.Arguments = ($argv | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' '
  $info.WorkingDirectory = $workspace
  $info.UseShellExecute = $false
  $info.RedirectStandardOutput = $true
  $info.RedirectStandardError = $true
  $process = [System.Diagnostics.Process]::Start($info)
  $stderrTask = $process.StandardError.ReadToEndAsync()
  $stdout = $process.StandardOutput.ReadToEnd()
  $process.WaitForExit()
  $report = $null
  try { $report = $stdout | ConvertFrom-Json } catch { $report = $null }
  [pscustomobject]@{ ExitCode=$process.ExitCode; Report=$report; Stdout=$stdout; Stderr=$stderrTask.Result }
}

function Invoke-Run([string]$Role, [int]$Index, [string]$Executable, [string[]]$Extra) {
  Assert-NoGame
  $launch = @{ Executable=$Executable; InputScript=$Script; ExtraArgs=$Extra }
  if ($HookTimings) { $launch.HookTimings = $true }
  $started = Get-Date
  $run = & $launcher @launch | Where-Object { $_ -and $_.PSObject.Properties['Id'] } | Select-Object -Last 1
  if (-not $run) { throw "$Role run $Index did not report a process" }
  Write-Host ("[{0} {1}] PID {2} {3}" -f $Role, $Index, $run.Id, $run.Log)
  $deadline = $started.AddSeconds($Seconds)
  $exitedEarly = $false
  while ((Get-Date) -lt $deadline) {
    $process = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
    if (-not $process -or $process.HasExited) { $exitedEarly = $true; break }
    Start-Sleep -Seconds 2
  }
  $elapsed = ((Get-Date) - $started).TotalSeconds
  $stopped = $false
  $process = Get-Process -Id $run.Id -ErrorAction SilentlyContinue
  if ($process -and -not $process.HasExited) {
    # A PID can be reused; stop it only while it is still the game we launched.
    if (-not $process.Path -or -not [string]::Equals($process.Path, $run.Executable, [StringComparison]::OrdinalIgnoreCase)) {
      throw "PID $($run.Id) is now '$($process.Path)', not '$($run.Executable)'; refusing to stop it"
    }
    Stop-Process -Id $run.Id -Force
    $process.WaitForExit(30000) | Out-Null
    $stopped = $true
  } else {
    $exitedEarly = $true
  }
  if ($exitedEarly) { Write-Warning "$Role run $Index exited after $([int]$elapsed) s, before the $Seconds s budget" }
  [pscustomobject]@{
    role=$Role; index=$Index; executable=$run.Executable; args=$Extra; pid=$run.Id
    log=$run.Log; run_directory=$run.RunDirectory; log_exists=(Test-Path -LiteralPath $run.Log)
    started=$started.ToString('o'); seconds=[Math]::Round($elapsed, 1)
    exited_early=$exitedEarly; stopped_by_runner=$stopped
  }
}

$baseExe = Resolve-GameExecutable $Baseline
$candExe = Resolve-GameExecutable $Candidate
if (-not [IO.Path]::IsPathRooted($Script)) { $Script = Join-Path $workspace $Script }
$Script = (Resolve-Path -LiteralPath $Script).Path
Assert-NoGame

$safeName = ($Name -replace '[^\w.-]', '_')
$outDir = Join-Path $workspace ('out/renderer-ab/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $safeName)
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$summaryPath = Join-Path $outDir 'summary.json'

$runs = @()
$pairs = @()
$aborted = $null
try {
  for ($i = 1; $i -le $Repeat; $i++) {
    $runs += Invoke-Run 'baseline' $i $baseExe $BaselineArgs
    $runs += Invoke-Run 'candidate' $i $candExe $CandidateArgs
  }
} catch {
  # Keep whatever already ran: those logs are still worth gating and saving.
  $aborted = $_.Exception.Message
  Write-Warning "A/B stopped early: $aborted"
}

for ($i = 1; $i -le $Repeat; $i++) {
  $base = $runs | Where-Object { $_.role -eq 'baseline' -and $_.index -eq $i }
  $cand = $runs | Where-Object { $_.role -eq 'candidate' -and $_.index -eq $i }
  if (-not $base -or -not $cand) { continue }
  $failures = @()
  $report = $null
  $gateResult = $null
  if (-not $base.log_exists -or -not $cand.log_exists) {
    $failures += 'no FPS samples: a run wrote no game.log'
  } else {
    $gateResult = Invoke-Gate $cand.log $base.log
    $report = $gateResult.Report
    if (-not $report) {
      $failures += "gate produced no JSON (exit $($gateResult.ExitCode)): $($gateResult.Stderr.Trim())"
    } else {
      $failures += @($report.failures)
    }
  }
  $passed = [bool]($report -and $report.passed -and $gateResult.ExitCode -eq 0 -and -not $failures.Count)
  $baseFps = if ($report) { $report.baseline.fps_median } else { $null }
  $candFps = if ($report) { $report.candidate.fps_median } else { $null }
  $ratio = if ($baseFps -and $candFps) { [Math]::Round($candFps / $baseFps, 4) } else { $null }
  $pairs += [pscustomobject]@{
    index=$i; baseline_log=$base.log; candidate_log=$cand.log
    baseline_exited_early=$base.exited_early; candidate_exited_early=$cand.exited_early
    baseline_fps_median=$baseFps; candidate_fps_median=$candFps; ratio=$ratio
    gate=$report; gate_exit_code=$(if ($gateResult) { $gateResult.ExitCode } else { $null })
    gate_stderr=$(if ($gateResult -and $gateResult.Stderr) { $gateResult.Stderr } else { $null })
    passed=$passed; failures=@($failures)
  }
  $fmt = { param($v) if ($null -eq $v) { 'n/a' } else { '{0:N1}' -f [double]$v } }
  $verdict = if ($passed) { 'PASS' } else { 'FAIL' }
  $line = "pair {0}: {1} baseline {2} fps, candidate {3} fps" -f $i, $verdict, (& $fmt $baseFps), (& $fmt $candFps)
  if ($ratio) { $line += (' (x{0:N3})' -f $ratio) }
  if ($failures.Count) { $line += ' - ' + ($failures -join '; ') }
  Write-Host $line
}

$medianBase = Get-Median @($pairs | Where-Object { $null -ne $_.baseline_fps_median } | ForEach-Object { [double]$_.baseline_fps_median })
$medianCand = Get-Median @($pairs | Where-Object { $null -ne $_.candidate_fps_median } | ForEach-Object { [double]$_.candidate_fps_median })
$medianRatio = Get-Median @($pairs | Where-Object { $null -ne $_.ratio } | ForEach-Object { [double]$_.ratio })
$allPassed = [bool]($pairs.Count -eq $Repeat -and -not $aborted -and -not ($pairs | Where-Object { -not $_.passed }))

$summary = [pscustomobject]@{
  name=$Name; created=(Get-Date).ToString('o'); output_directory=$outDir
  args=[pscustomobject]@{
    baseline=$baseExe; candidate=$candExe; baseline_args=$BaselineArgs; candidate_args=$CandidateArgs
    script=$Script; seconds=$Seconds; repeat=$Repeat; hook_timings=$HookTimings.IsPresent; gate_args=$GateArgs
  }
  runs=$runs; pairs=$pairs; aborted=$aborted
  median=[pscustomobject]@{ baseline_fps=$medianBase; candidate_fps=$medianCand; ratio=$medianRatio }
  passed=$allPassed
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8

$fmtMedian = { param($v) if ($null -eq $v) { 'n/a' } else { '{0:N1}' -f [double]$v } }
$overall = "overall: {0} over {1}/{2} pairs, median baseline {3} fps, candidate {4} fps" -f `
  $(if ($allPassed) { 'PASS' } else { 'FAIL' }), $pairs.Count, $Repeat, (& $fmtMedian $medianBase), (& $fmtMedian $medianCand)
if ($null -ne $medianRatio) { $overall += (' (x{0:N3})' -f $medianRatio) }
Write-Host $overall
Write-Host "summary: $summaryPath"
if (-not $allPassed) { exit 1 }
exit 0
