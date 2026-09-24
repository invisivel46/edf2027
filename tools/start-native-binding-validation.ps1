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
  [ValidateRange(640,8192)][int]$RenderWidth = 1280,
  [ValidateRange(480,8192)][int]$RenderHeight = 720,
  [switch]$NativeRenderSize,
  [ValidateSet(0,1,2,4)][int]$Msaa = 0,
  [ValidateRange(-1,5)][int]$AnisotropicFiltering = -1,
  # Write diagnostic captures as <run directory>/cap.output.<F>.bmp (the prefix
  # is only known here, once the run directory exists). Pair it with
  # --edf_native_output_capture_start_frame/interval/limit in -ExtraArgs;
  # without them the capture limit stays 0 and nothing is written.
  [switch]$SceneCapture,
  # A user-data tree to copy into this run's fresh user directory before launch,
  # such as a save from tools/make-edf-save.py that unlocks a later mission.
  # Without it every run starts from an empty profile (Mission 1 only).
  [string]$SaveSeed = '',
  # The renderer preset, passed as --edf_native_renderer. Since 361f80b the
  # executable's own default is native (the full-frame renderer), so this
  # launcher passes the preset explicitly and defaults to off, the guest
  # renderer that every run it documents was made against: a baseline stays a
  # guest baseline, and individual --edf_native_* flags in -ExtraArgs add to the
  # guest renderer as they did before the flip. Pass -Renderer native (or
  # --edf_native_renderer=... in -ExtraArgs, which replaces this) for the
  # native renderer, and -Renderer default to pass nothing. An executable
  # built before the preset existed (42823d7) has no such option and only the
  # guest route by default, so off is omitted for it and any other preset is
  # refused.
  [ValidateSet('off','world','full','native','default')][string]$Renderer = 'off',
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
# Whether the executable knows --edf_native_renderer: its cvar name is in the
# image as a NUL-terminated string (ISO-8859-1 maps each byte to one char).
function Test-ExecutableOption([string]$Path, [string]$Name) {
  $image = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($Path))
  return $image.IndexOf($Name + [char]0, [StringComparison]::Ordinal) -ge 0
}
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
if ($SaveSeed) {
  if (-not [IO.Path]::IsPathRooted($SaveSeed)) { $SaveSeed = Join-Path $workspace $SaveSeed }
  $seed = (Resolve-Path -LiteralPath $SaveSeed).Path
  $userRoot = Join-Path $runRoot 'user'
  New-Item -ItemType Directory -Path $userRoot | Out-Null
  Copy-Item -Path (Join-Path $seed '*') -Destination $userRoot -Recurse
}
# Do not pass an empty --edf_native_scene_capture= : the current command-line
# parser consumes the following flag as its string value. A fresh user directory
# has no saved capture override, so omit the option and retain its empty
# default unless -SceneCapture asks for a non-empty, quoted prefix.
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
# Automated runs start muted so they don't play over whatever the user is doing;
# -ManualInput keeps the sound, and -ExtraArgs '--audio_mute=false' overrides it.
if (-not $ManualInput) { $arguments += '--audio_mute=true' }
$rendererOverride = @($ExtraArgs | Where-Object { (($_ -split '=', 2)[0]) -eq '--edf_native_renderer' }).Count -gt 0
if ($Renderer -ne 'default' -and -not $rendererOverride) {
  if (Test-ExecutableOption $candidate 'edf_native_renderer') {
    $arguments += '--edf_native_renderer=' + $Renderer
  } elseif ($Renderer -ne 'off') {
    throw "$candidate predates --edf_native_renderer (42823d7); it cannot run -Renderer $Renderer"
  }
}
if ($SceneCapture) {
  $arguments += '--edf_native_scene_capture="' + (Join-Path $runRoot 'cap') + '"'
}
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
  [pscustomobject]@{ Id=$game.Id; Executable=$candidate; Log=$log; RunDirectory=$runRoot; SaveSeed=$SaveSeed
    Renderer=$(if ($rendererOverride) { 'extra-args' } else { $Renderer }); Arguments=$arguments
    CapturePrefix=$(if ($SceneCapture) { Join-Path $runRoot 'cap' } else { $null }) }
} finally {
  $env:EDF_INPUT_SCRIPT = $previousScript
  $env:EDF_INPUT_SCRIPT_RELOAD = $previousReload
}
# Automated runs are hidden; -ManualInput starts a visible game with the normal
# input driver. The caller owns this exact process and must validate its identity
# before stopping it. Both modes isolate user data and report the log path.
