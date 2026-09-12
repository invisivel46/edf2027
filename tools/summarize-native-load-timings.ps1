param(
  [Parameter(Mandatory=$true)][string]$RunDirectory,
  [datetime]$Since = [datetime]::MinValue,
  [datetime]$Until = [datetime]::MaxValue
)
$ErrorActionPreference = 'Stop'
if ($Since -gt $Until) { throw 'Since must not be later than Until.' }
$root = (Resolve-Path -LiteralPath $RunDirectory).Path
$culture = [Globalization.CultureInfo]::InvariantCulture
$records = foreach ($file in Get-ChildItem -LiteralPath $root -File -Filter 'game*.log') {
  foreach ($line in Get-Content -LiteralPath $file.FullName) {
    if ($line -notmatch '^\[(?<time>[^\]]+)\].*Native hook timing: phase=(?<phase>load\.[^ ]+) calls=(?<calls>\d+) total_ms=(?<total>[0-9.eE+-]+) max_ms=(?<max>[0-9.eE+-]+)') { continue }
    $stamp = [datetime]::ParseExact($Matches.time,'yyyy-MM-dd HH:mm:ss.fff',$culture)
    if ($stamp -lt $Since -or $stamp -gt $Until) { continue }
    [pscustomobject]@{
      Phase=$Matches.phase
      Calls=[long]$Matches.calls
      TotalMs=[double]::Parse($Matches.total,$culture)
      MaxMs=[double]::Parse($Matches.max,$culture)
    }
  }
}
Write-Warning 'Inclusive CPU wall time, grouped across threads. Shader entry/lock are inside registration; texture allocate/upload/prepare are inside original and may nest. Resource worker phases include nested work and waits and may overlap other threads. Do not add nested totals. Time filters select completion timestamps, not clipped intervals. Log rotation may omit earlier records.'
$records | Group-Object Phase | Sort-Object Name | ForEach-Object {
  [pscustomobject]@{
    Phase=$_.Name
    Calls=($_.Group | Measure-Object Calls -Sum).Sum
    TotalMs=($_.Group | Measure-Object TotalMs -Sum).Sum
    MaxMs=($_.Group | Measure-Object MaxMs -Maximum).Maximum
  }
}
