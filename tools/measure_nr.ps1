# Bounded original-path diagnostic; summary explicitly separates NGX timing from NR.
param(
    [string]$Testbed = 'dist\RequiemGame\RequiemGame.exe',
    [int]$Frames = 720,
    [int]$Warmup = 120,
    [ValidateSet(720,1080)][int]$Height = 720,
    [switch]$ReferenceOn,
    [switch]$FixedScene,
    [switch]$DeterministicMotion,
    [string]$Out = '.temp\medicao'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($FixedScene -and $DeterministicMotion) { throw 'Choose FixedScene or DeterministicMotion.' }
$arguments = @(
    (Join-Path $PSScriptRoot 'measure_requiem.py'),
    '--testbed', (Join-Path $root $Testbed),
    '--frames', $Frames, '--warmup', $Warmup, '--height', $Height,
    '--out', (Join-Path $root $Out)
)
if ($ReferenceOn) { $arguments += '--reference-on' }
if ($DeterministicMotion) { $arguments += '--motion' }
& python @arguments
exit $LASTEXITCODE
