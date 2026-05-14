param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

$benchScript = Join-Path $RepoRoot "tools/windows/bench.ps1"
$localBenchScript = Join-Path $RepoRoot "tools/windows/local-bench.ps1"
$competitorReportScript = Join-Path $RepoRoot "benchmarks/competitors/report-json-local.mjs"
$stderrPath = Join-Path $env:TEMP ("sloppy-bench-json-stderr-{0}.log" -f ([Guid]::NewGuid()))
$localRuntimeOut = Join-Path $env:TEMP ("sloppy-local-runtime-bench-{0}.json" -f ([Guid]::NewGuid()))
$localRuntimeStderr = Join-Path $env:TEMP ("sloppy-local-runtime-bench-stderr-{0}.log" -f ([Guid]::NewGuid()))
$generatedSloppyProject = Join-Path $env:TEMP ("sloppy-generated-bench-app-{0}" -f ([Guid]::NewGuid()))
$competitorReportInput = Join-Path $env:TEMP ("sloppy-json-competitor-current-{0}.json" -f ([Guid]::NewGuid()))
$competitorReportBaseline = Join-Path $env:TEMP ("sloppy-json-competitor-baseline-{0}.json" -f ([Guid]::NewGuid()))
$competitorReportProfileInput = Join-Path $env:TEMP ("sloppy-json-competitor-profile-{0}.json" -f ([Guid]::NewGuid()))
$competitorReportOut = Join-Path $env:TEMP ("sloppy-json-competitor-report-{0}.md" -f ([Guid]::NewGuid()))
$competitorProfileDir = Join-Path $env:TEMP ("sloppy-json-profile-{0}" -f ([Guid]::NewGuid()))

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

    $profileRunId = "fixture-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Force -Path $competitorProfileDir | Out-Null
    $currentReport = [ordered]@{
        schemaVersion = 1
        label = "fixture"
        localDevMachineMeasurements = $true
        generatedAtUtc = "2026-05-14T00:00:00.000Z"
        iterations = 10
        warmupIterations = 2
        repeat = 1
        httpProfile = [ordered]@{ enabled = $false; outDir = $null; runId = $null }
        client = "fixture client"
        scenarios = [ordered]@{ "static-json" = "fixture static JSON" }
        host = [ordered]@{ platform = "win32"; arch = "x64"; release = "fixture"; cpu = "fixture cpu" }
        results = @(
            [ordered]@{
                runtime = "sloppy:loopback:native_json"
                version = "fixture"
                status = "PASS"
                rows = @()
            },
            [ordered]@{
                runtime = "node:http"
                version = "fixture"
                status = "PASS"
                rows = @()
            }
        )
        summary = @(
            [ordered]@{
                runtime = "sloppy:loopback:native_json"
                version = "fixture"
                name = "sloppy.loopback.native_json.static_json"
                scenario = "static-json"
                repeats = 1
                passRepeats = 1
                status = "PASS"
                medianNsPerOp = 1000
                minNsPerOp = 1000
                maxNsPerOp = 1000
            },
            [ordered]@{
                runtime = "node:http"
                version = "fixture"
                name = "node_http.loopback.static_json"
                scenario = "static-json"
                repeats = 1
                passRepeats = 1
                status = "PASS"
                medianNsPerOp = 3000
                minNsPerOp = 3000
                maxNsPerOp = 3000
            }
        )
    }
    $baselineReport = $currentReport | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $baselineReport.summary[0].medianNsPerOp = 2000
    $profileInputReport = $currentReport | ConvertTo-Json -Depth 10 | ConvertFrom-Json
    $profileInputReport.httpProfile.enabled = $true
    $profileInputReport.httpProfile.outDir = $competitorProfileDir
    $profileInputReport.httpProfile.runId = $profileRunId

    $profileFixture = [ordered]@{
        scenario = "sloppy.loopback.native_json.static_json"
        requests = 12
        phases = [ordered]@{
            route_dispatch = [ordered]@{ totalNs = 1200; count = 12; avgNs = 100; minNs = 80; maxNs = 140 }
        }
        counters = [ordered]@{
            keepAliveReused = 11
            connectionsOpened = 1
            v8HandlerCalls = 0
            noJsResponsePlanHits = 12
        }
    }
    $profileFixturePath = Join-Path $competitorProfileDir "http-profile-$profileRunId-sloppy.loopback.native_json.static_json-repeat-1.json"
    $currentReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $competitorReportInput -Encoding utf8
    $baselineReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $competitorReportBaseline -Encoding utf8
    $profileInputReport | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $competitorReportProfileInput -Encoding utf8
    $profileFixture | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $profileFixturePath -Encoding utf8

    & node $competitorReportScript --input $competitorReportInput --baseline $competitorReportBaseline --profile-input $competitorReportProfileInput --out $competitorReportOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "json competitor report renderer failed with exit code $LASTEXITCODE"
    }
    $competitorReportMarkdown = Get-Content -LiteralPath $competitorReportOut -Raw
    foreach ($expected in @("JSON Competitor Benchmark Report", "Sloppy Before/After Delta", "HTTP Profile Evidence", "Label Verification", "Profile expectation checks", "No-JS hits")) {
        if ($competitorReportMarkdown -notlike "*$expected*") {
            throw "json competitor report missing expected text: $expected"
        }
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
    $maxActiveRequests = [int]$appsettings.Sloppy.Server.MaxActiveRequests
    $connectionCapacity = [int]$appsettings.Sloppy.Server.ConnectionCapacity
    $http2MaxStreams = [int]$appsettings.Sloppy.Server.Http2MaxStreams
    $dispatchOnEventLoop = [bool]$appsettings.Sloppy.Server.DispatchOnEventLoop
    $maxDispatchesPerTick = [int]$appsettings.Sloppy.Server.MaxDispatchesPerTick
    if ($maxConnections -lt 32 -or $maxActiveRequests -lt 32 -or
        $connectionCapacity -lt $maxConnections -or $http2MaxStreams -lt 32 -or
        -not $dispatchOnEventLoop -or $maxDispatchesPerTick -lt 32)
    {
        throw "generated Sloppy benchmark app server caps must cover the concurrency suite"
    }
}
finally {
    Pop-Location
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeOut -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $localRuntimeStderr -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $generatedSloppyProject -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $competitorReportInput -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $competitorReportBaseline -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $competitorReportProfileInput -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $competitorReportOut -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $competitorProfileDir -Recurse -Force -ErrorAction SilentlyContinue
}
