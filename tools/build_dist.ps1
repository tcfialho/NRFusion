param(
    [string]$RuntimePath = '',
    [string]$AdaRuntimePath = '',
    [switch]$EnableAdaRuntime,
    [string]$UpstreamCache = '',
    [switch]$AutoFetchRuntime,
    [switch]$CompileInstaller,
    [switch]$KeepWorktree
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lockPath = Join-Path $root 'upstreams.lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$repoUrl = [string]$lock.build.optiscaler.repository
$commit = [string]$lock.build.optiscaler.commit
if ($commit -notmatch '^[0-9a-fA-F]{40}$') { throw 'upstreams.lock.json contains an invalid OptiScaler commit.' }

if (-not $UpstreamCache) { $UpstreamCache = Join-Path $root 'upstreams\wilsjo' }
$UpstreamCache = [IO.Path]::GetFullPath($UpstreamCache)
$workRoot = Join-Path $root '.build'
$worktree = Join-Path $workRoot ('optiscaler-' + $commit.Substring(0, 12))
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null

function Invoke-Checked([string]$Exe, [string[]]$Arguments, [string]$WorkingDirectory = '') {
    if ($WorkingDirectory) { Push-Location $WorkingDirectory }
    try {
        & $Exe @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Exe exited with code $LASTEXITCODE" }
    }
    finally { if ($WorkingDirectory) { Pop-Location } }
}

function Find-MSBuild {
    $cmd = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
      'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
      'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
      'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
      'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

function Find-MakeNSIS {
    $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
      'C:\Program Files (x86)\NSIS\makensis.exe',
      'C:\Program Files\NSIS\makensis.exe'
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

if (-not (Test-Path -LiteralPath (Join-Path $UpstreamCache '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $UpstreamCache) | Out-Null
    Invoke-Checked 'git' @('clone', '--filter=blob:none', '--no-checkout', $repoUrl, $UpstreamCache)
}

# Reproducible/offline-friendly: do not contact the network when the locked object is already cached.
& git -C $UpstreamCache cat-file -e ($commit + '^{commit}') 2>$null
if ($LASTEXITCODE -ne 0) {
    Invoke-Checked 'git' @('-C', $UpstreamCache, 'fetch', '--depth=1', 'origin', $commit)
    Invoke-Checked 'git' @('-C', $UpstreamCache, 'cat-file', '-e', ($commit + '^{commit}'))
}

if (Test-Path -LiteralPath $worktree) {
    & git -C $UpstreamCache worktree remove --force $worktree 2>$null
    if (Test-Path -LiteralPath $worktree) { Remove-Item -LiteralPath $worktree -Recurse -Force }
}
Invoke-Checked 'git' @('-C', $UpstreamCache, 'worktree', 'add', '--detach', $worktree, $commit)
Invoke-Checked 'git' @('-C', $worktree, 'submodule', 'update', '--init', '--depth', '1')

try {
    $pythonCmd = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $pythonCmd) { $pythonCmd = Get-Command python -ErrorAction SilentlyContinue }
    $python = if ($pythonCmd) { $pythonCmd.Source } else { '' }
    if (-not $python) { throw 'Python 3 is required to apply NRFusion integration.' }
    Invoke-Checked $python @((Join-Path $root 'tools\apply_to_optiscaler.py'), $worktree)

    $msbuild = Find-MSBuild
    if (-not $msbuild) { throw 'MSBuild.exe not found. Install Visual Studio 2022 C++ Build Tools.' }
    Invoke-Checked $msbuild @((Join-Path $worktree 'OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj'), '/p:Configuration=Release', '/p:Platform=x64', '/m', '/v:minimal')
    Invoke-Checked $msbuild @((Join-Path $worktree 'OptiScaler.sln'), '/p:Configuration=Release', '/p:Platform=x64', '/m', '/v:minimal')

    $probeBuild = Join-Path $workRoot 'nrfusion-probe'
    Invoke-Checked 'cmake' @('-S', $root, '-B', $probeBuild, '-DCMAKE_BUILD_TYPE=Release')
    Invoke-Checked 'cmake' @('--build', $probeBuild, '--config', 'Release', '--target', 'nrfusion_probe')

    if (Test-Path -LiteralPath $dist) {
        # A packaging run must not be lost because a shell, an explorer window or a testbed
        # still has the folder open. Removing the contents achieves the same clean slate, and
        # what cannot be removed is reported rather than silently left behind.
        try { Remove-Item -LiteralPath $dist -Recurse -Force -ErrorAction Stop }
        catch {
            Get-ChildItem -LiteralPath $dist -Force | ForEach-Object {
                try { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction Stop }
                catch { Write-Warning "nao consegui remover $($_.FullName): em uso" }
            }
        }
    }
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    $upstreamOut = Join-Path $worktree 'x64\Release\a'
    $required = @(
      (Join-Path $upstreamOut 'OptiScaler.dll'),
      (Join-Path $worktree 'OptiScaler.ini')
    )
    foreach ($f in $required) { if (-not (Test-Path -LiteralPath $f)) { throw "Required build output missing: $f" } }

    Copy-Item -LiteralPath (Join-Path $upstreamOut 'OptiScaler.dll') -Destination (Join-Path $dist 'OptiScaler.dll')
    Copy-Item -LiteralPath (Join-Path $worktree 'OptiScaler.ini') -Destination (Join-Path $dist 'OptiScaler.ini')

    $forwarder = Join-Path $worktree 'OptiScaler\dlssnr\forwarder\x64\Release\a\nvngx.dll_dlssnr.dll'
    if (-not (Test-Path -LiteralPath $forwarder)) { $forwarder = Join-Path $upstreamOut 'nvngx.dll_dlssnr.dll' }
    if (-not (Test-Path -LiteralPath $forwarder)) { throw 'DLSS-NR forwarder build output was not found.' }
    Copy-Item -LiteralPath $forwarder -Destination (Join-Path $dist 'nvngx.dll_dlssnr.dll')

    foreach ($dirName in @('OptiScaler', 'Licenses')) {
        $srcDir = Join-Path $upstreamOut $dirName
        if ($dirName -eq 'OptiScaler' -and -not (Test-Path -LiteralPath $srcDir -PathType Container)) {
            throw "Required runtime directory missing: $srcDir"
        }
        if (Test-Path -LiteralPath $srcDir -PathType Container) {
            Copy-Item -LiteralPath $srcDir -Destination (Join-Path $dist $dirName) -Recurse
        }
    }

    . (Join-Path $PSScriptRoot 'build_dist_w4a8.ps1')

    $probeCandidates = @(
      (Join-Path $probeBuild 'Release\NRFusionProbe.exe'),
      (Join-Path $probeBuild 'NRFusionProbe.exe')
    )
    $probe = $probeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $probe) { throw 'NRFusionProbe.exe was not produced.' }
    Copy-Item -LiteralPath $probe -Destination (Join-Path $dist 'NRFusionProbe.exe')

    . (Join-Path $PSScriptRoot 'build_dist_carrier_testbed.ps1')

    Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY.md') -Destination (Join-Path $dist 'NRFusion.NOTICE.txt')
    $compatDist = Join-Path $dist 'NRFusion\compat'
    New-Item -ItemType Directory -Force -Path $compatDist | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'compat\games.json') -Destination (Join-Path $compatDist 'games.json')

    if (-not $RuntimePath -and ($AutoFetchRuntime -or $CompileInstaller)) {
        $fetchScript = Join-Path $PSScriptRoot 'fetch_dlssnr.ps1'
        $fetchedDll = Join-Path $root 'data\nvngx_dlssnr.dll'
        & $fetchScript -DestinationPath $fetchedDll -Tag 'dlssnr-310.8.SF-v2'
        if (Test-Path -LiteralPath $fetchedDll -PathType Leaf) {
            $RuntimePath = $fetchedDll
        }
    }

    if ($RuntimePath) {
        $RuntimePath = [IO.Path]::GetFullPath($RuntimePath)
        if (-not (Test-Path -LiteralPath $RuntimePath -PathType Leaf)) { throw "Runtime file not found: $RuntimePath" }
        $hash = (Get-FileHash -LiteralPath $RuntimePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $allowed = @(
          '6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927', # dlssnr-310.8.SF-v2 (RTX 20/30/40/50 Multi-Gen)
          '4C5BD1171C7336B4B04FB394DE51DA285AB6EAD6F922D7AFDEC163F71C319D74', # dlssnr-310.8.SF (RTX 20/30/40 Multi-Gen)
          '4B8D19BC3EFF58A084F5ECA7489C921501C203450169FB82FF4F649A4482BA05', # dlssnr-310.8.0-RTX40 (RTX 40 only)
          'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E', # dlssnr-310.8.0 (RTX 50 only original)
          'E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A'
        )
        if ($allowed -notcontains $hash) { throw "Unrecognized nvngx_dlssnr.dll SHA-256: $hash" }
        Copy-Item -LiteralPath $RuntimePath -Destination (Join-Path $dist 'nvngx_dlssnr.dll')
        Write-Host "Private runtime accepted: $hash"
    }
    else {
        Write-Warning 'nvngx_dlssnr.dll was not supplied. dist is a public/developer package, not a self-contained end-user installer.'
    }

    if ($AdaRuntimePath -and -not $RuntimePath) {
        throw 'AdaRuntimePath requires RuntimePath so non-Ada systems keep the original FP8 fallback.'
    }

    if ($EnableAdaRuntime -and -not $AdaRuntimePath) {
        throw 'EnableAdaRuntime requires AdaRuntimePath.'
    }

    if ($AdaRuntimePath) {
        $AdaRuntimePath = [IO.Path]::GetFullPath($AdaRuntimePath)
        if (-not (Test-Path -LiteralPath $AdaRuntimePath -PathType Leaf)) {
            throw "Ada runtime file not found: $AdaRuntimePath"
        }
        $adaHash = (Get-FileHash -LiteralPath $AdaRuntimePath -Algorithm SHA256).Hash.ToUpperInvariant()
        $adaAllowed = @(
          'CB1A1A6E47E38B9229DB4F22081C0F9B1271783599675E0AB13D41D4D039755A' # local Ada/SM89 FP8 rebuild
        )
        if ($adaAllowed -notcontains $adaHash) {
            throw "Unrecognized Ada runtime SHA-256: $adaHash"
        }
        Copy-Item -LiteralPath $AdaRuntimePath -Destination (Join-Path $dist 'nvngx_dlssnr_ada.dll')
        Write-Host "Private Ada FP8 sidecar accepted: $adaHash"
    }

    if ($EnableAdaRuntime) {
        $distIni = Join-Path $dist 'OptiScaler.ini'
        $distIniText = Get-Content -LiteralPath $distIni -Raw
        if ($distIniText -match '(?m)^AdaRuntime\s*=') {
            $distIniText = [regex]::Replace($distIniText, '(?m)^AdaRuntime\s*=.*$', 'AdaRuntime=true')
        }
        else {
            $distIniText = $distIniText -replace '(?m)^\[DlssNr\]\s*$', "[DlssNr]`r`nAdaRuntime=true"
        }
        Set-Content -LiteralPath $distIni -Value $distIniText -Encoding UTF8
        Write-Host 'Private Ada FP8 sidecar enabled explicitly for this build.'
    }

    $testbed = Join-Path $dist 'RequiemGame'
    if (Test-Path -LiteralPath $testbed) {
        foreach ($name in @('OptiScaler.dll', 'OptiScaler.ini', 'nvngx.dll_dlssnr.dll', 'nvngx_dlssnr.dll', 'w4a8_ffn_sm89.cubin', 'weights_sm89.bin')) {
            $source = Join-Path $dist $name
            if (Test-Path -LiteralPath $source) {
                # OptiScaler answers as the graphics proxy; dxgi.dll is the name a D3D12
                # executable loads without being told to.
                $target = if ($name -eq 'OptiScaler.dll') { 'dxgi.dll' } else { $name }
                Copy-Item -LiteralPath $source -Destination (Join-Path $testbed $target) -Force
            }
        }
        $optiRuntime = Join-Path $dist 'OptiScaler'
        if (Test-Path -LiteralPath $optiRuntime -PathType Container) {
            Copy-Item -LiteralPath $optiRuntime -Destination (Join-Path $testbed 'OptiScaler') -Recurse -Force
        }
    }

    $manifestPath = Join-Path $dist 'SHA256SUMS.txt'
    $lines = Get-ChildItem -LiteralPath $dist -Recurse -File | Where-Object { $_.FullName -ne $manifestPath } | ForEach-Object {
        $relative = $_.FullName.Substring($dist.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    } | Sort-Object
    Set-Content -LiteralPath $manifestPath -Value $lines -Encoding ascii

    if ($CompileInstaller) {
        if (-not $RuntimePath) {
            throw 'A compiled end-user installer must be self-contained. Supply -RuntimePath with an approved nvngx_dlssnr.dll.'
        }
        $makensis = Find-MakeNSIS
        if (-not $makensis) { throw 'makensis.exe not found. Install NSIS 3 or omit -CompileInstaller.' }
        $installerDir = Join-Path $root 'installer'
        $setupTemp = Join-Path $installerDir 'NRFusionSetup.exe'
        if (Test-Path -LiteralPath $setupTemp) { Remove-Item -LiteralPath $setupTemp -Force }
        Invoke-Checked $makensis @('/V2', 'NRFusion.nsi') $installerDir
        if (-not (Test-Path -LiteralPath $setupTemp)) { throw 'NSIS did not produce NRFusionSetup.exe.' }
        Move-Item -LiteralPath $setupTemp -Destination (Join-Path $dist 'NRFusionSetup.exe') -Force
    }

    Write-Host "NRFusion dist ready: $dist"
    Write-Host "OptiScaler locked at: $commit"
}
finally {
    if (-not $KeepWorktree -and (Test-Path -LiteralPath $worktree)) {
        & git -C $UpstreamCache worktree remove --force $worktree 2>$null
        if (Test-Path -LiteralPath $worktree) { Remove-Item -LiteralPath $worktree -Recurse -Force }
    }
}
