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

    # Ada SM89 W4A8 assets: CUBIN and weights_sm89.bin
    # O CUBIN compilado fica em .build\cuda; uma copia versionada pode existir em data\ para que
    # a pipeline (que nao tem CUDA Toolkit) tambem consiga empacotar a aceleracao Ada.
    $cubinSrc = Join-Path $root '.build\cuda\w4a8_ffn_sm89.cubin'
    if (-not (Test-Path -LiteralPath $cubinSrc)) {
        $cubinSrc = Join-Path $root 'data\w4a8_ffn_sm89.cubin'
    }
    $weightsSrc = Join-Path $root 'data\pesos\weights_sm89.bin'

    # The CUBIN is a build output, not a stored asset. Every package built before this silently
    # shipped without Ada acceleration because nobody had run the CUDA script by hand first.
    if (-not (Test-Path -LiteralPath $cubinSrc)) {
        $cudaScript = Join-Path $PSScriptRoot 'build_cuda_backend.ps1'
        $haveToolkit = Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' -Directory -EA SilentlyContinue
        if ($haveToolkit -and (Test-Path -LiteralPath $cudaScript)) {
            Write-Host 'Ada SM89 W4A8 CUBIN missing; building it now.'
            & $cudaScript
        }
    }

    if (Test-Path -LiteralPath $cubinSrc) {
        Copy-Item -LiteralPath $cubinSrc -Destination (Join-Path $dist 'OptiScaler\w4a8_ffn_sm89.cubin') -Force
        Copy-Item -LiteralPath $cubinSrc -Destination (Join-Path $dist 'w4a8_ffn_sm89.cubin') -Force
        Write-Host "Ada SM89 W4A8 CUBIN packaged into dist: $cubinSrc"
    }
    else {
        Write-Warning 'Ada SM89 W4A8 CUBIN ausente: o pacote sai sem aceleracao Ada. Instale o CUDA Toolkit e rode tools\build_cuda_backend.ps1.'
    }

    # Um container por bloco. Dezesseis blocos usam a mesma forma e nenhum deles usa os mesmos
    # pesos, entao empacotar um arquivo so entrega quinze blocos com os pesos do errado.
    $w4a8Dir = Join-Path $root 'dist\w4a8'
    $manifestPath = Join-Path $w4a8Dir 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        $cachedW4a8 = Join-Path $root 'data\w4a8'
        if (Test-Path -LiteralPath (Join-Path $cachedW4a8 'manifest.json')) {
            Copy-Item -LiteralPath $cachedW4a8 -Destination $dist -Recurse -Force
        }
        else {
            $logical = Join-Path $root 'data\pesos\logical.safetensors'
            if (Test-Path -LiteralPath $logical) {
                Write-Host 'Containers W4A8 ausentes; gerando agora.'
                & python (Join-Path $PSScriptRoot 'build_w4a8_containers.py') $logical -o $w4a8Dir
            }
        }
    }

    if (Test-Path -LiteralPath $manifestPath) {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        # Um bloco que nunca passou na fidelidade nao pode sair no pacote: em jogo ele apareceria
        # como imagem corrompida, nao como ausencia de aceleracao.
        if (@($manifest.failed).Count -gt 0) {
            throw "Blocos W4A8 reprovados na fidelidade: $(@($manifest.failed) -join ', '). Nada e empacotado."
        }
        $targetDirs = @((Join-Path $dist 'OptiScaler\w4a8'), (Join-Path $dist 'w4a8'))
        foreach ($target in $targetDirs) {
            New-Item -ItemType Directory -Force -Path $target | Out-Null
            $destManifest = Join-Path $target (Split-Path -Leaf $manifestPath)
            if ([IO.Path]::GetFullPath($destManifest) -ne [IO.Path]::GetFullPath($manifestPath)) {
                Copy-Item -LiteralPath $manifestPath -Destination $target -Force
            }
        }
        foreach ($entry in $manifest.blocks.PSObject.Properties) {
            $file = Join-Path $w4a8Dir $entry.Value.file
            if (-not (Test-Path -LiteralPath $file)) {
                throw "Container do bloco $($entry.Name) listado no manifesto mas ausente em $file."
            }
            foreach ($target in $targetDirs) {
                $destFile = Join-Path $target (Split-Path -Leaf $file)
                if ([IO.Path]::GetFullPath($destFile) -ne [IO.Path]::GetFullPath($file)) {
                    Copy-Item -LiteralPath $file -Destination $target -Force
                }
            }
        }
        $blockCount = @($manifest.blocks.PSObject.Properties).Count
        Write-Host "Ada SM89 W4A8: $blockCount containers empacotados."
    }
    elseif (Test-Path -LiteralPath $weightsSrc) {
        Copy-Item -LiteralPath $weightsSrc -Destination (Join-Path $dist 'OptiScaler\weights_sm89.bin') -Force
        Copy-Item -LiteralPath $weightsSrc -Destination (Join-Path $dist 'weights_sm89.bin') -Force
        Write-Host "Ada SM89 W4A8 weights container packaged into dist: $weightsSrc"
    }
    else {
        Write-Warning "Ada SM89 W4A8 sem containers: o pacote sai sem aceleracao Ada. Gere com tools/build_w4a8_containers.py."
    }

    $probeCandidates = @(
      (Join-Path $probeBuild 'Release\NRFusionProbe.exe'),
      (Join-Path $probeBuild 'NRFusionProbe.exe')
    )
    $probe = $probeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $probe) { throw 'NRFusionProbe.exe was not produced.' }
    Copy-Item -LiteralPath $probe -Destination (Join-Path $dist 'NRFusionProbe.exe')

    # x86-carrier transport: a 32-bit game never loads OptiScaler.dll. It loads
    # nrfusion_capture32.dll (built Win32, so it needs its own -A Win32 configure -- $probeBuild is
    # x64) as a proxy DLL, which then drives a plain, real x64 process, NRFusionHost64.exe, built
    # here from $probeBuild since that tree is already x64 like nrfusion_probe/nrfusion_requiem_game.
    # CaptureProvider32::Connect's own-process auto-spawn looks for it at a fixed relative path,
    # "OptiScaler\NRFusion\NRFusionHost64.exe" under the game's working directory, which is exactly
    # where the installer's $InstallStateDir places it -- so the dist layout has to match that path.
    Invoke-Checked 'cmake' @('--build', $probeBuild, '--config', 'Release', '--target', 'nrfusion_host64')
    $hostCandidates = @(
      (Join-Path $probeBuild 'Release\NRFusionHost64.exe'),
      (Join-Path $probeBuild 'NRFusionHost64.exe')
    )
    $hostExe = $hostCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $hostExe) { throw 'NRFusionHost64.exe was not produced.' }

    $captureBuild = Join-Path $workRoot 'nrfusion-capture32'
    Invoke-Checked 'cmake' @('-S', $root, '-B', $captureBuild, '-A', 'Win32', '-DCMAKE_BUILD_TYPE=Release')
    Invoke-Checked 'cmake' @('--build', $captureBuild, '--config', 'Release', '--target', 'nrfusion_capture32')
    $captureCandidates = @(
      (Join-Path $captureBuild 'Release\nrfusion_capture32.dll'),
      (Join-Path $captureBuild 'nrfusion_capture32.dll')
    )
    $captureDll = $captureCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $captureDll) { throw 'nrfusion_capture32.dll was not produced.' }

    $x86CarrierDist = Join-Path $dist 'OptiScaler\NRFusion'
    New-Item -ItemType Directory -Force -Path $x86CarrierDist | Out-Null
    Copy-Item -LiteralPath $hostExe -Destination (Join-Path $x86CarrierDist 'NRFusionHost64.exe')
    Copy-Item -LiteralPath $captureDll -Destination (Join-Path $x86CarrierDist 'nrfusion_capture32.dll')

    # The testbed ships inside the package, in its own folder next to a copy of the runtime, so
    # a measurement run needs no game and no install. The neural kernels only launch when an
    # upscaler answers, which is why the runtime has to sit beside the executable.
    Invoke-Checked 'cmake' @('--build', $probeBuild, '--config', 'Release', '--target', 'nrfusion_requiem_game')
    $requiemCandidates = @(
      (Join-Path $probeBuild 'Release\RequiemGame.exe'),
      (Join-Path $probeBuild 'RequiemGame.exe')
    )
    $requiem = $requiemCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($requiem) {
        $testbed = Join-Path $dist 'RequiemGame'
        New-Item -ItemType Directory -Force -Path $testbed | Out-Null
        Copy-Item -LiteralPath $requiem -Destination (Join-Path $testbed 'RequiemGame.exe')
        # Without the reference frames the testbed has nothing to compare, so they travel with it.
        $assets = Join-Path $root 'tools/requiem_game/assets'
        if (Test-Path -LiteralPath $assets -PathType Container) {
            Copy-Item -LiteralPath $assets -Destination (Join-Path $testbed 'assets') -Recurse -Force
        }
    }
    else { Write-Warning 'RequiemGame nao foi produzido; o pacote sai sem o banco de testes.' }
    Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY.md') -Destination (Join-Path $dist 'NRFusion.NOTICE.txt')
    $compatDist = Join-Path $dist 'NRFusion\compat'
    New-Item -ItemType Directory -Force -Path $compatDist | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'compat\games.json') -Destination (Join-Path $compatDist 'games.json')

    if (-not $RuntimePath -and ($AutoFetchRuntime -or $CompileInstaller)) {
        $fetchScript = Join-Path $PSScriptRoot 'fetch_dlssnr.ps1'
        $fetchedDll = Join-Path $root 'data\nvngx_dlssnr.dll'
        & $fetchScript -DestinationPath $fetchedDll
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
