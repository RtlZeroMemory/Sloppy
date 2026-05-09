param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$benchScript = Join-Path $RepoRoot "tools/windows/bench.ps1"
$localBenchScript = Join-Path $RepoRoot "tools/windows/local-bench.ps1"
$stderrPath = Join-Path $env:TEMP ("sloppy-bench-json-stderr-{0}.log" -f ([Guid]::NewGuid()))
$localRuntimeOut = Join-Path $env:TEMP ("sloppy-local-runtime-bench-{0}.json" -f ([Guid]::NewGuid()))
$localRuntimeStderr = Join-Path $env:TEMP ("sloppy-local-runtime-bench-stderr-{0}.log" -f ([Guid]::NewGuid()))
$generatedSloppyProject = Join-Path $env:TEMP ("sloppy-generated-bench-app-{0}" -f ([Guid]::NewGuid()))

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

    New-Item -ItemType Directory -Force -Path $generatedSloppyProject | Out-Null
    $localBenchSource = Get-Content -LiteralPath $localBenchScript -Raw
    $tokens = $null
    $parseErrors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput($localBenchSource, [ref]$tokens, [ref]$parseErrors)
    if ($null -ne $parseErrors -and $parseErrors.Count -gt 0) {
        throw "local-bench.ps1 has parse errors"
    }

    $writeFileFunction = $ast.Find({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq "Write-BenchUtf8File"
        }, $true)
    $newAppFunction = $ast.Find({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq "New-SloppyBenchApp"
        }, $true)
    if ($null -eq $writeFileFunction -or $null -eq $newAppFunction) {
        throw "local benchmark app generator functions are missing"
    }

    $escapedProject = $generatedSloppyProject.Replace("'", "''")
    $generatorScript = @(
        '$Utf8NoBomEncoding = [System.Text.UTF8Encoding]::new($false)'
        $writeFileFunction.Extent.Text
        $newAppFunction.Extent.Text
        "New-SloppyBenchApp -ProjectDir '$escapedProject' -SuiteName 'concurrency' -RouteCount 0"
    ) -join [Environment]::NewLine
    . ([scriptblock]::Create($generatorScript))

    $appsettingsPath = Join-Path $generatedSloppyProject "appsettings.json"
    if (-not (Test-Path -LiteralPath $appsettingsPath)) {
        throw "generated Sloppy benchmark app is missing appsettings.json"
    }
    $appsettings = Get-Content -LiteralPath $appsettingsPath -Raw | ConvertFrom-Json
    $maxConnections = [int]$appsettings.Sloppy.Server.MaxConnections
    if ($maxConnections -lt 32) {
        throw "generated Sloppy benchmark app MaxConnections must cover the concurrency suite"
    }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeOut -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeStderr -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $generatedSloppyProject -Recurse -Force -ErrorAction SilentlyContinue
}
