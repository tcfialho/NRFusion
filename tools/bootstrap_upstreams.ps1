$ErrorActionPreference = 'Stop'

$lock = Get-Content -Raw 'upstreams.lock.json' | ConvertFrom-Json
$repos = @(
    @{
        Url = $lock.build.optiscaler.repository
        Dir = 'upstreams/wilsjo'
        Commit = $lock.build.optiscaler.commit
    },
    @{ Url='https://github.com/matiasLombo/neural-upstream.git'; Dir='upstreams/neural-upstream' },
    @{ Url='https://github.com/kibblerz/DLSS5-Reshade-AIO.git'; Dir='upstreams/aio' },
    @{ Url='https://github.com/maohgad-web/Neural-coprocessor.git'; Dir='upstreams/neural-coprocessor' },
    @{ Url='https://github.com/jlrouzies-fr/DLSS5-Feeder.git'; Dir='upstreams/feeder' },
    @{ Url='https://github.com/NIGos/dlss5-bridge.git'; Dir='upstreams/bridge' }
)

New-Item -ItemType Directory -Force -Path upstreams | Out-Null

foreach ($r in $repos) {
    if (-not (Test-Path (Join-Path $r.Dir '.git'))) {
        git clone $r.Url $r.Dir
    }

    git -C $r.Dir fetch --all --tags

    if ($r.Commit) {
        git -C $r.Dir checkout --detach $r.Commit
        $actual = (git -C $r.Dir rev-parse HEAD).Trim()
        if ($actual -ne $r.Commit) {
            throw "Upstream lock mismatch for $($r.Dir): expected $($r.Commit), got $actual"
        }
    } else {
        git -C $r.Dir pull --ff-only
    }
}
