param(
    [string]$Generated = 'generated/default',
    [string]$Output = 'out/geometry-bit-reader-outputs.csv'
)
$ErrorActionPreference = 'Stop'
# Syntactic call-site evidence only. No transitive alias or path proof.
$rows = [Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem -LiteralPath $Generated -Filter 'edf2017_recomp.*.cpp') {
    $lines = [IO.File]::ReadAllLines($file.FullName)
    $caller = ''
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match '^DEFINE_REX_FUNC\(([^)]+)') { $caller = $Matches[1] }
        if ($lines[$i] -notmatch '^\s*(sub_82460668|sub_824607D8)\(ctx, base\);') { continue }
        $callee = $Matches[1]
        $outputRegister = if ($callee -eq 'sub_82460668') { 'r5' } else { 'r4' }
        $prep = ''
        $site = ''
        $state = 'unresolved'
        # Walk only the straight-line preparation immediately preceding this call.
        for ($j = $i - 1; $j -ge 0; $j--) {
            $line = $lines[$j].Trim()
            if ($line -match '^ctx.lr = (0x[0-9A-Fa-f]+);$' -and !$site) {
                $site = '{0:X8}' -f ([Convert]::ToInt64($Matches[1].Substring(2),16) - 4)
            }
            if ($line -match '^loc_|^DEFINE_REX_FUNC|^if\s|^goto\s|^return;|^\w+\(ctx, base\);') { break }
            if ($line -match ('^// (\w+) ' + $outputRegister + ',(.*)$')) {
                $opcode = $Matches[1]
                $operands = $Matches[2]
                # Compare/store instructions mention rN without defining it.
                if ($opcode -match '^(st|cmp|cmpl)') { continue }
                $prep = $line.Substring(3)
                if ($opcode -eq 'addi' -and $operands -match '^r1,([0-9]+)$') {
                    $state = 'explicit_stack_address'
                } else { $state = 'register_preparation_needs_review' }
                break
            }
        }
        $rows.Add([pscustomobject]@{
            File=$file.Name; Caller=$caller; Callee=$callee; CallAddress=$site
            SourceLine=$i+1; OutputRegister=$outputRegister; Preparation=$prep; Evidence=$state
        })
    }
}
$parent = Split-Path -Parent $Output
if ($parent -and !(Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent | Out-Null }
$rows | Sort-Object Caller,CallAddress | Export-Csv -LiteralPath $Output -NoTypeInformation
$rows | Group-Object Callee,Evidence | Select-Object Name,Count
Write-Output "Wrote $($rows.Count) direct call sites. This is preparation evidence, not alias certification."
