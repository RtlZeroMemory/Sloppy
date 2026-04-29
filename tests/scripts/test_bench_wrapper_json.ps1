param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$benchScript = Join-Path $RepoRoot "tools/windows/bench.ps1"
$stderrPath = Join-Path $env:TEMP ("sloppy-bench-json-stderr-{0}.log" -f ([Guid]::NewGuid()))

try {
    Push-Location $RepoRoot
    $jsonText = & $benchScript -Smoke -Json 2> $stderrPath
    if ($LASTEXITCODE -ne 0) {
        $stderr = Get-Content -LiteralPath $stderrPath -Raw -ErrorAction SilentlyContinue
        throw "bench.ps1 -Smoke -Json failed with exit code $LASTEXITCODE. stderr: $stderr"
    }

    $parsed = $jsonText | ConvertFrom-Json
    if ($parsed.sloppyBenchmarkVersion -ne 1) {
        throw "missing or invalid sloppyBenchmarkVersion"
    }
    if ($null -eq $parsed.benchmarks -or $parsed.benchmarks.Count -eq 0) {
        throw "missing benchmark results"
    }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
}
