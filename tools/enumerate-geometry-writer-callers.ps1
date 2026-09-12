param([string]$GeneratedDirectory='generated/default',
  [string]$OutputDirectory='out/geometry-writer-inventory')
$ErrorActionPreference='Stop'
$directory=(Resolve-Path -LiteralPath $GeneratedDirectory).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(-not (Test-Path -LiteralPath $output)) { New-Item -ItemType Directory -Path $output | Out-Null }
# Full emitted direct-call graph plus unresolved indirect sites. Reachability
# alone does not establish pointer aliasing, execution or payload mutation.
$functionPattern=[regex]::new('DEFINE_REX_FUNC\((?<name>[^)]+)\)')
$callPattern=[regex]::new('(?m)^\s*(?:(?<direct>\w+)\(ctx, base\);|REX_CALL_INDIRECT_FUNC\((?<indirect>[^;]+)\);)')
$edges=foreach($file in Get-ChildItem -LiteralPath $directory -Filter '*_recomp.*.cpp' -File) {
  $source=[IO.File]::ReadAllText($file.FullName)
  $functions=$functionPattern.Matches($source); $index=0
  foreach($call in $callPattern.Matches($source)) {
    while($index+1 -lt $functions.Count -and $functions[$index+1].Index -lt $call.Index) {++$index}
    [pscustomobject]@{
      File=$file.Name
      Caller=if($functions.Count) {$functions[$index].Groups['name'].Value} else {'<unknown>'}
      Callee=$call.Groups['direct'].Value
      IndirectTarget=$call.Groups['indirect'].Value
      SourceOffset=$call.Index
    }
  }
}
$edges | Export-Csv -LiteralPath (Join-Path $output 'generated-call-edges.csv') -NoTypeInformation -Encoding UTF8
$edges | Where-Object {$_.Callee -like '__imp__*'} |
  Export-Csv -LiteralPath (Join-Path $output 'imported-provider-call-sites.csv') -NoTypeInformation -Encoding UTF8
Get-ChildItem -LiteralPath $directory -Filter '*_recomp.*.cpp' -File | ForEach-Object {
  [pscustomobject]@{File=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
} | Export-Csv -LiteralPath (Join-Path $output 'source-manifest.csv') -NoTypeInformation -Encoding UTF8
$writers=@('sub_821D7530','sub_821D76A8','sub_82134958','sub_82134A78',
  'sub_821349B8','sub_82134AD8','sub_821E8230','sub_821E8320','sub_821E8740','sub_821EA320','sub_821E9BA0',
  '__imp__NtReadFile','__imp__RtlFillMemoryUlong')
$known=$edges | Where-Object {$writers -contains $_.Callee}
$known | Export-Csv -LiteralPath (Join-Path $output 'known-writer-direct-callers.csv') -NoTypeInformation -Encoding UTF8
$known | Group-Object Callee | Sort-Object Name | Select-Object Name,Count
[pscustomobject]@{Name='All emitted direct call sites';Count=@($edges | Where-Object Callee).Count}
[pscustomobject]@{Name='Unresolved indirect call sites';Count=@($edges | Where-Object IndirectTarget).Count}
