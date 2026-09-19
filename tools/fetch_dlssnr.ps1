param(
    [string]$DestinationPath = '',
    [string]$Repo = 'RankFTW/rhi-repo',
    [string]$Tag = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $DestinationPath) {
    $DestinationPath = Join-Path $root 'dist\nvngx_dlssnr.dll'
}
$DestinationPath = [IO.Path]::GetFullPath($DestinationPath)

$knownGoodHashes = @{
    '6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927' = 'dlssnr-310.8.SF-v2 (RTX 20/30/40/50 Multi-Gen)'
    '4C5BD1171C7336B4B04FB394DE51DA285AB6EAD6F922D7AFDEC163F71C319D74' = 'dlssnr-310.8.SF (RTX 20/30/40 Multi-Gen)'
    '4B8D19BC3EFF58A084F5ECA7489C921501C203450169FB82FF4F649A4482BA05' = 'dlssnr-310.8.0-RTX40 (RTX 40 only)'
    'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E' = 'dlssnr-310.8.0 (RTX 50 only original)'
    'E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A' = 'dlssnr alternative build'
}

# If destination already exists and is valid, and -Force is not specified, skip download
if (-not $Force -and (Test-Path -LiteralPath $DestinationPath -PathType Leaf)) {
    $existingHash = (Get-FileHash -LiteralPath $DestinationPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($knownGoodHashes.ContainsKey($existingHash)) {
        Write-Host "nvngx_dlssnr.dll already present and valid: $($knownGoodHashes[$existingHash]) [$existingHash]"
        return $DestinationPath
    }
}

Write-Host "Resolving DLSS-NR release from $Repo (DLSS5oneclick strategy: prioritizing .SF multi-generation for RTX 20/30/40)..."

$chosenTag = $Tag
$downloadUrl = ''

if (-not $chosenTag) {
    # 1. Try GitHub API
    $apiReleases = $null
    try {
        $headers = @{
            'User-Agent' = 'NRFusion-DLSSNR-Fetcher/0.5.4'
            'Accept' = 'application/vnd.github+json'
        }
        $resp = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=100" -Headers $headers -TimeoutSec 15 -ErrorAction Stop
        $apiReleases = @($resp)
    }
    catch {
        Write-Warning "GitHub API call failed ($($_.Exception.Message)). Falling back to HTML scraping."
    }

    if ($apiReleases) {
        # Filter for tags starting with dlssnr-
        $candidates = @()
        foreach ($r in $apiReleases) {
            $t = [string]$r.tag_name
            if ($t -like 'dlssnr-*') {
                $asset = $r.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
                if ($asset) {
                    $candidates += [PSCustomObject]@{
                        Tag = $t
                        Url = [string]$asset.browser_download_url
                        IsSF = ($t -match '\.SF')
                    }
                }
            }
        }

        # Prefer ShortFuse .SF multi-generation builds (supports RTX 20, 30, 40)
        $sfCandidates = @($candidates | Where-Object { $_.IsSF })
        if ($sfCandidates.Count -gt 0) {
            $chosen = $sfCandidates | Sort-Object -Property Tag -Descending | Select-Object -First 1
        } else {
            $chosen = $candidates | Sort-Object -Property Tag -Descending | Select-Object -First 1
        }

        if ($chosen) {
            $chosenTag = $chosen.Tag
            $downloadUrl = $chosen.Url
        }
    }

    # 2. If API was unavailable or empty, scrape HTML
    if (-not $downloadUrl) {
        $html = (curl.exe -sL "https://github.com/$Repo/releases") -join "`n"
        $matches = [regex]::Matches($html, '/releases/tag/(dlssnr-[A-Za-z0-9._-]+)')
        $tags = @()
        foreach ($m in $matches) {
            $t = $m.Groups[1].Value
            if ($tags -notcontains $t) { $tags += $t }
        }

        $sfTags = @($tags | Where-Object { $_ -match '\.SF' })
        if ($sfTags.Count -gt 0) {
            $chosenTag = $sfTags | Sort-Object -Descending | Select-Object -First 1
        } elseif ($tags.Count -gt 0) {
            $chosenTag = $tags | Sort-Object -Descending | Select-Object -First 1
        } else {
            throw "Failed to find any dlssnr release tags on github.com/$Repo"
        }

        $assetVersion = $chosenTag -replace '^dlssnr-', ''
        $downloadUrl = "https://github.com/$Repo/releases/download/$chosenTag/nvngx_dlssnr_$assetVersion.zip"
    }
} else {
    $assetVersion = $chosenTag -replace '^dlssnr-', ''
    $downloadUrl = "https://github.com/$Repo/releases/download/$chosenTag/nvngx_dlssnr_$assetVersion.zip"
}

Write-Host "Selected DLSS-NR model release: $chosenTag"
Write-Host "Download URL: $downloadUrl"

$tempDir = Join-Path $root '.temp\dlssnr_download'
if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$zipFile = Join-Path $tempDir 'dlssnr.zip'
Write-Host "Downloading $chosenTag..."
curl.exe -sL -f $downloadUrl -o $zipFile
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $zipFile)) {
    throw "Download failed for $downloadUrl"
}

Write-Host "Extracting archive..."
Expand-Archive -LiteralPath $zipFile -DestinationPath $tempDir -Force

$extractedDll = Get-ChildItem -LiteralPath $tempDir -Recurse -Filter 'nvngx_dlssnr.dll' | Select-Object -First 1
if (-not $extractedDll) {
    throw "nvngx_dlssnr.dll was not found inside the downloaded archive."
}

$hash = (Get-FileHash -LiteralPath $extractedDll.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Host "Calculated SHA-256: $hash"

if ($knownGoodHashes.ContainsKey($hash)) {
    Write-Host "Validated: $($knownGoodHashes[$hash])"
} else {
    Write-Warning "Unrecognized SHA-256 ($hash), but continuing as requested."
}

$destParent = Split-Path -Parent $DestinationPath
if (-not (Test-Path -LiteralPath $destParent)) {
    New-Item -ItemType Directory -Force -Path $destParent | Out-Null
}

Copy-Item -LiteralPath $extractedDll.FullName -Destination $DestinationPath -Force
Remove-Item -LiteralPath $tempDir -Recurse -Force

Write-Host "Successfully installed nvngx_dlssnr.dll to: $DestinationPath"
return $DestinationPath
