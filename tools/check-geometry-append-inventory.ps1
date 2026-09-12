param(
  [string]$GeneratedDirectory='generated/default',
  [string]$Inventory='docs/geometry-append-callers.csv',
  [object[]]$ExpectedRows
)
$ErrorActionPreference='Stop'
$actual=@(foreach($file in Get-ChildItem -LiteralPath $GeneratedDirectory -Filter '*_recomp.*.cpp' -File) {
  $source=[IO.File]::ReadAllText($file.FullName)
  $functions=[regex]::Matches($source,'DEFINE_REX_FUNC\(sub_(?<name>[0-9A-F]+)\)')
  $index=0
  $calls=[regex]::Matches($source,'(?m)^\s*ctx\.lr = 0x(?<lr>[0-9A-F]+);\r?\n\s*sub_821C8A20\(ctx, base\);')
  $allCalls=[regex]::Matches($source,'(?m)^\s*sub_821C8A20\(ctx, base\);')
  if($calls.Count -ne $allCalls.Count) { throw 'Append call without explicit return address; inventory needs review' }
  foreach($call in $calls) {
    while($index+1 -lt $functions.Count -and $functions[$index+1].Index -lt $call.Index) { ++$index }
    if(-not $functions.Count -or $functions[$index].Index -gt $call.Index) { throw 'Append call outside recognized function' }
    $pc=[Convert]::ToUInt32($call.Groups['lr'].Value,16)-4
    $functions[$index].Groups['name'].Value+'|'+$pc.ToString('X8')
  }
})
if(-not $actual.Count) { throw 'No append sites found; check source/emitter format' }
$records=if($PSBoundParameters.ContainsKey('ExpectedRows')) {@($ExpectedRows)} else {@(Import-Csv -LiteralPath $Inventory)}
if(-not $records.Count) { throw 'Empty append inventory' }
foreach($record in $records) {
  if(-not $record.Caller -or -not $record.CallPC -or -not $record.VectorOrigin -or
     -not $record.ValueSource -or -not $record.Status) { throw 'Incomplete append inventory row' }
}
$listed=@($records | ForEach-Object {$_.Caller+'|'+$_.CallPC})
if(@($listed | Sort-Object -Unique).Count -ne $listed.Count) { throw 'Duplicate append inventory site' }
if(@(Compare-Object @($actual | Sort-Object) @($listed | Sort-Object)).Count) { throw 'Generated append sites and inventory differ' }
Write-Output "Matched $($actual.Count) direct append instruction sites."
Write-Output 'This checks source enumeration, not semantic ownership, indirect calls or writer completeness.'
