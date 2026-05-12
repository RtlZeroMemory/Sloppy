param(
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\$Preset"
$caseRoot = Join-Path $buildDir "jobs-sqlite-live"
$dbPath = Join-Path $caseRoot "jobs-live.db"

function Resolve-SloppyCli {
    if ($SloppyCli.Length -gt 0) {
        return $SloppyCli
    }
    return Join-Path $buildDir "sloppy.exe"
}

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
}

$cli = Resolve-SloppyCli
if (-not (Test-Path -LiteralPath $cli)) {
    throw "UNAVAILABLE: sloppy CLI was not found at $cli"
}

Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $caseRoot | Out-Null

try {
    & $cli run (Join-Path $repoRoot "examples\jobs-basic\main.ts") -- $dbPath
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs-basic Sloppy Program Mode smoke failed."
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-recurring\main.ts") -- $dbPath
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs-recurring Sloppy Program Mode smoke failed."
    }

    Write-Host "PASS: SQLite jobs live lane used Sloppy Program Mode and data.sqlite."
}
finally {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
}
