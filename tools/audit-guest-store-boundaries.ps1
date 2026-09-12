param([string]$GeneratedDirectory = 'generated/default', [switch]$Details,
  [string]$CsvPath)
$ErrorActionPreference = 'Stop'
if (-not [IO.Path]::IsPathRooted($GeneratedDirectory)) {
  $GeneratedDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) $GeneratedDirectory
}
$directory = (Resolve-Path -LiteralPath $GeneratedDirectory).Path
# Instruction comments delimit emitted statements. This is a source inventory,
# not a decoder or proof that a store can reach any particular model allocation.
$instructions = [regex]::new('(?m)^\s*// (?<opcode>st[a-z0-9.]+|dcbzl?)[^\r\n]*\r?\n(?<body>(?:(?!^\s*// |^DEFINE_REX_FUNC)[\s\S])*)')
$functions = [regex]::new('DEFINE_REX_FUNC\((?<name>[^)]+)\)')
$records = foreach ($file in Get-ChildItem -LiteralPath $directory -Filter '*_recomp.*.cpp' -File) {
  $source = [IO.File]::ReadAllText($file.FullName)
  $entries = $functions.Matches($source)
  $lineStarts = [int[]](@(0) + @([regex]::Matches($source,"`n") | ForEach-Object { $_.Index+1 }))
  $entryIndex = 0
  foreach ($instruction in $instructions.Matches($source)) {
    while ($entryIndex + 1 -lt $entries.Count -and $entries[$entryIndex + 1].Index -lt $instruction.Index) { ++$entryIndex }
    $body = $instruction.Groups['body'].Value
    $commentOffset=$source.IndexOf('//',$instruction.Index)
    $instructionText=$source.Substring($commentOffset,$source.IndexOf("`n",$commentOffset)-$commentOffset).Trim()
    $lineIndex=[Array]::BinarySearch($lineStarts,[int]$commentOffset)
    if($lineIndex -lt 0) { $lineIndex=(-bnot $lineIndex)-1 }
    $endLineIndex=[Array]::BinarySearch($lineStarts,[int]($instruction.Index+$instruction.Length-1))
    if($endLineIndex -lt 0) { $endLineIndex=(-bnot $endLineIndex)-1 }
    $route = if ($body -match 'REX_MM_STORE_') { 'mmio_or_scalar' }
      elseif ($body -match 'REX_STORE_') { 'scalar_macro' }
      elseif ($body -match '__sync_bool_compare_and_swap') { 'atomic_raw' }
      elseif ($body -match 'simde_.*store.*REX_RAW_ADDR') { 'vector_raw' }
      elseif ($body -match 'memset') { 'bulk_zero' }
      elseif ($body -match 'REX_RAW_ADDR') { 'other_raw' }
      else { 'unclassified' }
    [pscustomobject]@{
      File = $file.Name
      Function = if ($entries.Count) { $entries[$entryIndex].Groups['name'].Value } else { '<unknown>' }
      Opcode = $instruction.Groups['opcode'].Value
      Route = $route
      SourceOffset = $instruction.Index
      SourceLine = $lineIndex+1
      SourceEndLine = $endLineIndex+1
      Instruction = $instructionText
      DestinationClass = if($instructionText -match ',\s*-?\d+\(r1\)$') {'stack_relative_immediate'} else {'needs_alias_trace'}
      EmittedWrite = $body.Trim()
    }
  }
}
if (-not $records) { throw 'No generated store instructions found; verify the source directory and emitter format.' }
if ($CsvPath) {
  $csvFullPath=[IO.Path]::GetFullPath($CsvPath)
  $csvDirectory=Split-Path $csvFullPath -Parent
  if(-not (Test-Path -LiteralPath $csvDirectory)) { New-Item -ItemType Directory -Path $csvDirectory | Out-Null }
  $records | Export-Csv -LiteralPath $csvFullPath -NoTypeInformation -Encoding UTF8
}
if ($Details) { $records } else {
  $records | Group-Object Route,Opcode | Sort-Object Count -Descending |
    Select-Object Count,@{Name='RouteOpcode';Expression={$_.Name}}
}
