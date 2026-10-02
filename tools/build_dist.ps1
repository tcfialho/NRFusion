param(
    [string]$BuildDir = '',
    [string]$RuntimePath = '',
    [string]$ForwarderPath = '',
    [string]$ForwarderSha256 = '',
    [switch]$CompileInstaller,
    [switch]$CompileEndUserInstaller,
    [switch]$AutoFetchRuntime,
    [string]$AdaRuntimePath = '',
    [switch]$EnableAdaRuntime
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($AutoFetchRuntime -or $AdaRuntimePath -or $EnableAdaRuntime) {
    throw 'The retired distribution options are unsupported. Supply approved RuntimePath and ForwarderPath to package_standalone_dist.ps1.'
}

if ($CompileEndUserInstaller) { $CompileInstaller = $true }

$package = Join-Path $PSScriptRoot 'package_standalone_dist.ps1'
$parameters = @{}
if ($BuildDir) { $parameters['BuildDir'] = $BuildDir }
if ($RuntimePath) { $parameters['RuntimePath'] = $RuntimePath }
if ($ForwarderPath) { $parameters['ForwarderPath'] = $ForwarderPath }
if ($ForwarderSha256) { $parameters['ForwarderSha256'] = $ForwarderSha256 }
if ($CompileInstaller) { $parameters['CompileInstaller'] = $true }

& $package @parameters
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
