param(
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) {
    $BuildDir = Join-Path $root 'build-phase13-hardware'
}

& cmake -S $root -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 13 x64 configure failed.'
}

& cmake --build $BuildDir --config Release --parallel --target nrfusion_d3d10_external_bridge_tests
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 13 D3D10 bridge build failed.'
}

$testPath = Join-Path $BuildDir 'Release\nrfusion_d3d10_external_bridge_tests.exe'
if (-not (Test-Path -LiteralPath $testPath)) {
    throw 'Phase 13 D3D10 physical gate executable missing.'
}

$env:NRFUSION_TEST_D3D10_HARDWARE = '1'
try {
    & $testPath
    if ($LASTEXITCODE -ne 0) {
        throw "Phase 13 D3D10 physical gate failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item Env:NRFUSION_TEST_D3D10_HARDWARE -ErrorAction SilentlyContinue
}

Write-Host 'NRFusion Phase 13 physical D3D10.1/D3D11/D3D12 bridge gate passed.'
