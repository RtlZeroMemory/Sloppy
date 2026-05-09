param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$benchScript = Join-Path $RepoRoot "tools/windows/bench.ps1"
$stderrPath = Join-Path $env:TEMP ("sloppy-bench-json-stderr-{0}.log" -f ([Guid]::NewGuid()))
$localRuntimeOut = Join-Path $env:TEMP ("sloppy-local-runtime-bench-{0}.json" -f ([Guid]::NewGuid()))
$localRuntimeStderr = Join-Path $env:TEMP ("sloppy-local-runtime-bench-stderr-{0}.log" -f ([Guid]::NewGuid()))

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

    & $benchScript -Suite concurrency -Runtime node -WarmupRequests 0 -Requests 1 -Out $localRuntimeOut 2> $localRuntimeStderr
    if ($LASTEXITCODE -ne 0) {
        $stderr = Get-Content -LiteralPath $localRuntimeStderr -Raw -ErrorAction SilentlyContinue
        throw "bench.ps1 local runtime JSON smoke failed with exit code $LASTEXITCODE. stderr: $stderr"
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
    foreach ($field in @("cpu", "memory", "allocations", "bytesCopied")) {
        if ($first.PSObject.Properties.Name -notcontains $field) {
            throw "local runtime benchmark result is missing $field"
        }
    }
    if ($first.id -notlike "concurrency.*") {
        throw "local runtime benchmark result did not come from the concurrency suite"
    }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeOut -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeStderr -Force -ErrorAction SilentlyContinue
}
