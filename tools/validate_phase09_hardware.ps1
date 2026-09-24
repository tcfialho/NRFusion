param(
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build-phase09-hardware' }

& cmake -S $root -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) { throw 'Phase 09 configure failed.' }

$targets = @(
    'nrfusion_d3d11_carrier_native_acquire_tests',
    'nrfusion_synthetic_dx11_bridge_test',
    'nrfusion_capture32_d3d11_hook_test',
    'nrfusion_host64'
)
& cmake --build $BuildDir --config Release --parallel --target $targets
if ($LASTEXITCODE -ne 0) { throw 'Phase 09 target build failed.' }

$release = Join-Path $BuildDir 'Release'
$hostPath = Join-Path $release 'NRFusionHost64.exe'
$hook = Join-Path $release 'nrfusion_capture32_d3d11_hook_test.exe'
if (-not (Test-Path -LiteralPath $hostPath)) { throw 'NRFusionHost64.exe missing.' }
if (-not (Test-Path -LiteralPath $hook)) { throw 'D3D11 hook gate executable missing.' }

$env:NRFUSION_TEST_D3D11_HARDWARE = '1'
try {
    & ctest --test-dir $BuildDir -C Release --output-on-failure `
        -R '^(nrfusion_d3d11_carrier_native_acquire_tests|nrfusion_synthetic_dx11_bridge_test)$'
    if ($LASTEXITCODE -ne 0) { throw 'Phase 09 D3D11 hardware carrier tests failed.' }

    $env:NRFUSION_HOST64_PATH = $hostPath
    & $hook
    if ($LASTEXITCODE -ne 0) { throw 'Phase 09 x64 D3D11 hook gate failed.' }
}
finally {
    Remove-Item Env:NRFUSION_TEST_D3D11_HARDWARE -ErrorAction SilentlyContinue
    Remove-Item Env:NRFUSION_HOST64_PATH -ErrorAction SilentlyContinue
}

Write-Host 'NRFusion Phase 09 physical D3D11 gate passed.'
