param(
    [string]$Preset = "windows-relwithdebinfo",
    [switch]$NoDocker
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$composeFile = Join-Path $repoRoot "tests\live\postgres\compose.yml"
$env:SLOPPY_POSTGRES_TEST_URL = "postgres://sloppy:sloppy@localhost:55432/sloppy_test"

function Assert-DockerAvailable {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "UNAVAILABLE: Docker CLI is required for the PostgreSQL live-provider lane."
    }
    docker info | Out-Null
}

if (-not $NoDocker) {
    Assert-DockerAvailable
    docker compose -f $composeFile up -d --wait
}

try {
    & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
    ctest --test-dir (Join-Path $repoRoot "build\$Preset") --output-on-failure -R "data\.postgres\.live_provider|conformance\.postgres\.(native_live|bridge_live)"
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
    }
}
