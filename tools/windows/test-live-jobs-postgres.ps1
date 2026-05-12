param(
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [switch]$NoDocker,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\$Preset"
$composeFile = Join-Path $repoRoot "tests\live\postgres\compose.yml"
$env:SLOPPY_JOBS_POSTGRES_URL = "postgres://sloppy:sloppy@localhost:55432/sloppy_test"
$env:SLOPPY_POSTGRES_TEST_URL = $env:SLOPPY_JOBS_POSTGRES_URL
$env:Sloppy__Providers__postgres__main__connectionString = $env:SLOPPY_JOBS_POSTGRES_URL

function Resolve-SloppyCli {
    if ($SloppyCli.Length -gt 0) {
        return $SloppyCli
    }
    return Join-Path $buildDir "sloppy.exe"
}

function Assert-DockerAvailable {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "UNAVAILABLE: Docker CLI is required for the PostgreSQL jobs live lane."
    }
    docker info | Out-Null
}

if (-not $NoDocker) {
    Assert-DockerAvailable
    docker compose -f $composeFile up -d --wait
}

try {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
    }

    $cli = Resolve-SloppyCli
    if (-not (Test-Path -LiteralPath $cli)) {
        throw "UNAVAILABLE: sloppy CLI was not found at $cli"
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-postgres-worker\main.ts")
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: PostgreSQL jobs Sloppy Program Mode smoke failed."
    }

    Write-Host "PASS: PostgreSQL jobs live lane used Sloppy Program Mode and data.postgres."
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
    }
}
