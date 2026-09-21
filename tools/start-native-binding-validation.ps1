param(
  [string]$Executable = 'out/build/win-amd64-release/edf2027.exe',
  [string]$GameData = 'D:/roms2/edf3-translation-project/work/x360',
  [string]$InputScript = 'tools/native-flicker-input.txt',
  [switch]$ManualInput,
  [switch]$PixelCenters,
  [switch]$LoadingTrace,
  [switch]$LoadTimings,
  [switch]$HookTimings,
  [switch]$WorkerCallbackAudit,
  [switch]$RetirementAudit,
  [switch]$RenderStateAudit,
  [switch]$OwnedRenderState,
  [switch]$UntiledScene,
  [ValidateRange(640,4095)][int]$RenderWidth = 1280,
  [ValidateRange(480,4095)][int]$RenderHeight = 720,
  [switch]$NativeRenderSize,
  [ValidateSet(0,1,2,4)][int]$Msaa = 0,
  [ValidateRange(-1,5)][int]$AnisotropicFiltering = -1,
  # Anything else this run needs, passed through verbatim. The options above
  # are the ones every run chooses between; this is for a flag that exists to
  # answer one question, such as a port's A/B control.
  [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
if (Get-Process edf2027* -ErrorAction SilentlyContinue) {
  throw 'An EDF game is already running. Leave that process alone and retry after it exits.'
}
if (-not [IO.Path]::IsPathRooted($Executable)) { $Executable = Join-Path $workspace $Executable }
$candidate = (Resolve-Path -LiteralPath $Executable).Path
$assets = (Resolve-Path -LiteralPath $GameData).Path
$scriptPath = $null
if (-not $ManualInput) {
  if (-not [IO.Path]::IsPathRooted($InputScript)) { $InputScript = Join-Path $workspace $InputScript }
  $scriptPath = (Resolve-Path -LiteralPath $InputScript).Path
}
$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$runRoot = Join-Path $workspace ('out/native-bridge-run/binding-validation-' + $stamp)
New-Item -ItemType Directory -Path $runRoot | Out-Null
$log = Join-Path $runRoot 'game.log'
# Do not pass --edf_native_scene_capture= : the current command-line parser
# consumes the following flag as its string value. A fresh user directory has
# no saved capture override, so omit the option and retain its empty default.
$arguments = @(
  ('--game_data_root="' + $assets + '"'),
  ('--user_data_root="' + (Join-Path $runRoot 'user') + '"'),
  ('--cache_root="' + (Join-Path $workspace 'out/native-bridge-run/cache') + '"'),
  ('--log_file="' + $log + '"'),
  '--fullscreen=false',
  ('--video_mode_width=' + $RenderWidth),
  ('--video_mode_height=' + $RenderHeight),
  ('--edf_native_anisotropic_filtering=' + $AnisotropicFiltering),
  ('--edf_native_msaa=' + $Msaa),
  '--edf_native_host_timings=true',
  ('--edf_native_hook_timings=' + $HookTimings.IsPresent.ToString().ToLowerInvariant()),
  ('--edf_native_worker_callback_audit=' + $WorkerCallbackAudit.IsPresent.ToString().ToLowerInvariant()),
  ('--edf_native_retirement_audit=' + $RetirementAudit.IsPresent.ToString().ToLowerInvariant()),
  '--edf_native_mesh_watch_audit=false',
  ('--edf_native_render_state_audit=' + $RenderStateAudit.IsPresent.ToString().ToLowerInvariant()),
  '--edf_native_output_capture_limit=0',
  ('--edf_native_loading_trace=' + $LoadingTrace.IsPresent.ToString().ToLowerInvariant()),
  ('--edf_native_load_timings=' + $LoadTimings.IsPresent.ToString().ToLowerInvariant())
)
# Omission tests the guest-driven pixel-centre default; pass -PixelCenters:$false
# to restore the unshifted post viewport for regression diagnosis.
if ($PSBoundParameters.ContainsKey('PixelCenters')) {
  $arguments += '--edf_native_pixel_centers=' + $PixelCenters.IsPresent.ToString().ToLowerInvariant()
}
# Omission tests the executable's actual default with this fresh user directory.
# Explicit -OwnedRenderState or -OwnedRenderState:$false tests either override.
if ($PSBoundParameters.ContainsKey('OwnedRenderState')) {
  $arguments += '--edf_native_owned_render_state=' + $OwnedRenderState.IsPresent.ToString().ToLowerInvariant()
}
# Compatibility probe: false must be migrated, never enable Xbox tile replay.
if ($PSBoundParameters.ContainsKey('UntiledScene')) {
  $arguments += '--edf_native_untiled_scene=' + $UntiledScene.IsPresent.ToString().ToLowerInvariant()
}
# Passing a flag twice does not mean "the last one wins": the parser mis-reads
# the options that follow the duplicate, and the run silently uses a default
# for something the caller asked for by name. So an -ExtraArgs entry replaces
# the base entry for the same option rather than being appended after it.
foreach ($extra in $ExtraArgs) {
  $name = ($extra -split '=', 2)[0]
  $arguments = @($arguments | Where-Object { (($_ -split '=', 2)[0]) -ne $name })
  $arguments += $extra
}
$arguments += if ($NativeRenderSize) {
  @(('--edf_native_render_width=' + $RenderWidth), ('--edf_native_render_height=' + $RenderHeight))
} else { @() }
$previousScript = $env:EDF_INPUT_SCRIPT
$previousReload = $env:EDF_INPUT_SCRIPT_RELOAD
try {
  $env:EDF_INPUT_SCRIPT = $scriptPath
  $env:EDF_INPUT_SCRIPT_RELOAD = if ($ManualInput) { $null } else { '1' }
  # Manual input explicitly requests a visible interactive game. Automated
  # diagnostics keep their existing hidden launch behavior.
  $windowStyle = if ($ManualInput) { 'Normal' } else { 'Hidden' }
  $game = Start-Process -FilePath $candidate -WorkingDirectory (Split-Path $candidate) -WindowStyle $windowStyle -ArgumentList $arguments -PassThru
  [pscustomobject]@{ Id=$game.Id; Executable=$candidate; Log=$log; RunDirectory=$runRoot }
} finally {
  $env:EDF_INPUT_SCRIPT = $previousScript
  $env:EDF_INPUT_SCRIPT_RELOAD = $previousReload
}
# Automated runs are hidden; -ManualInput starts a visible game with the normal
# input driver. The caller owns this exact process and must validate its identity
# before stopping it. Both modes isolate user data and report the log path.
