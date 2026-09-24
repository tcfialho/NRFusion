param(
    [string]$BuildDir = '',
    [string]$VulkanHeaders = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDir) {
    $BuildDir = Join-Path $root 'build-phase11-hardware'
}

$previousHeaders = $env:NRFUSION_VULKAN_HEADERS
if ($VulkanHeaders) {
    $resolvedHeaders = [IO.Path]::GetFullPath($VulkanHeaders)
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedHeaders 'vulkan\vulkan.h'))) {
        throw 'VulkanHeaders must point to a directory containing vulkan\vulkan.h.'
    }
    $env:NRFUSION_VULKAN_HEADERS = $resolvedHeaders
}

try {
    & cmake -S $root -B $BuildDir -A x64
    if ($LASTEXITCODE -ne 0) {
        throw 'Phase 11 x64 configure failed.'
    }

    $targets = @(
        'nrfusion_vulkan_carrier_device_tests',
        'nrfusion_vulkan_external_interop_tests'
    )
    & cmake --build $BuildDir --config Release --parallel --target $targets
    if ($LASTEXITCODE -ne 0) {
        throw 'Phase 11 Vulkan hardware target build failed.'
    }

    $release = Join-Path $BuildDir 'Release'
    $carrier = Join-Path $release 'nrfusion_vulkan_carrier_device_tests.exe'
    $interop = Join-Path $release 'nrfusion_vulkan_external_interop_tests.exe'
    if (-not (Test-Path -LiteralPath $carrier)) {
        throw 'Vulkan carrier device gate executable missing.'
    }
    if (-not (Test-Path -LiteralPath $interop)) {
        throw 'Vulkan external interop gate executable missing.'
    }

    $env:NRFUSION_TEST_VULKAN_HARDWARE = '1'
    $env:NRFUSION_TEST_VULKAN_EXTERNAL_HARDWARE = '1'
    try {
        & $carrier
        if ($LASTEXITCODE -ne 0) {
            throw "Phase 11 Vulkan carrier device gate failed with exit code $LASTEXITCODE."
        }

        & $interop
        if ($LASTEXITCODE -ne 0) {
            throw "Phase 11 Vulkan external interop gate failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Remove-Item Env:NRFUSION_TEST_VULKAN_HARDWARE -ErrorAction SilentlyContinue
        Remove-Item Env:NRFUSION_TEST_VULKAN_EXTERNAL_HARDWARE -ErrorAction SilentlyContinue
    }
}
finally {
    if ($null -eq $previousHeaders) {
        Remove-Item Env:NRFUSION_VULKAN_HEADERS -ErrorAction SilentlyContinue
    }
    else {
        $env:NRFUSION_VULKAN_HEADERS = $previousHeaders
    }
}

Write-Host 'NRFusion Phase 11 physical Vulkan carrier + external interop gate passed.'
