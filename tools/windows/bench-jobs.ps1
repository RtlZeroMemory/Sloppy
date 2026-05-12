param(
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [int]$JobCount = 1000,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\$Preset"
$caseRoot = Join-Path $buildDir "jobs-bench"
$dbPath = Join-Path $caseRoot "jobs-bench.db"

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
    & $cli run (Join-Path $repoRoot "examples\jobs-stress\main.ts") -- $dbPath $JobCount
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs benchmark Sloppy Program Mode workload failed."
    }
}
finally {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
}
