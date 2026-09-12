param(
  [string]$GeneratedDirectory = 'generated/default',
  [string]$OutputDirectory = 'out/geometry-registration-candidates'
)
$ErrorActionPreference = 'Stop'
$directory = (Resolve-Path -LiteralPath $GeneratedDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
# Syntactic candidates only: neither object type nor argument provenance is
# established. Include bctr tail wrappers as well as bctrl calls. This is not
# an exhaustive indirect-call resolver (indexed loads/register copies excluded).
$functionPattern = [regex]'DEFINE_REX_FUNC\((?<name>[^)]+)\)'
$instructionPattern = [regex]'(?m)^\s*// (?<asm>[^\r\n]+)'
$rows = foreach ($file in Get-ChildItem -LiteralPath $directory -Filter '*_recomp.*.cpp' -File) {
  $source = [IO.File]::ReadAllText($file.FullName)
  $functions = $functionPattern.Matches($source)
  for ($f = 0; $f -lt $functions.Count; ++$f) {
    $begin = $functions[$f].Index
    $end = if ($f + 1 -lt $functions.Count) { $functions[$f + 1].Index } else { $source.Length }
    $instructions = $instructionPattern.Matches($source.Substring($begin, $end - $begin))
    for ($i = 0; $i -lt $instructions.Count; ++$i) {
      $load = $instructions[$i].Groups['asm'].Value.Trim()
      if ($load -notmatch '^lwz (?<target>r\d+),(?<slot>56|60)\((?<table>r\d+)\)$') { continue }
      $target = $Matches['target']; $slot = [int]$Matches['slot']; $table = $Matches['table']
      $ctrSeen = $false
      for ($j = $i + 1; $j -lt [Math]::Min($i + 9, $instructions.Count); ++$j) {
        $instruction = $instructions[$j].Groups['asm'].Value.Trim()
        if ($instruction -eq "mtctr $target") { $ctrSeen = $true; continue }
        if ($instruction -match '^mtctr ') { break }
        # A subsequent load/arithmetic result can replace the candidate before
        # it reaches CTR. Do not attribute that later dispatch to this slot.
        # Conservative: even an identity move counts as a clobber here.
        if (-not $ctrSeen -and
            $instruction -match ('^\w+\.? ' + [regex]::Escape($target) + ',') -and
            $instruction -notmatch '^(st\w*|cmp\w*|mt\w*) ') { break }
        if ($instruction -match '^bctrl?$') {
          if ($ctrSeen) {
            $context = for ($k = [Math]::Max(0, $i - 20); $k -le $j; ++$k) {
              $instructions[$k].Groups['asm'].Value.Trim()
            }
            [pscustomobject]@{
              File = $file.Name
              Caller = $functions[$f].Groups['name'].Value
              Slot = ('0x{0:x}' -f $slot)
              TargetRegister = $target
              TableRegister = $table
              Dispatch = $instruction
              SourceOffset = $begin + $instructions[$j].Index
              Context = $context -join '; '
              Classification = 'Unresolved syntactic candidate'
            }
          }
          break
        }
        # Stop at any other branch: do not silently cross a control-flow edge.
        if ($instruction -match '^b') { break }
      }
    }
  }
}
$rows | Export-Csv -LiteralPath (Join-Path $output 'candidates.csv') -NoTypeInformation -Encoding UTF8
Get-ChildItem -LiteralPath $directory -Filter '*_recomp.*.cpp' -File | ForEach-Object {
  [pscustomobject]@{ File = $_.Name; SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
} | Export-Csv -LiteralPath (Join-Path $output 'source-manifest.csv') -NoTypeInformation -Encoding UTF8
$rows | Group-Object Slot,Dispatch | Select-Object Name,Count
