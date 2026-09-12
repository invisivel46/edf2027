param(
    [string]$Edges = 'out/geometry-writer-inventory/generated-call-edges.csv',
    [string]$Inventory = 'docs/geometry-binding-consumers.csv'
)
$ErrorActionPreference = 'Stop'
$records = @(Import-Csv $Inventory)
$actual = @(Import-Csv $Edges | Where-Object {
    $_.Callee -in @('sub_82137410', 'sub_821375C0')
} | Select-Object -ExpandProperty Caller -Unique | Sort-Object)
$listed = @($records.Caller | Sort-Object -Unique)
if ($listed.Count -ne $records.Count) { throw 'Duplicate caller classifications' }
$difference = @(Compare-Object $actual $listed)
if ($difference.Count) {
    $difference | Format-Table
    throw 'Direct binding callers and classification inventory differ'
}
if (@($records | Where-Object { -not $_.Role -or -not $_.DirectPayloadEffect -or -not $_.OpenBoundary }).Count) {
    throw 'Every caller requires a role, direct effect, and explicit open boundary'
}
Write-Output "Matched $($actual.Count) distinct direct binding callers."
Write-Output 'This checks inventory coverage only, not semantic correctness or indirect-call/writer closure.'
