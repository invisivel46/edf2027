param(
    [Parameter(Mandatory=$true)][string]$LogPath,
    [switch]$ReverseDepth
)
$ErrorActionPreference = 'Stop'
# Diagnostic candidate only: not every VS uses position * world * view * projection.
# Matrices in this trace are already logical row-major, regardless of CB storage.
function Parse-Values([string]$Text) {
    return ,([double[]]@($Text.Split(',') | ForEach-Object {
        [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture)
    }))
}
function Multiply-Row([double[]]$Point, [object[]]$Rows) {
    $result = [double[]]::new(4)
    for ($column=0; $column -lt 4; $column++) {
        for ($row=0; $row -lt 4; $row++) {
            $result[$column] += $Point[$row] * $Rows[$row][$column]
        }
    }
    return ,$result
}
function Compare-Probe($Probe) {
    if ($null -eq $Probe) { return }
    foreach ($name in 'g_mWorld','g_mView','g_mProjection') {
        if (!$Probe.Matrices.ContainsKey($name)) { return }
        foreach ($row in $Probe.Matrices[$name]) { if ($null -eq $row) { return } }
    }
    $maximum = 0.0
    $samples = 0
    foreach ($index in $Probe.Inputs.Keys) {
        if (!$Probe.Outputs.ContainsKey($index)) { continue }
        $inputPoint = $Probe.Inputs[$index]
        $point = [double[]]@($inputPoint[0],$inputPoint[1],$inputPoint[2],1.0)
        foreach ($name in 'g_mWorld','g_mView','g_mProjection') {
            $point = Multiply-Row $point $Probe.Matrices[$name]
        }
        if ($ReverseDepth) { $point[2] = $point[3] - $point[2] }
        for ($lane=0; $lane -lt 4; $lane++) {
            $errorValue = [Math]::Abs($point[$lane] - $Probe.Outputs[$index][$lane])
            if ([double]::IsNaN($errorValue) -or [double]::IsInfinity($errorValue)) {
                throw 'Non-finite matrix comparison; no numeric match can be inferred'
            }
            $maximum = [Math]::Max($maximum,$errorValue)
        }
        $samples++
    }
    if ($samples) {
        [pscustomobject]@{VS=$Probe.VS; Entry=$Probe.Entry; Samples=$samples;
            MaxAbsoluteWvpError=$maximum; ReverseDepth=[bool]$ReverseDepth}
    }
}
$probe = $null
foreach ($line in Get-Content -LiteralPath $LogPath) {
    if ($line -match 'Native clip probe: VS=(\S+) (\S+),') {
        Compare-Probe $probe
        $probe = @{VS=$Matches[1]; Entry=$Matches[2]; Matrices=@{}; Inputs=@{}; Outputs=@{}}
    } elseif ($null -ne $probe -and $line -match 'Native clip matrix: name=(\S+), row=(\d+), values=(\S+)') {
        $name=$Matches[1]; $row=[int]$Matches[2]; $values=Parse-Values $Matches[3]
        if ($row -ge 4 -or $values.Count -ne 4) { throw 'Invalid matrix row in trace' }
        if (!$probe.Matrices.ContainsKey($name)) { $probe.Matrices[$name]=[object[]]::new(4) }
        $probe.Matrices[$name][$row]=$values
    } elseif ($null -ne $probe -and $line -match 'Native clip input: index=(\d+), vertex=\d+, xyz=(\S+)') {
        $index=[int]$Matches[1]; $values=Parse-Values $Matches[2]
        if ($values.Count -ne 3) { throw 'Invalid input position in trace' }
        $probe.Inputs[$index]=$values
    } elseif ($null -ne $probe -and $line -match 'Native clip vertex: index=(\d+), xyzw=(\S+)') {
        $index=[int]$Matches[1]; $values=Parse-Values $Matches[2]
        if ($values.Count -ne 4) { throw 'Invalid clip position in trace' }
        $probe.Outputs[$index]=$values
    }
}
Compare-Probe $probe
