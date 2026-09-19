$ErrorActionPreference = 'Stop'
$repos = @(
    @{ Url='https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass.git'; Dir='upstreams/wilsjo' },
    @{ Url='https://github.com/matiasLombo/neural-upstream.git'; Dir='upstreams/neural-upstream' },
    @{ Url='https://github.com/kibblerz/DLSS5-Reshade-AIO.git'; Dir='upstreams/aio' },
    @{ Url='https://github.com/maohgad-web/Neural-coprocessor.git'; Dir='upstreams/neural-coprocessor' },
    @{ Url='https://github.com/jlrouzies-fr/DLSS5-Feeder.git'; Dir='upstreams/feeder' },
    @{ Url='https://github.com/NIGos/dlss5-bridge.git'; Dir='upstreams/bridge' }
)
New-Item -ItemType Directory -Force -Path upstreams | Out-Null
foreach ($r in $repos) {
    if (Test-Path (Join-Path $r.Dir '.git')) {
        git -C $r.Dir fetch --all --tags
        git -C $r.Dir pull --ff-only
    } else {
        git clone $r.Url $r.Dir
    }
}
