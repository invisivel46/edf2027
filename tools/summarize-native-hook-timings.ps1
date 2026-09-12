param(
  [Parameter(Mandatory=$true)][string[]]$LogPath,
  [datetime]$Since=[datetime]::MinValue,
  [datetime]$Until=[datetime]::MaxValue,
  [switch]$ByThread
)
$ErrorActionPreference='Stop'
if($Until -lt $Since) { throw 'Until must not precede Since' }
$culture=[System.Globalization.CultureInfo]::InvariantCulture
$buckets=@{}
# Hooks report independent, per-thread 256-call buckets. Their wall times are
# inclusive and may overlap; never sum all phases into an exclusive frame time.
# Since filters report timestamps, not individual calls: a boundary bucket may
# contain earlier calls, and unreported trailing calls are absent from totals.
Get-Content -LiteralPath $LogPath | ForEach-Object {
  if($_ -match '^\[(?<time>[^\]]+)\].*\[t(?<thread>\d+)\] Native hook timing: phase=(?<phase>[\w.]+) calls=(?<calls>\d+) total_ms=(?<total>[\d.eE+-]+) max_ms=(?<maximum>[\d.eE+-]+)') {
    $stamp=[datetime]::ParseExact($Matches.time,'yyyy-MM-dd HH:mm:ss.fff',$culture)
    if($stamp -ge $Since -and $stamp -le $Until) {
      $phase=$Matches.phase
      $thread=$Matches.thread
      $key=if($ByThread) { $phase + ':' + $thread } else { $phase }
      if(-not $buckets.ContainsKey($key)) {
        $buckets[$key]=@{Phase=$phase; Thread=$thread; Calls=[long]0; Total=[double]0; Maximum=[double]0; Buckets=0}
      }
      $bucket=$buckets[$key]
      $bucket.Calls += [long]::Parse($Matches.calls,$culture)
      $bucket.Total += [double]::Parse($Matches.total,$culture)
      $bucket.Maximum=[Math]::Max($bucket.Maximum,[double]::Parse($Matches.maximum,$culture))
      $bucket.Buckets++
    }
  }
}
foreach($key in ($buckets.Keys | Sort-Object)) {
  $bucket=$buckets[$key]
  $result=[pscustomobject]@{
    Phase=$bucket.Phase
    Buckets=$bucket.Buckets
    Calls=$bucket.Calls
    TotalMilliseconds=$bucket.Total
    AverageMilliseconds=$bucket.Total/$bucket.Calls
    MaxCallMilliseconds=$bucket.Maximum
  }
  if($ByThread) { $result | Add-Member -NotePropertyName ThreadId -NotePropertyValue ([long]$bucket.Thread) }
  $result
}
