param(
    [string]$BuildDir = '',
    [string]$RuntimePath = '',
    [string]$ForwarderPath = '',
    [string]$ForwarderSha256 = '',
    [string]$RequiemUpscalerPath = '',
    [switch]$CompileInstaller
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$dist = Join-Path $root 'dist'

if (-not $BuildDir) {
    $BuildDir = Join-Path $root 'build-windows-validation\Release'
    if (-not (Test-Path -LiteralPath $BuildDir)) {
        $BuildDir = Join-Path $root 'build-windows-validation'
    }
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)

function Find-Tool([string]$Name, [string[]]$Candidates) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $Candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

function Find-MakeNSIS {
    return Find-Tool 'makensis.exe' @(
        'C:\Program Files (x86)\NSIS\makensis.exe',
        'C:\Program Files\NSIS\makensis.exe',
        'C:\w64devkit\bin\makensis.exe'
    )
}

function Copy-EnsureParent([string]$Source, [string]$Destination) {
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Test-Pe64([string]$Path) {
    try {
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { return $false }
        $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
        if ($peOffset -lt 0 -or $peOffset + 26 -gt $bytes.Length) { return $false }
        if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
            $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) { return $false }
        return [BitConverter]::ToUInt16($bytes, $peOffset + 24) -eq 0x20b
    }
    catch { return $false }
}

function Assert-Pe64([string]$Path, [string]$Role) {
    if (-not (Test-Pe64 $Path)) { throw "$Role nao e um PE x64 valido: $Path" }
}

function Assert-ApprovedRuntime([string]$Path) {
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    $approved = @(
        '6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927',
        '4C5BD1171C7336B4B04FB394DE51DA285AB6EAD6F922D7AFDEC163F71C319D74',
        '4B8D19BC3EFF58A084F5ECA7489C921501C203450169FB82FF4F649A4482BA05',
        'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E',
        'E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A'
    )
    if ($approved -notcontains $hash) { throw "Runtime DLSS-NR nao aprovado: $hash" }
}

# 0. Localizar Runtime DLSS-NR antes de limpar dist
if (-not $RuntimePath) {
    $existingRuntime = Join-Path $dist 'NRFusion\internal\nvngx_dlssnr.dll'
    if (Test-Path -LiteralPath $existingRuntime -PathType Leaf) {
        $backupRuntime = Join-Path $env:TEMP 'nrfusion_cached_nvngx_dlssnr.dll'
        Copy-Item -LiteralPath $existingRuntime -Destination $backupRuntime -Force
        $RuntimePath = $backupRuntime
    }
}
if (-not $RuntimePath) {
    $candidates = @(
        'D:\Users\tcfialho\Documents\dlss5-for-all\nvngx_dlssnr.dll',
        'D:\nrf-undelete\nvngx_dlssnr.dll'
    )
    foreach ($cand in $candidates) {
        if (Test-Path -LiteralPath $cand -PathType Leaf) {
            $RuntimePath = $cand
            break
        }
    }
}

# 1. Preparar diretório dist limpo
if (Test-Path -LiteralPath $dist) {
    try { Remove-Item -LiteralPath $dist -Recurse -Force -ErrorAction Stop }
    catch {
        Get-ChildItem -LiteralPath $dist -Force | ForEach-Object {
            try { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction Stop }
            catch { Write-Warning "Arquivo em uso: $($_.FullName)" }
        }
        if (@(Get-ChildItem -LiteralPath $dist -Force).Count -ne 0) {
            throw 'dist nao pode ser limpo com seguranca; pacote abortado.'
        }
    }
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# 2. Binários do código novo compilados
$probeExe = Join-Path $BuildDir 'NRFusionProbe.exe'
$hostExe = Join-Path $BuildDir 'NRFusionHost64.exe'
$proxyDll = Join-Path $BuildDir 'nrfusion_proxy.dll'

if (-not (Test-Path -LiteralPath $probeExe)) { throw "NRFusionProbe.exe nao encontrado em $BuildDir" }
if (-not (Test-Path -LiteralPath $hostExe)) { throw "NRFusionHost64.exe nao encontrado em $BuildDir" }
if (-not (Test-Path -LiteralPath $proxyDll)) { throw "nrfusion_proxy.dll nao encontrado em $BuildDir" }
Assert-Pe64 $probeExe 'NRFusionProbe.exe'
Assert-Pe64 $hostExe 'NRFusionHost64.exe'
Assert-Pe64 $proxyDll 'nrfusion_proxy.dll'

Copy-EnsureParent $probeExe (Join-Path $dist 'NRFusionProbe.exe')
Copy-EnsureParent $hostExe (Join-Path $dist 'NRFusion\internal\NRFusionHost64.exe')
Copy-EnsureParent $proxyDll (Join-Path $dist 'nrfusion_proxy.dll')
Write-Host 'Binarios standalone do novo codigo empacotados com sucesso.'

# 3. CUBIN SM89 e pesos W4A8
$cubin = Join-Path $root 'data\w4a8_ffn_sm89.cubin'
if (Test-Path -LiteralPath $cubin) {
    Copy-EnsureParent $cubin (Join-Path $dist 'NRFusion\internal\w4a8_ffn_sm89.cubin')
    Write-Host "CUBIN SM89 W4A8 empacotado: $cubin"
}

$w4a8Src = Join-Path $root 'data\w4a8'
if (Test-Path -LiteralPath $w4a8Src) {
    $w4a8Manifest = Join-Path $w4a8Src 'manifest.json'
    if (Test-Path -LiteralPath $w4a8Manifest) {
        $manifest = Get-Content -LiteralPath $w4a8Manifest -Raw | ConvertFrom-Json
        if (@($manifest.failed).Count -ne 0) {
            throw "Blocos W4A8 reprovados: $(@($manifest.failed) -join ', ')"
        }
    }
    Copy-Item -LiteralPath $w4a8Src -Destination (Join-Path $dist 'NRFusion\internal\w4a8') -Recurse -Force
    Write-Host 'Containers W4A8 empacotados.'
}

# 4. Compatibilidade e Testbed Requiem
$compatJson = Join-Path $root 'compat\games.json'
if (Test-Path -LiteralPath $compatJson) {
    Copy-EnsureParent $compatJson (Join-Path $dist 'NRFusion\compat\games.json')
}

$requiemExe = Join-Path $BuildDir 'RequiemGame.exe'
if (Test-Path -LiteralPath $requiemExe) {
    Copy-EnsureParent $requiemExe (Join-Path $dist 'RequiemGame\RequiemGame.exe')
    Copy-EnsureParent $proxyDll (Join-Path $dist 'RequiemGame\version.dll')
    if (-not $RequiemUpscalerPath) {
        $requiemUpscalerCandidates = @(
            'D:\Users\tcfialho\Documents\dlss5-for-all\nvngx_dlss.dll'
        )
        $RequiemUpscalerPath = $requiemUpscalerCandidates |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
    }
    if (-not $RequiemUpscalerPath) {
        throw 'RequiemGame foi compilado, mas nvngx_dlss.dll nao foi localizado.'
    }
    Assert-Pe64 $RequiemUpscalerPath 'Requiem DLSS-SR runtime'
    Copy-EnsureParent $RequiemUpscalerPath (Join-Path $dist 'RequiemGame\nvngx_dlss.dll')
    $assets = Join-Path $root 'tools\requiem_game\assets'
    if (Test-Path -LiteralPath $assets) {
        Copy-Item -LiteralPath $assets -Destination (Join-Path $dist 'RequiemGame\assets') -Recurse -Force
    }
}

$notice = Join-Path $root 'THIRD_PARTY.md'
$licenses = Join-Path $root 'licenses'
if (-not (Test-Path -LiteralPath $notice -PathType Leaf)) { throw 'THIRD_PARTY.md ausente.' }
if (-not (Test-Path -LiteralPath $licenses -PathType Container)) { throw 'diretorio licenses ausente.' }
Copy-EnsureParent $notice (Join-Path $dist 'NRFusion.NOTICE.txt')
Copy-Item -LiteralPath $licenses -Destination (Join-Path $dist 'Licenses') -Recurse -Force

# 5. Sidecars para o Host64 (runtime NVIDIA e bridge nativo NRFusion)
if (-not $RuntimePath) {
    $existingRuntime = Join-Path $dist 'NRFusion\internal\nvngx_dlssnr.dll'
    if (Test-Path -LiteralPath $existingRuntime -PathType Leaf) {
        $RuntimePath = $existingRuntime
    }
}
if (-not $ForwarderPath) {
    $builtBridge = Join-Path $BuildDir 'nvngx.dll_dlssnr.dll'
    if (-not (Test-Path -LiteralPath $builtBridge -PathType Leaf)) {
        $builtBridge = Join-Path $BuildDir 'Release\nvngx.dll_dlssnr.dll'
    }
    if (Test-Path -LiteralPath $builtBridge -PathType Leaf) {
        $ForwarderPath = $builtBridge
    }
}

if ($RuntimePath -and -not (Test-Path -LiteralPath $RuntimePath -PathType Leaf)) {
    throw "RuntimePath nao encontrado: $RuntimePath"
}
if ($ForwarderPath -and -not (Test-Path -LiteralPath $ForwarderPath -PathType Leaf)) {
    throw "ForwarderPath nao encontrado: $ForwarderPath"
}
if ($ForwarderSha256 -and $ForwarderSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'ForwarderSha256 deve ser um SHA-256 hexadecimal de 64 caracteres.'
}
if ($RuntimePath) {
    Assert-Pe64 $RuntimePath 'Runtime DLSS-NR'
    Assert-ApprovedRuntime $RuntimePath
    $destRuntime = Join-Path $dist 'NRFusion\internal\nvngx_dlssnr.dll'
    if ([IO.Path]::GetFullPath($RuntimePath) -ne [IO.Path]::GetFullPath($destRuntime)) {
        Copy-EnsureParent $RuntimePath $destRuntime
    }
    Write-Host "Runtime DLSS-NR empacotado: $RuntimePath"
}

if ($ForwarderPath) {
    Assert-Pe64 $ForwarderPath 'Bridge/Forwarder DLSS-NR'
    if ($ForwarderSha256) {
        $forwarderHash = (Get-FileHash -LiteralPath $ForwarderPath -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($forwarderHash -ne $ForwarderSha256.ToUpperInvariant()) {
            throw "Bridge DLSS-NR nao corresponde ao SHA-256 aprovado: $forwarderHash"
        }
    }
    Copy-EnsureParent $ForwarderPath (Join-Path $dist 'NRFusion\internal\nvngx.dll_dlssnr.dll')
    Write-Host "Bridge DLSS-NR empacotado: $ForwarderPath"
}

if ($CompileInstaller -and (-not $RuntimePath -or -not $ForwarderPath)) {
    throw 'CompileInstaller requer RuntimePath e Bridge DLSS-NR para o Host64.'
}


# 6. Gerar manifesto SHA256SUMS.txt
$manifestPath = Join-Path $dist 'SHA256SUMS.txt'
$files = Get-ChildItem -LiteralPath $dist -Recurse -File | Where-Object { $_.FullName -ne $manifestPath }
$lines = $files | ForEach-Object {
    $relative = $_.FullName.Substring($dist.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
} | Sort-Object
Set-Content -LiteralPath $manifestPath -Value $lines -Encoding ascii
Write-Host "Manifesto SHA256SUMS.txt gerado com $($lines.Count) arquivos."

# 7. Compilar instalador final via NSIS
if ($CompileInstaller) {
    $makensis = Find-MakeNSIS
    if (-not $makensis) { throw 'makensis.exe nao encontrado. Instale o NSIS 3.' }

    $installerDir = Join-Path $root 'installer'
    $setupTemp = Join-Path $installerDir 'NRFusionSetup.exe'
    if (Test-Path -LiteralPath $setupTemp) { Remove-Item -LiteralPath $setupTemp -Force }

    Write-Host "Compilando instalador NSIS em $installerDir..."
    Push-Location $installerDir
    try {
        & $makensis /V2 NRFusion.nsi
        if ($LASTEXITCODE -ne 0) { throw "makensis falhou com codigo $LASTEXITCODE" }
    }
    finally { Pop-Location }

    if (-not (Test-Path -LiteralPath $setupTemp)) {
        throw 'NSIS nao gerou NRFusionSetup.exe.'
    }

    $setupFinal = Join-Path $dist 'NRFusionSetup.exe'
    Move-Item -LiteralPath $setupTemp -Destination $setupFinal -Force
    $sizeMB = [math]::Round((Get-Item $setupFinal).Length / 1MB, 2)
    $hash = (Get-FileHash -LiteralPath $setupFinal -Algorithm SHA256).Hash
    Write-Host "INSTALADOR CONCLUIDO COM SUCESSO!"
    Write-Host "Localizacao: $setupFinal"
    Write-Host "Tamanho: $sizeMB MB"
    Write-Host "SHA256: $hash"
}
