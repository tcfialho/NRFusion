param(
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) {
    $BuildDir = Join-Path $root 'build-phase12-hardware'
}

& cmake -S $root -B $BuildDir -A x64
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 12 x64 configure failed.'
}

& cmake --build $BuildDir --config Release --parallel --target nrfusion_opengl_external_interop_tests
if ($LASTEXITCODE -ne 0) {
    throw 'Phase 12 OpenGL external interop build failed.'
}

$testPath = Join-Path $BuildDir 'Release\nrfusion_opengl_external_interop_tests.exe'
if (-not (Test-Path -LiteralPath $testPath)) {
    throw 'Phase 12 OpenGL physical gate executable missing.'
}

$env:NRFUSION_TEST_OPENGL_HARDWARE = '1'
try {
    & $testPath
    if ($LASTEXITCODE -ne 0) {
        throw "Phase 12 OpenGL physical gate failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item Env:NRFUSION_TEST_OPENGL_HARDWARE -ErrorAction SilentlyContinue
}

Write-Host 'NRFusion Phase 12 physical OpenGL external interop gate passed.'
