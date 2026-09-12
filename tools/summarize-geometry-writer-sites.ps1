param([Parameter(Mandatory=$true)][string[]]$LogPath,
  [string]$Inventory='out/geometry-writer-inventory/generated-store-sites.csv')
$ErrorActionPreference='Stop'
$byFile=@{}
Import-Csv -LiteralPath $Inventory | ForEach-Object {
  if(-not $_.SourceEndLine) {throw 'Regenerate the store inventory with source end lines'}
  if(-not $byFile.ContainsKey($_.File)) {$byFile[$_.File]=[Collections.Generic.List[object]]::new()}
  $byFile[$_.File].Add($_)
}
$sites=@{}; [long]$omitted=0
$seen=[Collections.Generic.HashSet[string]]::new()
Get-Content -LiteralPath $LogPath | ForEach-Object {
  $line=$_
  if($line -match 'Native geometry writer inventory incomplete: omitted_hits=(\d+)') {
    if($seen.Add($line)) {$omitted += [long]$Matches[1]}
  } elseif($line -match 'Native geometry writer site: file=(?<file>.*), line=(?<line>\d+), owner=(?<owner>0x[0-9a-fA-F]+), lifetime=(?<lifetime>\d+), calls=(?<calls>\d+), first_physical=(?<physical>0x[0-9a-fA-F]+), first_bytes=(?<bytes>\d+)') {
    if(-not $seen.Add($line)) {return}
    $file=[IO.Path]::GetFileName($Matches.file); $sourceLine=[int]$Matches.line
    $key=$file+':'+$sourceLine+':'+$Matches.owner+':'+$Matches.lifetime
    if(-not $sites.ContainsKey($key)) {
      $records=$byFile[$file]; $match=$null
      # Sorted source intervals: find the last store beginning at/before this line.
      $low=0; $high=if($records) {$records.Count-1} else {-1}
      while($low -le $high) {
        $middle=($low+$high) -shr 1
        if([int]$records[$middle].SourceLine -le $sourceLine) {$match=$records[$middle];$low=$middle+1}
        else {$high=$middle-1}
      }
      if($match -and [int]$match.SourceEndLine -lt $sourceLine) {$match=$null}
      $sites[$key]=[pscustomobject]@{
        File=$file; Line=$sourceLine; Function=if($match){$match.Function}else{'<unresolved>'}
        Instruction=if($match){$match.Instruction}else{''}
        Owner=$Matches.owner; Lifetime=[long]$Matches.lifetime; Calls=[long]0
        FirstPhysical=$Matches.physical; FirstBytes=[int]$Matches.bytes
      }
    }
    $sites[$key].Calls += [long]$Matches.calls
  }
}
[pscustomobject]@{
  Sites=@($sites.Values | Sort-Object File,Line,Owner,Lifetime)
  OmittedHits=$omitted
  UnresolvedSites=@($sites.Values | Where-Object Function -EQ '<unresolved>').Count
  Note='Observed workload only; zero sites is not proof of immutable geometry. Match inventory to the build source.'
}
