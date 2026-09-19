# Measure where the neural pass spends its time, per kernel, per hardware pipe.
#
# Three readings disagreed about what limits the pass: a roofline over the weight shapes said
# memory, isolated instruction loops said arithmetic, and the published SASS mix said integer
# overhead. Each was inferring from something other than the running kernel. This asks the
# hardware directly, which is the only thing that settles it.
#
# The DLSS-NR kernels only run inside a real frame, so point this at a game with the neural
# path active, or at one of the project's own D3D12 tests to profile NRFusion's own work.
#
#   tools\nr_profile.ps1 -Target "C:\Games\X\game.exe" -Kernels "swin|ffwd|qkv|conv"
#   tools\nr_profile.ps1 -Target ".build\tests\nrfusion_residual_gpu_test.exe"

param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Arguments = '',
    # Vendor kernel names seen in the parameter dump follow cc_*/k_* with the block role in
    # the name; the default net is wide enough to catch both and the project's own shaders.
    [string]$Kernels = 'swin|ffwd|qkv|attn|conv|dlssnr|Residual|nrfusion',
    [int]$Count = 40,
    [string]$Out = '.temp\perfil'
)

$ErrorActionPreference = 'Stop'

$ncu = Get-ChildItem 'C:\Program Files\NVIDIA Corporation' -Recurse -Filter 'ncu.exe' -Depth 3 -EA SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $ncu) {
    $ncu = Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit' -Recurse -Filter 'ncu.exe' -Depth 4 -EA SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $ncu) { throw 'Nsight Compute nao encontrado. Instale o CUDA Toolkit.' }

if (-not (Test-Path -LiteralPath $Target)) { throw "alvo nao encontrado: $Target" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$report = Join-Path $Out 'perfil'

# One section per question. SpeedOfLight gives the two headline utilisations, the pipe
# breakdown says which unit is actually busy, and the memory chart separates DRAM traffic
# from traffic that never leaves L2 -- the caveat the roofline could not resolve.
$sections = @(
    '--section', 'SpeedOfLight',
    '--section', 'ComputeWorkloadAnalysis',
    '--section', 'MemoryWorkloadAnalysis',
    '--section', 'LaunchStats'
)

$argv = @(
    '--target-processes', 'all',
    '--kernel-name-base', 'demangled',
    '--kernel-name', "regex:$Kernels",
    '--launch-count', $Count,
    '--export', $report,
    '--force-overwrite',
    '--print-summary', 'per-kernel'
) + $sections + @($Target)
if ($Arguments) { $argv += $Arguments.Split(' ') }

Write-Host "perfilando: $Target"
Write-Host "kernels: $Kernels (ate $Count lancamentos)"
& $ncu @argv 2>&1 | Tee-Object -FilePath (Join-Path $Out 'perfil.log')
$code = $LASTEXITCODE

if ($code -ne 0) {
    Write-Warning "ncu terminou com codigo $code."
    Write-Warning 'Se a mensagem citar permissao, os contadores estao restritos a administrador:'
    Write-Warning 'rode esta sessao como administrador, ou libere em NVIDIA Control Panel ->'
    Write-Warning 'Developer -> Manage GPU Performance Counters.'
    exit $code
}

Write-Host ''
Write-Host "relatorio: $report.ncu-rep"
Write-Host 'Para a tabela por caminho de hardware:'
Write-Host "  & '$ncu' --import $report.ncu-rep --page details --csv > $Out\perfil.csv"
