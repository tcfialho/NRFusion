param(
    [string]$X64BuildDir = '',
    [string]$Win32BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $X64BuildDir) {
    $X64BuildDir = Join-Path $root 'build-phase10-x64'
}
if (-not $Win32BuildDir) {
    $Win32BuildDir = Join-Path $root 'build-phase10-win32'
}

& cmake -S $root -B $X64BuildDir -A x64
if ($LASTEXITCODE -ne 0) { throw 'Phase 10 x64 configure failed.' }
& cmake --build $X64BuildDir --config Release --parallel --target nrfusion_host64
if ($LASTEXITCODE -ne 0) { throw 'Phase 10 Host64 build failed.' }

& cmake -S $root -B $Win32BuildDir -A Win32
if ($LASTEXITCODE -ne 0) { throw 'Phase 10 Win32 configure failed.' }
& cmake --build $Win32BuildDir --config Release --parallel --target nrfusion_capture32_roundtrip_test
if ($LASTEXITCODE -ne 0) { throw 'Phase 10 Win32 capture build failed.' }

$hostPath = Join-Path $X64BuildDir 'Release\NRFusionHost64.exe'
$roundtripPath = Join-Path $Win32BuildDir 'Release\nrfusion_capture32_roundtrip_test.exe'
if (-not (Test-Path -LiteralPath $hostPath)) {
    throw 'NRFusionHost64.exe missing after x64 build.'
}
if (-not (Test-Path -LiteralPath $roundtripPath)) {
    throw 'Win32 capture roundtrip executable missing.'
}

$env:NRFUSION_HOST64_PATH = $hostPath
try {
    & $roundtripPath
    if ($LASTEXITCODE -ne 0) {
        throw 'Phase 10 Win32-to-x64 roundtrip failed.'
    }
}
finally {
    Remove-Item Env:NRFUSION_HOST64_PATH -ErrorAction SilentlyContinue
}

Write-Host 'NRFusion Phase 10 Win32-to-x64 hardware gate passed.'
