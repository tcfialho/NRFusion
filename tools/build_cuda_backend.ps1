# Build the CUDA backend and its test with nvcc directly.
#
# CMake's CUDA language detection does not accept every toolkit/generator pairing, and the
# project must not fail to build because of that: the library falls back to a stub that
# reports NoDevice, and this script builds the real backend when a toolkit is present.

param(
    [string]$Arch = 'sm_89',
    [string]$Out = '.build\cuda',
    [switch]$RunTest
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

$toolkit = Get-ChildItem 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' -Directory -EA SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolkit) { throw 'CUDA Toolkit nao encontrado.' }
$nvcc = Join-Path $toolkit.FullName 'bin\nvcc.exe'
if (-not (Test-Path $nvcc)) { throw "nvcc nao encontrado em $nvcc" }

# nvcc drives the host compiler, so the MSVC environment has to be live for it to work.
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw 'Visual Studio 2022 Community nao encontrado.' }

$outDir = Join-Path $root $Out
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$test = Join-Path $outDir 'fused_grouped_ffn_test.exe'
$w4a8Test = Join-Path $outDir 'w4a8_sm89_gpu_test.exe'
$w4a8Cubin = Join-Path $outDir 'w4a8_ffn_sm89.cubin'
$bench = Join-Path $outDir 'benchmark_sm89_ffn.exe'

# Through a batch file rather than `cmd /c "a && b"`: the nested quoting that command lines
# with spaces in every path require does not survive the round trip reliably.
$script = Join-Path $outDir 'build.bat'
@(
    '@echo off',
    "call `"$vcvars`" >nul 2>&1",
    "`"$nvcc`" -O3 -arch=$Arch -I`"$root\include`" `"$root\tests\fused_grouped_ffn_test.cu`" `"$root\src\cuda\FusedGroupedFfn.cu`" -o `"$test`"",
    'if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%',
    "`"$nvcc`" -O3 -arch=$Arch -I`"$root\include`" `"$root\tests\w4a8_sm89_gpu_test.cu`" `"$root\src\cuda\W4A8FfnSm89.cu`" -o `"$w4a8Test`"",
    'if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%',
    "`"$nvcc`" -O3 -cubin -arch=$Arch -I`"$root\include`" `"$root\src\cuda\W4A8FfnSm89.cu`" -o `"$w4a8Cubin`"",
    'if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%',
    "`"$nvcc`" -O3 -arch=$Arch -I`"$root\include`" `"$root\tools\benchmark_sm89_ffn.cu`" `"$root\src\cuda\W4A8FfnSm89.cu`" -o `"$bench`"",
    'exit /b %ERRORLEVEL%'
) | Set-Content -LiteralPath $script -Encoding ASCII

Write-Host "nvcc: $nvcc"
Write-Host "arquitetura: $Arch"
cmd /c "`"$script`""
if ($LASTEXITCODE -ne 0) { throw "compilacao falhou ($LASTEXITCODE)" }
Write-Host "construido: $test"
Write-Host "construido: $w4a8Test"
Write-Host "construido: $w4a8Cubin"
Write-Host "construido: $bench"

if ($RunTest) {
    Push-Location $root
    try {
        & $test
        if ($LASTEXITCODE -ne 0) { throw "teste fused_grouped falhou ($LASTEXITCODE)" }
        & $w4a8Test
        if ($LASTEXITCODE -ne 0) { throw "teste w4a8_sm89 falhou ($LASTEXITCODE)" }
    } finally { Pop-Location }
}
