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

