param(
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) {
    $BuildDir = Join-Path $root 'build-phase14-hardware'
}

& cmake -S $root -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 14 x64 configure failed.'
}

& cmake --build $BuildDir --config Release --parallel --target nrfusion_d3d9ex_share_tests
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 14 D3D9Ex bridge build failed.'
}

$testPath = Join-Path $BuildDir 'Release\nrfusion_d3d9ex_share_tests.exe'
if (-not (Test-Path -LiteralPath $testPath)) {
    throw 'Phase 14 D3D9Ex physical gate executable missing.'
}

$env:NRFUSION_TEST_D3D9EX_HARDWARE = '1'
try {
    & $testPath
    if ($LASTEXITCODE -ne 0) {
        throw "Phase 14 D3D9Ex physical gate failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item Env:NRFUSION_TEST_D3D9EX_HARDWARE -ErrorAction SilentlyContinue
}

Write-Host 'NRFusion Phase 14 physical D3D9Ex carrier proof gate passed.'
