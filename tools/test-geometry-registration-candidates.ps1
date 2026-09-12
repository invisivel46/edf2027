$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'out/geometry-registration-candidates-test'
& (Join-Path $PSScriptRoot 'enumerate-geometry-registration-candidates.ps1') `
  -GeneratedDirectory (Join-Path $repo 'tests/fixtures/registration-candidates') `
  -OutputDirectory $output | Out-Null
$rows = @(Import-Csv -LiteralPath (Join-Path $output 'candidates.csv'))
$expected = @('call_registration', 'tail_registration', 'target_changed_after_ctr')
$actual = @($rows.Caller | Sort-Object)
if (($actual -join ',') -ne ($expected -join ',')) {
  throw "Unexpected candidate set: $($actual -join ',')"
}
foreach ($row in $rows) {
  $source = [IO.File]::ReadAllText((Join-Path $repo ('tests/fixtures/registration-candidates/' + $row.File)))
  if ($source.Substring([int]$row.SourceOffset) -notmatch '^\s*// bctrl?\s*\r?\n') {
    throw "Invalid dispatch offset for $($row.Caller)"
  }
}
if (($rows | Where-Object Caller -eq 'call_registration').Slot -ne '0x3c') { throw 'Wrong call slot' }
if (($rows | Where-Object Caller -eq 'tail_registration').Dispatch -ne 'bctr') { throw 'Tail call missed' }
'Registration candidate tests passed: calls, tails, register/CTR clobbers, branches, function boundaries, offsets.'
