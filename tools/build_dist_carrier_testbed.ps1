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
