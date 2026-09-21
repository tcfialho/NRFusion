param(
    [string]$RuntimePath = '',
    [string]$AdaRuntimePath = '',
    [switch]$EnableAdaRuntime,
    [switch]$AutoFetchRuntime,
    [switch]$CompileEndUserInstaller,
    [switch]$Fast,
    [string]$TargetsCsv = 'nrfusion_core',
    [string]$TestRegex = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Find-MakeNSIS {
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
      'C:\Program Files (x86)\NSIS\makensis.exe',
      'C:\Program Files\NSIS\makensis.exe'
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

$build = Join-Path $root 'build-windows-validation'
& cmake -S $root -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

if ($Fast) {
    $targets = @($TargetsCsv.Split(',') |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ })
    if ($targets.Count -eq 0) { throw '-Fast requires at least one CMake target.' }

    $buildArgs = @('--build', $build, '--config', 'Release', '--parallel', '--target') + $targets
    & cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Fast Windows target build failed.' }

    if ($TestRegex) {
        & ctest --test-dir $build -C Release --output-on-failure -R $TestRegex
        if ($LASTEXITCODE -ne 0) { throw 'Fast Windows targeted tests failed.' }
    }

    Write-Host 'NRFusion Windows fast validation passed.'
    exit 0
}

& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Portable Windows build failed.' }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Portable Windows tests failed.' }

$distParams = @{}
if ($RuntimePath) { $distParams['RuntimePath'] = $RuntimePath }
if ($AdaRuntimePath) { $distParams['AdaRuntimePath'] = $AdaRuntimePath }
if ($EnableAdaRuntime) { $distParams['EnableAdaRuntime'] = $true }
if ($AutoFetchRuntime) { $distParams['AutoFetchRuntime'] = $true }
if ($CompileEndUserInstaller) {
    if (-not $RuntimePath -and -not $AutoFetchRuntime) {
        throw '-CompileEndUserInstaller requires -RuntimePath or -AutoFetchRuntime.'
    }
    $distParams['CompileInstaller'] = $true
}
& (Join-Path $root 'tools\build_dist.ps1') @distParams
if ($LASTEXITCODE -ne 0) { throw 'Integrated NRFusion distribution build failed.' }

if (-not $CompileEndUserInstaller) {
    $makensis = Find-MakeNSIS
    if (-not $makensis) { throw 'makensis.exe not found; install NSIS 3.' }
    $validationOut = Join-Path $root '.build\installer-validation'
    New-Item -ItemType Directory -Force -Path $validationOut | Out-Null
    $installerDir = Join-Path $root 'installer'
    $setupTemp = Join-Path $installerDir 'NRFusionSetup.exe'
    if (Test-Path -LiteralPath $setupTemp) { Remove-Item -LiteralPath $setupTemp -Force }
    Push-Location $installerDir
    try {
        & $makensis /V2 NRFusion.nsi
        if ($LASTEXITCODE -ne 0) { throw 'NSIS validation compile failed.' }
    }
    finally { Pop-Location }
    if (-not (Test-Path -LiteralPath $setupTemp)) {
        throw 'NSIS validation did not produce NRFusionSetup.exe.'
    }
    Move-Item -LiteralPath $setupTemp -Destination (Join-Path $validationOut 'NRFusionSetup.exe') -Force
}

Write-Host 'NRFusion Windows validation passed.'
