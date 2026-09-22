param(
    [string]$Java = 'C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot/bin/java.exe',
    [string]$Installation = 'out/ghidra/installation',
    [string]$Output = 'out/renderer-automation-pilot/ghidra/facts.json',
    [switch]$AllCensus
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installationPath = [IO.Path]::GetFullPath((Join-Path $root $Installation))
$outputPath = [IO.Path]::GetFullPath((Join-Path $root $Output))
$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$inventoryName = if ($AllCensus) { 'complete-function-inventory.csv' } else { 'scalar-dispatch-review.csv' }
$inventoryPath = Join-Path $root "out/renderer-inventory/$inventoryName"
if (-not (Test-Path -LiteralPath $inventoryPath -PathType Leaf)) {
    throw "Renderer inventory is missing: $inventoryPath"
}
$addresses = @(
    Import-Csv -LiteralPath $inventoryPath |
        ForEach-Object { $_.function } |
        Where-Object { $_ -match '^sub_[0-9A-Fa-f]+$' } |
        ForEach-Object { $_.Substring(4).ToUpperInvariant() } |
        Sort-Object -Unique
)
if ($addresses.Count -eq 0) { throw "No function addresses found in $inventoryPath" }
if (-not $AllCensus -and $addresses.Count -ne 170) {
    throw "Expected 170 bounded scalar-dispatch functions, found $($addresses.Count)"
}

$manifestName = if ($AllCensus) { 'census-function-addresses.txt' } else { 'scalar-function-addresses.txt' }
$manifestPath = Join-Path $outputDirectory $manifestName
$addresses | Set-Content -LiteralPath $manifestPath -Encoding ascii
$logPath = Join-Path $outputDirectory 'export.log'
$gsonPath = Join-Path $installationPath 'Ghidra/Framework/Generic/lib/gson-2.13.2.jar'
$utilityPath = Join-Path $installationPath 'Ghidra/Framework/Utility/lib/Utility.jar'
foreach ($requiredPath in @($Java, $gsonPath, $utilityPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required exporter dependency is missing: $requiredPath"
    }
}

$classPath = "$utilityPath;$gsonPath"
& $Java '-Djava.system.class.loader=ghidra.GhidraClassLoader' `
    "-Dapplication.settingsdir=$root/out/ghidra/settings" `
    "-Dapplication.cachedir=$root/out/ghidra/cache" `
    "-Dapplication.tempdir=$root/out/ghidra/temp" -Xmx2G `
    -cp $classPath `
    ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless `
    "$root/out/ghidra/project" edf2027-geometry -process guest_image.bin -noanalysis -readOnly `
    -scriptPath "$root/tools/ghidra" `
    -postScript RendererScalarFacts.java $outputPath $manifestPath `
    -log $logPath
if ($LASTEXITCODE -ne 0) { throw "Ghidra scalar fact export failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
    throw "Ghidra completed without producing $outputPath"
}

$facts = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
$actualFunctions = @($facts.functions).Count
$actualInstructionRows = @(
    foreach ($function in $facts.functions) {
        foreach ($instruction in $function.instructions) {
            [pscustomobject]@{
                key = "$($function.function)|$($instruction.address.ToUpperInvariant())"
                function = $function.function
                address = $instruction.address.ToUpperInvariant()
                bytes = $instruction.bytes.ToUpperInvariant()
            }
        }
    }
)
$actualInstructions = $actualInstructionRows.Count
if ($actualFunctions -ne $addresses.Count) {
    throw "Export coverage mismatch: requested $($addresses.Count) functions, wrote $actualFunctions"
}
if (-not $AllCensus) {
    $frozenInstructionsPath = Join-Path $root 'out/renderer-inventory/scalar-dispatch-instructions.csv'
    $expectedInstructions = @(Import-Csv -LiteralPath $frozenInstructionsPath)
    $actualByKey = @{}
    foreach ($instruction in $actualInstructionRows) { $actualByKey[$instruction.key] = $instruction }
    $expectedKeys = @{}
    $missing = @()
    $byteMismatches = @()
    foreach ($expected in $expectedInstructions) {
        $key = "$($expected.function)|$($expected.site.ToUpperInvariant())"
        $expectedKeys[$key] = $true
        if (-not $actualByKey.ContainsKey($key)) {
            $missing += $key
        }
        elseif ($actualByKey[$key].bytes -ne $expected.raw.ToUpperInvariant()) {
            $byteMismatches += "$key expected=$($expected.raw) actual=$($actualByKey[$key].bytes)"
        }
    }
    if ($expectedInstructions.Count -ne 1443) {
        throw "Frozen scalar instruction manifest changed: expected 1443 rows, found $($expectedInstructions.Count)"
    }
    if ($missing.Count -or $byteMismatches.Count) {
        throw "Frozen raw coverage failed: $($missing.Count) missing and $($byteMismatches.Count) byte mismatches"
    }
    $additional = @($actualInstructionRows | Where-Object { -not $expectedKeys.ContainsKey($_.key) })
    if ($additional.Count) {
        $additionalFunctions = @($additional.function | Sort-Object -Unique) -join ', '
        Write-Warning "Ghidra body membership adds $($additional.Count) instructions beyond the frozen 1443 at: $additionalFunctions"
    }
}
$incomplete = @($facts.functions | Where-Object { -not $_.decompile_completed }).Count
Write-Host "Renderer scalar facts: $actualFunctions functions, $actualInstructions raw instructions, $incomplete incomplete decompilations"
Write-Host "Inventory SHA256: $((Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "Address-list SHA256: $((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant())"
Write-Host "Output: $outputPath"
