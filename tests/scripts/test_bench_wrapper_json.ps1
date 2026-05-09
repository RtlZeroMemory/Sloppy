param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$benchScript = Join-Path $RepoRoot "tools/windows/bench.ps1"
$stderrPath = Join-Path $env:TEMP ("sloppy-bench-json-stderr-{0}.log" -f ([Guid]::NewGuid()))
$localRuntimeOut = Join-Path $env:TEMP ("sloppy-local-runtime-bench-{0}.json" -f ([Guid]::NewGuid()))

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

    & $benchScript -Suite bridge -Runtime node -WarmupRequests 0 -Requests 1 -Out $localRuntimeOut
    if ($LASTEXITCODE -ne 0) {
        throw "bench.ps1 local runtime JSON smoke failed with exit code $LASTEXITCODE"
    }

    $localParsed = Get-Content -LiteralPath $localRuntimeOut -Raw | ConvertFrom-Json
    if ($localParsed.schemaVersion -ne 1) {
        throw "missing or invalid local runtime schemaVersion"
    }
    if ($null -eq $localParsed.git -or $null -eq $localParsed.host -or $null -eq $localParsed.runtimes) {
        throw "local runtime benchmark output is missing metadata"
    }
    if ($null -eq $localParsed.benchmarks -or $localParsed.benchmarks.Count -eq 0) {
        throw "local runtime benchmark output is missing benchmark results"
    }
    $first = $localParsed.benchmarks | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($first.id) -or [string]::IsNullOrWhiteSpace($first.status)) {
        throw "local runtime benchmark result is missing id/status"
    }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeOut -Force -ErrorAction SilentlyContinue
}
