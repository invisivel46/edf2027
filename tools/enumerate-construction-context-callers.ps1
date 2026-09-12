param(
  [string]$SourceDirectory = 'generated/default',
  [string]$OutputPath = 'out/geometry-writer-inventory/construction-context-callers.csv'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($SourceDirectory)) { $SourceDirectory = Join-Path $workspace $SourceDirectory }
if (-not [IO.Path]::IsPathRooted($OutputPath)) { $OutputPath = Join-Path $workspace $OutputPath }
# This is a lexical index, not register dataflow or an alias certificate.
$functions = @{}
foreach ($file in (Get-ChildItem -LiteralPath $SourceDirectory -Filter 'edf2017_recomp.*.cpp')) {
  $source = Get-Content -LiteralPath $file.FullName -Raw
  foreach ($match in [regex]::Matches($source, '(?s)DEFINE_REX_FUNC\(sub_([0-9A-F]+)\).*?(?=\r?\nDEFINE_REX_FUNC|\z)')) {
    $name = $match.Groups[1].Value
    if ($functions.ContainsKey($name)) { throw "Duplicate generated function $name" }
    $functions[$name] = $match.Value
  }
}
$factories = @{}
foreach ($name in $functions.Keys) {
  $body = $functions[$name]
  # Restrict discovery to the reviewed emitted base/displacement form. Other
  # ways of writing the global, and indirect entries, are not enumerated here.
  if ($body -notmatch 'ctx\.r30\.s64 = -2108162048;') { continue }
  if ($body -match 'REX_STORE_U32\(ctx\.r30\.u32 \+ -15600, ctx\.r5\.u32\);') {
    $factories[$name] = 'entry_r5'
  } elseif ($name -eq '821E49D0' -and
      $body -match 'ctx\.r31\.u64 = ctx\.r5\.u64;' -and
      $body -match 'REX_STORE_U32\(ctx\.r30\.u32 \+ -15600, ctx\.r31\.u32\);') {
    $factories[$name] = 'entry_r5_via_r31'
  }
}
if (-not $factories.Count) { throw 'No typed context publishers matched' }
$rows = @(
  foreach ($caller in $functions.Keys) {
    $lines = $functions[$caller] -split '\r?\n'
    $setup = 'not present earlier in this function'
    for ($i = 0; $i -lt $lines.Count; ++$i) {
      if ($lines[$i] -match '^\s*ctx\.r5\.[a-z0-9]+ = ') { $setup = $lines[$i].Trim() }
      if ($lines[$i] -notmatch '^\s*sub_([0-9A-F]+)\(ctx, base\);') { continue }
      $callee = $Matches[1]
      if (-not $factories.ContainsKey($callee)) { continue }
      if ($i -eq 0 -or $lines[$i-1] -notmatch '^\s*ctx\.lr = 0x([0-9A-F]+);') {
        throw "Call to $callee in $caller has no adjacent return address"
      }
      $callPC = ([Convert]::ToUInt32($Matches[1],16)-4).ToString('X8')
      [pscustomobject]@{
        Caller=$caller; CallPC=$callPC; Factory=$callee
        ContextArgument=$factories[$callee]; LastLexicalR5Assignment=$setup
        Status='unclassified; lexical setup is not reaching-definition or ownership proof'
      }
    }
  }
) | Sort-Object Caller,CallPC,Factory
if (@($rows | Group-Object Caller,CallPC | Where-Object Count -gt 1).Count) {
  throw 'Duplicate caller/instruction pair'
}
$directory = Split-Path $OutputPath -Parent
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$rows | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
"Indexed $($rows.Count) direct call sites across $($factories.Count) typed factories: $OutputPath"
'Indirect entries, alternate global stores, and semantic ownership are not covered.'
