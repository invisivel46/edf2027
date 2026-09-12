param(
  [string]$GeneratedDirectory='generated/default',
  [string]$Inventory='docs/geometry-physical-free-sites.csv',
  [object[]]$ExpectedRows
)
$ErrorActionPreference='Stop'
$functionPattern=[regex]::new('DEFINE_REX_FUNC\((?<name>[^)]+)\)')
$callPattern=[regex]::new('(?m)^\s*(?:ctx\.lr = 0x(?<lr>[0-9A-Fa-f]+);\r?\n\s*)?(?<callee>sub_8212FC28|__imp__MmFreePhysicalMemory)\(ctx, base\);')
$actual=@(foreach($file in Get-ChildItem -LiteralPath $GeneratedDirectory -Filter '*_recomp.*.cpp' -File) {
  $source=[IO.File]::ReadAllText($file.FullName)
  $functions=$functionPattern.Matches($source); $index=0
  foreach($call in $callPattern.Matches($source)) {
    while($index+1 -lt $functions.Count -and $functions[$index+1].Index -lt $call.Index) { ++$index }
    if(-not $functions.Count -or $functions[$index].Index -gt $call.Index) { throw 'Free call outside recognized generated function' }
    $lr=if($call.Groups['lr'].Success) {$call.Groups['lr'].Value.ToUpperInvariant()} else {'inherited'}
    $functions[$index].Groups['name'].Value+'|'+$call.Groups['callee'].Value+'|'+$lr
  }
})
if(-not $actual.Count) { throw 'No physical-free sites found; verify generated directory/emitter format' }
$records=if($PSBoundParameters.ContainsKey('ExpectedRows')) {@($ExpectedRows)} else {@(Import-Csv -LiteralPath $Inventory)}
if(-not $records.Count) { throw 'Empty physical-free inventory' }
foreach($record in $records) {
  if(-not $record.Caller -or -not $record.Callee -or -not $record.ReturnAddress -or
     -not $record.NativeCoverage -or -not $record.OpenBoundary) { throw 'Every free site needs location, coverage and an explicit open boundary' }
}
$listed=@($records | ForEach-Object {$_.Caller+'|'+$_.Callee+'|'+$_.ReturnAddress})
if(@($listed | Sort-Object -Unique).Count -ne $listed.Count) { throw 'Duplicate physical-free inventory site' }
$difference=@(Compare-Object @($actual | Sort-Object) @($listed | Sort-Object))
if($difference.Count) { $difference | Format-Table; throw 'Generated physical-free sites and inventory differ' }
Write-Output "Matched $($actual.Count) emitted free sites, including inherited return-address edges."
Write-Output 'Inventory coverage only: classifications, indirect calls and native lifetime correctness remain separate obligations.'
