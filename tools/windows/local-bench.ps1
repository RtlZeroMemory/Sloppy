param(
    [string[]]$Suite = @("http", "route", "bridge", "concurrency", "startup"),
    [string[]]$Runtime = @("sloppy", "node", "bun", "deno"),
    [string]$Out,
    [string[]]$Compare = @(),
    [int]$WarmupRequests = 10,
    [int]$Requests = 100,
    [int]$TimeoutSeconds = 20,
    [string]$SloppyExe
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Net.Http

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$AllowedSuites = @("http", "route", "route.generated-table", "route.compact-prefix", "bridge", "concurrency", "middleware", "sqlite", "startup")
$AllowedRuntimes = @("sloppy", "node", "bun", "deno")
$Utf8NoBomEncoding = [System.Text.UTF8Encoding]::new($false)

function Write-BenchUtf8File {
    param(
        [string]$Path,
        [string]$Content
    )

    [System.IO.File]::WriteAllText($Path, $Content, $Utf8NoBomEncoding)
}

function Split-BenchSelection {
    param([string[]]$Values)

    $result = @()
    foreach ($value in $Values) {
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($part in ($value -split ",")) {
            $trimmed = $part.Trim().ToLowerInvariant()
            if ($trimmed.Length -gt 0) {
                $result += $trimmed
            }
        }
    }
    return @($result | Select-Object -Unique)
}

function Get-GitValue {
    param([string[]]$Arguments)

    $value = & git -c core.excludesfile= -C $Root @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }
    return ($value | Select-Object -First 1)
}

function Get-BenchGitInfo {
    $status = & git -c core.excludesfile= -C $Root status --short --untracked-files=no 2>$null
    if ($LASTEXITCODE -ne 0) {
        $status = @()
    }
    return [ordered]@{
        commit = Get-GitValue @("rev-parse", "HEAD")
        branch = Get-GitValue @("branch", "--show-current")
        dirty = ($status | Measure-Object).Count -gt 0
    }
}

function Get-BenchHostInfo {
    $cpu = $null
    try {
        if ($IsWindows -or $env:OS -eq "Windows_NT") {
            $cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
        }
    }
    catch {
        $cpu = $null
    }

    return [ordered]@{
        os = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription
        arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        cpu = $cpu
        logicalCores = [Environment]::ProcessorCount
    }
}

function Resolve-SloppyExecutable {
    if (-not [string]::IsNullOrWhiteSpace($SloppyExe) -and (Test-Path -LiteralPath $SloppyExe)) {
        return (Resolve-Path -LiteralPath $SloppyExe).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($env:SLOPPY_BENCH_SLOPPY_EXE) -and
        (Test-Path -LiteralPath $env:SLOPPY_BENCH_SLOPPY_EXE)) {
        return (Resolve-Path -LiteralPath $env:SLOPPY_BENCH_SLOPPY_EXE).Path
    }

    $candidates = @(
        "build/windows-relwithdebinfo/sloppy.exe",
        "build/windows-release/sloppy.exe",
        "build/windows-dev/sloppy.exe",
        "build/windows-relwithdebinfo/bin/sloppy.exe",
        "build/windows-release/bin/sloppy.exe",
        "build/windows-dev/bin/sloppy.exe"
    )

    foreach ($candidate in $candidates) {
        $path = Join-Path $Root $candidate
        if (Test-Path -LiteralPath $path) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    $command = Get-Command "sloppy" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return $null
}

function Get-ProcessVersionText {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    try {
        $output = & $File @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) {
            return $null
        }
        return (($output | Select-Object -First 1) -as [string])
    }
    catch {
        return $null
    }
}

function Get-RuntimeInfo {
    param([string]$Name)

    if ($Name -eq "sloppy") {
        $path = Resolve-SloppyExecutable
        if ($null -eq $path) {
            return [ordered]@{
                status = "UNAVAILABLE"
                path = $null
                version = $null
                reason = "sloppy executable was not found; set SLOPPY_BENCH_SLOPPY_EXE or build a V8-enabled preset"
            }
        }
        return [ordered]@{
            status = "AVAILABLE"
            path = $path
            version = Get-ProcessVersionText $path @("--version")
            reason = $null
        }
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return [ordered]@{
            status = "UNAVAILABLE"
            path = $null
            version = $null
            reason = "$Name was not found on PATH"
        }
    }

    return [ordered]@{
        status = "AVAILABLE"
        path = $command.Source
        version = Get-ProcessVersionText $command.Source @("--version")
        reason = $null
    }
}

function New-BenchResult {
    param(
        [string]$Id,
        [string]$Suite,
        [string]$Runtime,
        [string]$Status,
        [string]$Reason,
        [int]$Warmup,
        [int]$Measured,
        [double[]]$LatenciesMs = @(),
        [int]$Errors = 0,
        [Nullable[double]]$StartupMs = $null,
        [Nullable[double]]$RequestsPerSecond = $null,
        [switch]$SuppressThroughput,
        [hashtable]$ProcessProfile = $null,
        [hashtable]$Correctness = $null,
        [hashtable]$Extra = $null
    )

    $sorted = @($LatenciesMs | Sort-Object)
    $p50 = $null
    $p95 = $null
    $p99 = $null
    $rps = $null

    if ($sorted.Count -gt 0) {
        $p50 = Get-Percentile $sorted 50
        $p95 = Get-Percentile $sorted 95
        $p99 = Get-Percentile $sorted 99
        $totalSeconds = (($LatenciesMs | Measure-Object -Sum).Sum) / 1000.0
        if ($totalSeconds -gt 0) {
            $rps = [Math]::Round($LatenciesMs.Count / $totalSeconds, 2)
        }
    }
    if ($SuppressThroughput) {
        $rps = $null
    }
    elseif ($null -ne $RequestsPerSecond) {
        $rps = [Math]::Round([double]$RequestsPerSecond, 2)
    }

    $result = [ordered]@{
        id = $Id
        suite = $Suite
        runtime = $Runtime
        status = $Status
        reason = $Reason
        warmupRequests = $Warmup
        requests = $Measured
        p50Ms = $p50
        p95Ms = $p95
        p99Ms = $p99
        requestsPerSecond = $rps
        errorCount = $Errors
        startupMs = $StartupMs
        cpu = $(if ($null -ne $ProcessProfile -and $ProcessProfile.ContainsKey("cpu")) { $ProcessProfile.cpu } else { $null })
        memory = $(if ($null -ne $ProcessProfile -and $ProcessProfile.ContainsKey("memory")) { $ProcessProfile.memory } else { $null })
        allocations = $null
        bytesCopied = $null
        correctness = $(if ($null -eq $Correctness) {
                [ordered]@{ checked = $false; status = "SKIPPED"; details = "no correctness check ran" }
            }
            else {
                $Correctness
            })
    }

    if ($null -ne $Extra) {
        foreach ($key in $Extra.Keys) {
            $result[$key] = $Extra[$key]
        }
    }

    return $result
}

function Get-Percentile {
    param(
        [double[]]$SortedValues,
        [int]$Percentile
    )

    if ($SortedValues.Count -eq 0) {
        return $null
    }
    $index = [Math]::Ceiling(($Percentile / 100.0) * $SortedValues.Count) - 1
    if ($index -lt 0) {
        $index = 0
    }
    if ($index -ge $SortedValues.Count) {
        $index = $SortedValues.Count - 1
    }
    return [Math]::Round($SortedValues[$index], 4)
}

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    }
    finally {
        $listener.Stop()
    }
}

function Start-BenchProcess {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [hashtable]$Environment,
        [string]$Name
    )

    $stdoutPath = Join-Path $WorkingDirectory "$Name.stdout.log"
    $stderrPath = Join-Path $WorkingDirectory "$Name.stderr.log"
    $previous = @{}

    foreach ($key in $Environment.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, [string]$Environment[$key], "Process")
    }

    try {
        return Start-Process `
            -FilePath $File `
            -ArgumentList $Arguments `
            -WorkingDirectory $WorkingDirectory `
            -PassThru `
            -WindowStyle Hidden `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
    }
    finally {
        foreach ($key in $Environment.Keys) {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], "Process")
        }
    }
}

function Stop-BenchProcess {
    param($Process)

    if ($null -eq $Process) {
        return
    }
    try {
        if (-not $Process.HasExited) {
            $Process.Kill($true)
            $Process.WaitForExit(5000) | Out-Null
        }
    }
    catch {
        try {
            if (-not $Process.HasExited) {
                $Process.Kill()
            }
        }
        catch {
        }
    }
}

function Get-BenchExceptionMessage {
    param([Exception]$Exception)

    if ($null -eq $Exception) {
        return "request failed"
    }
    $parts = @($Exception.Message)
    $inner = $Exception.InnerException
    while ($null -ne $inner) {
        if (-not [string]::IsNullOrWhiteSpace($inner.Message)) {
            $parts += $inner.Message
        }
        $inner = $inner.InnerException
    }
    return ($parts | Select-Object -Unique) -join "; inner="
}

function Get-BenchProcessSnapshot {
    param($Process)

    if ($null -eq $Process) {
        return $null
    }
    try {
        if ($Process.HasExited) {
            return $null
        }
        $Process.Refresh()
        return [ordered]@{
            totalProcessorMs = [Math]::Round($Process.TotalProcessorTime.TotalMilliseconds, 4)
            userProcessorMs = [Math]::Round($Process.UserProcessorTime.TotalMilliseconds, 4)
            privilegedProcessorMs = [Math]::Round($Process.PrivilegedProcessorTime.TotalMilliseconds, 4)
            workingSetBytes = $Process.WorkingSet64
            peakWorkingSetBytes = $Process.PeakWorkingSet64
            privateMemoryBytes = $Process.PrivateMemorySize64
            pagedMemoryBytes = $Process.PagedMemorySize64
            peakPagedMemoryBytes = $Process.PeakPagedMemorySize64
            virtualMemoryBytes = $Process.VirtualMemorySize64
            peakVirtualMemoryBytes = $Process.PeakVirtualMemorySize64
        }
    }
    catch {
        return $null
    }
}

function Get-BenchMetricDelta {
    param(
        [object]$Before,
        [object]$After,
        [string]$Key
    )

    if ($null -eq $Before -or $null -eq $After) {
        return $null
    }
    if (-not $Before.Contains($Key) -or -not $After.Contains($Key)) {
        return $null
    }
    return [Math]::Round(([double]$After[$Key] - [double]$Before[$Key]), 4)
}

function Get-BenchProcessProfile {
    param(
        [object]$Before,
        [object]$After,
        [int]$MeasuredRequests
    )

    if ($null -eq $Before -or $null -eq $After) {
        return $null
    }

    $cpuTotalMs = Get-BenchMetricDelta $Before $After "totalProcessorMs"
    $cpuUserMs = Get-BenchMetricDelta $Before $After "userProcessorMs"
    $cpuPrivilegedMs = Get-BenchMetricDelta $Before $After "privilegedProcessorMs"
    $cpuMsPerRequest = $null
    if ($null -ne $cpuTotalMs -and $MeasuredRequests -gt 0) {
        $cpuMsPerRequest = [Math]::Round($cpuTotalMs / $MeasuredRequests, 4)
    }

    return @{
        cpu = [ordered]@{
            totalMs = $cpuTotalMs
            userMs = $cpuUserMs
            privilegedMs = $cpuPrivilegedMs
            msPerRequest = $cpuMsPerRequest
        }
        memory = [ordered]@{
            workingSetBytes = $After["workingSetBytes"]
            privateMemoryBytes = $After["privateMemoryBytes"]
            pagedMemoryBytes = $After["pagedMemoryBytes"]
            virtualMemoryBytes = $After["virtualMemoryBytes"]
            peakWorkingSetBytes = $After["peakWorkingSetBytes"]
            peakPagedMemoryBytes = $After["peakPagedMemoryBytes"]
            peakVirtualMemoryBytes = $After["peakVirtualMemoryBytes"]
        }
    }
}

function Get-BenchProcessFailureDetails {
    param(
        [string]$WorkingDirectory,
        [string]$Name,
        $Process,
        [string]$BaseUrl
    )

    $parts = @("baseUrl=$BaseUrl", "tempDir=$WorkingDirectory")
    if (-not [string]::IsNullOrWhiteSpace($script:LastReadyError)) {
        $parts += "lastReadyError=$script:LastReadyError"
    }
    if ($null -ne $Process) {
        $parts += "processExited=$($Process.HasExited)"
        try {
            $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $($Process.Id)" -ErrorAction SilentlyContinue
            if ($null -ne $processInfo -and -not [string]::IsNullOrWhiteSpace($processInfo.CommandLine)) {
                $parts += "commandLine=$($processInfo.CommandLine)"
            }
        }
        catch {
        }
        if ($Process.HasExited) {
            $parts += "exitCode=$($Process.ExitCode)"
        }
    }

    foreach ($stream in @("stderr", "stdout")) {
        $path = Join-Path $WorkingDirectory "$Name.$stream.log"
        $text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
        if (-not [string]::IsNullOrWhiteSpace($text)) {
            $compact = ($text -replace "\s+", " ").Trim()
            if ($compact.Length -gt 500) {
                $compact = $compact.Substring(0, 500) + "..."
            }
            $parts += "$stream=$compact"
        }
    }

    return ($parts -join "; ")
}

function Invoke-BenchHttpRequest {
    param(
        [System.Net.Http.HttpClient]$Client,
        [string]$Method,
        [string]$Url,
        [string]$Body = $null,
        [string]$ContentType = "application/json"
    )

    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($Method), $Url)
    if (-not [string]::IsNullOrEmpty($Body)) {
        $request.Content = [System.Net.Http.StringContent]::new($Body, [System.Text.Encoding]::UTF8, $ContentType)
    }

    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $response = $Client.SendAsync($request).GetAwaiter().GetResult()
    $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
    $watch.Stop()

    $contentTypeValue = $null
    if ($null -ne $response.Content.Headers.ContentType) {
        $contentTypeValue = $response.Content.Headers.ContentType.ToString()
    }

    return [ordered]@{
        elapsedMs = $watch.Elapsed.TotalMilliseconds
        status = [int]$response.StatusCode
        body = $text
        contentType = $contentTypeValue
    }
}

function New-BenchHttpRequestMessage {
    param(
        [hashtable]$Workload,
        [string]$BaseUrl
    )

    $request = [System.Net.Http.HttpRequestMessage]::new(
        [System.Net.Http.HttpMethod]::new($Workload.method),
        "$BaseUrl$($Workload.path)"
    )
    if ($null -ne $Workload.body) {
        $request.Content = [System.Net.Http.StringContent]::new(
            $Workload.body,
            [System.Text.Encoding]::UTF8,
            "application/json"
        )
    }
    if ($Workload.headers) {
        foreach ($header in $Workload.headers.Keys) {
            $request.Headers.TryAddWithoutValidation($header, [string]$Workload.headers[$header]) | Out-Null
        }
    }
    return $request
}

function Wait-BenchServerReady {
    param(
        [System.Net.Http.HttpClient]$Client,
        [string]$BaseUrl,
        $Process,
        [int]$Timeout
    )

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($Timeout)
    $script:LastReadyError = $null
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        if ($null -ne $Process -and $Process.HasExited) {
            return $false
        }
        try {
            $response = Invoke-BenchHttpRequest $Client "GET" "$BaseUrl/health"
            if ($response.status -eq 200) {
                return $true
            }
            $script:LastReadyError = "health returned status $($response.status) with body '$($response.body)'"
        }
        catch {
            $inner = $(if ($_.Exception.InnerException) { "; inner=$($_.Exception.InnerException.Message)" } else { "" })
            $script:LastReadyError = "$($_.Exception.Message)$inner"
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Test-BenchResponse {
    param(
        [hashtable]$Workload,
        [hashtable]$Response
    )

    if ($Response.status -ne $Workload.expectedStatus) {
        return "expected status $($Workload.expectedStatus), got $($Response.status)"
    }

    if ($Workload.expectedContentType -and
        ($null -eq $Response.contentType -or
            -not $Response.contentType.ToLowerInvariant().Contains($Workload.expectedContentType))) {
        return "expected content type containing $($Workload.expectedContentType), got $($Response.contentType)"
    }

    if ($null -ne $Workload.expectedBody -and $Response.body -ne $Workload.expectedBody) {
        return "expected body '$($Workload.expectedBody)', got '$($Response.body)'"
    }

    if ($Workload.expectedBodyContains -and -not $Response.body.Contains($Workload.expectedBodyContains)) {
        return "expected body containing '$($Workload.expectedBodyContains)', got '$($Response.body)'"
    }

    return $null
}

function Invoke-ConcurrentHttpBatch {
    param(
        [System.Net.Http.HttpClient]$Client,
        [string]$BaseUrl,
        [hashtable]$Workload,
        [int]$Count
    )

    $requests = @()
    $tasks = @()
    $errors = 0
    $firstError = $null
    $watch = [System.Diagnostics.Stopwatch]::StartNew()

    try {
        for ($index = 0; $index -lt $Count; $index += 1) {
            $request = New-BenchHttpRequestMessage $Workload $BaseUrl
            $requests += $request
            $tasks += $Client.SendAsync($request)
        }

        try {
            [System.Threading.Tasks.Task]::WaitAll([System.Threading.Tasks.Task[]]$tasks)
        }
        catch {
        }

        for ($index = 0; $index -lt $tasks.Count; $index += 1) {
            $task = $tasks[$index]
            if ($task.IsFaulted) {
                $errors += 1
                if ($null -eq $firstError) {
                    if ($task.Exception -and $task.Exception.InnerException) {
                        $firstError = Get-BenchExceptionMessage $task.Exception.InnerException
                    }
                    else {
                        $firstError = "concurrent request failed"
                    }
                }
                continue
            }
            if ($task.IsCanceled) {
                $errors += 1
                if ($null -eq $firstError) {
                    $firstError = "concurrent request was canceled"
                }
                continue
            }

            $response = $null
            try {
                $response = $task.GetAwaiter().GetResult()
                $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
                $contentType = $null
                if ($null -ne $response.Content.Headers.ContentType) {
                    $contentType = $response.Content.Headers.ContentType.ToString()
                }
                $check = Test-BenchResponse $Workload @{
                    status = [int]$response.StatusCode
                    body = $body
                    contentType = $contentType
                }
                if ($null -ne $check) {
                    $errors += 1
                    if ($null -eq $firstError) {
                        $firstError = $check
                    }
                }
            }
            catch {
                $errors += 1
                if ($null -eq $firstError) {
                    $firstError = Get-BenchExceptionMessage $_.Exception
                }
            }
            finally {
                if ($null -ne $response) {
                    $response.Dispose()
                }
            }
        }
    }
    catch {
        $errors += $Count
        if ($null -eq $firstError) {
            $firstError = Get-BenchExceptionMessage $_.Exception
        }
    }
    finally {
        $watch.Stop()
        foreach ($request in $requests) {
            $request.Dispose()
        }
    }

    return [ordered]@{
        elapsedMs = $watch.Elapsed.TotalMilliseconds
        errors = $errors
        firstError = $firstError
    }
}

function Invoke-ConcurrentHttpWorkload {
    param(
        [System.Net.Http.HttpClient]$Client,
        [string]$BaseUrl,
        [hashtable]$Workload,
        $Process,
        [int]$Warmup,
        [int]$Measured
    )

    $concurrency = [int]$Workload.concurrency
    if ($concurrency -lt 1) {
        $concurrency = 1
    }

    $errors = 0
    $firstError = $null
    $latencies = @()
    $remainingWarmup = $Warmup
    while ($remainingWarmup -gt 0) {
        $batchSize = [Math]::Min($concurrency, $remainingWarmup)
        $batch = Invoke-ConcurrentHttpBatch $Client $BaseUrl $Workload $batchSize
        $errors += $batch.errors
        if ($null -eq $firstError -and $null -ne $batch.firstError) {
            $firstError = $batch.firstError
        }
        $remainingWarmup -= $batchSize
    }

    $profileBefore = Get-BenchProcessSnapshot $Process
    $wall = [System.Diagnostics.Stopwatch]::StartNew()
    $remainingMeasured = $Measured
    $measuredBatches = 0
    while ($remainingMeasured -gt 0) {
        $batchSize = [Math]::Min($concurrency, $remainingMeasured)
        $batch = Invoke-ConcurrentHttpBatch $Client $BaseUrl $Workload $batchSize
        $measuredBatches += 1
        if ($batch.errors -eq 0) {
            $latencies += $batch.elapsedMs
        }
        $errors += $batch.errors
        if ($null -eq $firstError -and $null -ne $batch.firstError) {
            $firstError = $batch.firstError
        }
        $remainingMeasured -= $batchSize
    }
    $wall.Stop()
    $profileAfter = Get-BenchProcessSnapshot $Process

    $rps = $null
    if ($wall.Elapsed.TotalMilliseconds -gt 0 -and $Measured -gt 0) {
        $rps = $Measured / ($wall.Elapsed.TotalMilliseconds / 1000.0)
    }

    return [ordered]@{
        latencies = [double[]]$latencies
        errors = $errors
        firstError = $firstError
        wallMs = [Math]::Round($wall.Elapsed.TotalMilliseconds, 4)
        measuredBatches = $measuredBatches
        requestsPerSecond = $rps
        profile = Get-BenchProcessProfile $profileBefore $profileAfter $Measured
    }
}

function Get-HttpWorkloads {
    return @(
        @{ id = "http.health"; method = "GET"; path = "/health"; expectedStatus = 200; expectedBody = "ok"; expectedContentType = "text/plain" },
        @{ id = "http.json"; method = "GET"; path = "/json"; expectedStatus = 200; expectedBody = '{"ok":true}'; expectedContentType = "application/json" },
        @{ id = "http.route_param"; method = "GET"; path = "/hello/Ada"; expectedStatus = 200; expectedBody = '{"hello":"Ada"}'; expectedContentType = "application/json" },
        @{ id = "http.query"; method = "GET"; path = "/query?x=1&y=2"; expectedStatus = 200; expectedBody = '{"x":"1","y":"2"}'; expectedContentType = "application/json" },
        @{ id = "http.post_json"; method = "POST"; path = "/echo"; body = '{"n":1}'; expectedStatus = 200; expectedBody = '{"received":true}'; expectedContentType = "application/json" }
    )
}

function Get-ConcurrencyWorkloads {
    return @(
        @{ id = "concurrency.health.4"; method = "GET"; path = "/health"; concurrency = 4; expectedStatus = 200; expectedBody = "ok"; expectedContentType = "text/plain" },
        @{ id = "concurrency.json.16"; method = "GET"; path = "/json"; concurrency = 16; expectedStatus = 200; expectedBody = '{"ok":true}'; expectedContentType = "application/json" },
        @{ id = "concurrency.route_param.16"; method = "GET"; path = "/hello/Ada"; concurrency = 16; expectedStatus = 200; expectedBody = '{"hello":"Ada"}'; expectedContentType = "application/json" },
        @{ id = "concurrency.post_json.32"; method = "POST"; path = "/echo"; body = '{"n":1}'; concurrency = 32; expectedStatus = 200; expectedBody = '{"received":true}'; expectedContentType = "application/json" }
    )
}

function Get-RouteWorkloads {
    param([string]$ComparatorShape)

    $workloads = @()
    foreach ($count in @(10, 100, 1000)) {
        foreach ($position in @("first", "middle", "last", "missing")) {
            $index = 0
            if ($position -eq "middle") {
                $index = [Math]::Floor($count / 2)
            }
            elseif ($position -eq "last") {
                $index = $count - 1
            }
            elseif ($position -eq "missing") {
                $index = $count + 1
            }
            $expectedStatus = $(if ($position -eq "missing") { 404 } else { 200 })
            $expectedBody = $(if ($position -eq "missing") { $null } else { "r$index" })
            $workloads += @{
                id = "route.$ComparatorShape.$count.$position"
                method = "GET"
                path = "/routes/$index"
                comparatorShape = $ComparatorShape
                routeCount = $count
                routePosition = $position
                expectedStatus = $expectedStatus
                expectedBody = $expectedBody
            }
        }
    }
    return $workloads
}

function Get-BenchWorkloadExtra {
    param([hashtable]$Workload)

    $extra = @{}
    foreach ($key in @("comparatorShape", "routeCount", "routePosition")) {
        if ($Workload.ContainsKey($key)) {
            $extra[$key] = $Workload[$key]
        }
    }
    return $extra
}

function Get-BridgeWorkloads {
    return @(
        @{ id = "bridge.result.text"; method = "GET"; path = "/bridge/text"; expectedStatus = 200; expectedBody = "ok"; expectedContentType = "text/plain" },
        @{ id = "bridge.result.json"; method = "GET"; path = "/bridge/json"; expectedStatus = 200; expectedBody = '{"ok":true}'; expectedContentType = "application/json" },
        @{ id = "bridge.result.created"; method = "GET"; path = "/bridge/created"; expectedStatus = 201; expectedBody = '{"id":1}'; expectedContentType = "application/json" },
        @{ id = "bridge.result.no_content"; method = "GET"; path = "/bridge/no-content"; expectedStatus = 204; expectedBody = "" },
        @{ id = "bridge.context.route"; method = "GET"; path = "/bridge/route/Ada"; expectedStatus = 200; expectedBody = '{"name":"Ada"}'; expectedContentType = "application/json" },
        @{ id = "bridge.context.query"; method = "GET"; path = "/bridge/query?x=1"; expectedStatus = 200; expectedBody = '{"x":"1"}'; expectedContentType = "application/json" },
        @{
            id = "bridge.context.header"
            method = "GET"
            path = "/bridge/header"
            headers = @{ "user-agent" = "bench-agent" }
            expectedStatus = 200
            expectedBody = "bench-agent"
            expectedContentType = "text/plain"
        },
        @{ id = "bridge.request.post_json"; method = "POST"; path = "/bridge/body"; body = '{"bench":true}'; expectedStatus = 200; expectedBody = '{"received":true}'; expectedContentType = "application/json" },
        @{ id = "bridge.async.immediate"; method = "GET"; path = "/bridge/async"; expectedStatus = 200; expectedBody = "async"; expectedContentType = "text/plain" }
    )
}

function Get-MiddlewareWorkloads {
    return @(
        @{ id = "middleware.none"; method = "GET"; path = "/middleware/none"; expectedStatus = 200; expectedBody = "none"; expectedContentType = "text/plain" },
        @{ id = "middleware.one"; method = "GET"; path = "/middleware/one"; expectedStatus = 200; expectedBody = "one"; expectedContentType = "text/plain" },
        @{ id = "middleware.five"; method = "GET"; path = "/middleware/five"; expectedStatus = 200; expectedBody = "five"; expectedContentType = "text/plain" },
        @{ id = "middleware.short_circuit"; method = "GET"; path = "/middleware/short"; expectedStatus = 204; expectedBody = "" },
        @{ id = "middleware.problem_details"; method = "GET"; path = "/middleware/problem"; expectedStatus = 500; expectedBodyContains = "SLOPPY_E_HANDLER_ERROR"; expectedContentType = "application/problem+json" },
        @{ id = "middleware.cors.normal"; method = "GET"; path = "/middleware/cors"; headers = @{ "origin" = "https://bench.local" }; expectedStatus = 200; expectedBody = "cors"; expectedContentType = "text/plain" },
        @{ id = "middleware.cors.preflight"; method = "OPTIONS"; path = "/middleware/cors"; headers = @{ "origin" = "https://bench.local"; "access-control-request-method" = "GET" }; expectedStatus = 204; expectedBody = "" },
        @{ id = "middleware.health"; method = "GET"; path = "/middleware/health"; expectedStatus = 200; expectedBodyContains = "healthy"; expectedContentType = "application/json" }
    )
}

function New-NodeBenchApp {
    param([string]$Path)

    $content = @'
const http = require("http");
const port = Number(process.env.SLOPPY_BENCH_PORT || "0");
const routeCount = Number(process.env.SLOPPY_BENCH_ROUTE_COUNT || "0");
const routeShape = process.env.SLOPPY_BENCH_ROUTE_SHAPE || "generated-table";
const routeTable = new Map();
if (routeShape === "generated-table") {
  for (let index = 0; index < routeCount; index += 1) {
    routeTable.set(`/routes/${index}`, `r${index}`);
  }
}

function send(res, status, body, contentType) {
  const text = body === undefined ? "" : body;
  res.statusCode = status;
  if (contentType) res.setHeader("content-type", contentType);
  res.setHeader("content-length", Buffer.byteLength(text));
  res.end(text);
}

function json(value) {
  return JSON.stringify(value);
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, "http://127.0.0.1");
  if (req.method === "GET" && url.pathname === "/health") return send(res, 200, "ok", "text/plain; charset=utf-8");
  if (req.method === "GET" && url.pathname === "/json") return send(res, 200, json({ok: true}), "application/json; charset=utf-8");
  if (req.method === "GET" && url.pathname.startsWith("/hello/")) return send(res, 200, json({hello: decodeURIComponent(url.pathname.slice(7))}), "application/json; charset=utf-8");
  if (req.method === "GET" && url.pathname === "/query") return send(res, 200, json({x: url.searchParams.get("x"), y: url.searchParams.get("y")}), "application/json; charset=utf-8");
  if (req.method === "POST" && url.pathname === "/echo") {
    req.on("data", () => {});
    req.on("end", () => send(res, 200, json({received: true}), "application/json; charset=utf-8"));
    return;
  }
  if (req.method === "GET" && routeShape === "generated-table") {
    const body = routeTable.get(url.pathname);
    if (body !== undefined) return send(res, 200, body, "text/plain; charset=utf-8");
  }
  if (req.method === "GET" && routeShape === "compact-prefix" && url.pathname.startsWith("/routes/")) {
    const index = Number(url.pathname.slice(8));
    if (Number.isInteger(index) && index >= 0 && index < routeCount) return send(res, 200, `r${index}`, "text/plain; charset=utf-8");
  }
  send(res, 404, "", "text/plain; charset=utf-8");
});
server.listen(port, "127.0.0.1");
process.on("SIGTERM", () => server.close(() => process.exit(0)));
process.on("SIGINT", () => server.close(() => process.exit(0)));
'@
    Write-BenchUtf8File $Path $content
}

function New-BunBenchApp {
    param([string]$Path)

    $content = @'
const port = Number(process.env.SLOPPY_BENCH_PORT || "0");
const routeCount = Number(process.env.SLOPPY_BENCH_ROUTE_COUNT || "0");
const routeShape = process.env.SLOPPY_BENCH_ROUTE_SHAPE || "generated-table";
const routeTable = new Map();
if (routeShape === "generated-table") {
  for (let index = 0; index < routeCount; index += 1) {
    routeTable.set(`/routes/${index}`, `r${index}`);
  }
}
function response(status, body, contentType) {
  return new Response(body ?? "", { status, headers: contentType ? { "content-type": contentType } : {} });
}
Bun.serve({
  hostname: "127.0.0.1",
  port,
  async fetch(req) {
    const url = new URL(req.url);
    if (req.method === "GET" && url.pathname === "/health") return response(200, "ok", "text/plain; charset=utf-8");
    if (req.method === "GET" && url.pathname === "/json") return response(200, JSON.stringify({ok: true}), "application/json; charset=utf-8");
    if (req.method === "GET" && url.pathname.startsWith("/hello/")) return response(200, JSON.stringify({hello: decodeURIComponent(url.pathname.slice(7))}), "application/json; charset=utf-8");
    if (req.method === "GET" && url.pathname === "/query") return response(200, JSON.stringify({x: url.searchParams.get("x"), y: url.searchParams.get("y")}), "application/json; charset=utf-8");
    if (req.method === "POST" && url.pathname === "/echo") { await req.text(); return response(200, JSON.stringify({received: true}), "application/json; charset=utf-8"); }
    if (req.method === "GET" && routeShape === "generated-table") {
      const body = routeTable.get(url.pathname);
      if (body !== undefined) return response(200, body, "text/plain; charset=utf-8");
    }
    if (req.method === "GET" && routeShape === "compact-prefix" && url.pathname.startsWith("/routes/")) {
      const index = Number(url.pathname.slice(8));
      if (Number.isInteger(index) && index >= 0 && index < routeCount) return response(200, `r${index}`, "text/plain; charset=utf-8");
    }
    return response(404, "", "text/plain; charset=utf-8");
  }
});
'@
    Write-BenchUtf8File $Path $content
}

function New-DenoBenchApp {
    param([string]$Path)

    $content = @'
const port = Number(Deno.env.get("SLOPPY_BENCH_PORT") || "0");
const routeCount = Number(Deno.env.get("SLOPPY_BENCH_ROUTE_COUNT") || "0");
const routeShape = Deno.env.get("SLOPPY_BENCH_ROUTE_SHAPE") || "generated-table";
const routeTable = new Map();
if (routeShape === "generated-table") {
  for (let index = 0; index < routeCount; index += 1) {
    routeTable.set(`/routes/${index}`, `r${index}`);
  }
}
function response(status, body, contentType) {
  return new Response(body ?? "", { status, headers: contentType ? { "content-type": contentType } : {} });
}
Deno.serve({ hostname: "127.0.0.1", port }, async (req) => {
  const url = new URL(req.url);
  if (req.method === "GET" && url.pathname === "/health") return response(200, "ok", "text/plain; charset=utf-8");
  if (req.method === "GET" && url.pathname === "/json") return response(200, JSON.stringify({ok: true}), "application/json; charset=utf-8");
  if (req.method === "GET" && url.pathname.startsWith("/hello/")) return response(200, JSON.stringify({hello: decodeURIComponent(url.pathname.slice(7))}), "application/json; charset=utf-8");
  if (req.method === "GET" && url.pathname === "/query") return response(200, JSON.stringify({x: url.searchParams.get("x"), y: url.searchParams.get("y")}), "application/json; charset=utf-8");
  if (req.method === "POST" && url.pathname === "/echo") { await req.text(); return response(200, JSON.stringify({received: true}), "application/json; charset=utf-8"); }
  if (req.method === "GET" && routeShape === "generated-table") {
    const body = routeTable.get(url.pathname);
    if (body !== undefined) return response(200, body, "text/plain; charset=utf-8");
  }
  if (req.method === "GET" && routeShape === "compact-prefix" && url.pathname.startsWith("/routes/")) {
    const index = Number(url.pathname.slice(8));
    if (Number.isInteger(index) && index >= 0 && index < routeCount) return response(200, `r${index}`, "text/plain; charset=utf-8");
  }
  return response(404, "", "text/plain; charset=utf-8");
});
'@
    Write-BenchUtf8File $Path $content
}

function New-SloppyBenchApp {
    param(
        [string]$ProjectDir,
        [string]$SuiteName,
        [int]$RouteCount,
        [string]$RouteShape = "generated-table"
    )

    $src = Join-Path $ProjectDir "src"
    New-Item -ItemType Directory -Force -Path $src | Out-Null
    @{
        entry = "src/main.ts"
        outDir = ".sloppy"
        environment = "Development"
    } | ConvertTo-Json | ForEach-Object {
        Write-BenchUtf8File (Join-Path $ProjectDir "sloppy.json") $_
    }
    if ($SuiteName -eq "concurrency") {
        @{
            Sloppy = @{
                Server = @{
                    MaxConnections = 64
                    MaxActiveRequests = 64
                    ConnectionCapacity = 64
                    Http2MaxStreams = 64
                    DispatchOnEventLoop = $true
                    MaxDispatchesPerTick = 64
                }
            }
        } | ConvertTo-Json -Depth 4 | ForEach-Object {
            Write-BenchUtf8File (Join-Path $ProjectDir "appsettings.json") $_
        }
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('import { Sloppy, Results, ProblemDetails } from "sloppy";')
    $lines.Add("")
    $lines.Add("const app = Sloppy.create();")
    $lines.Add('app.get("/health", () => Results.text("ok"));')
    $lines.Add('app.get("/json", () => Results.json({ ok: true }));')
    $lines.Add('app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }));')
    $lines.Add('app.get("/query", (ctx) => Results.json({ x: ctx.query.x, y: ctx.query.y }));')
    $lines.Add('app.post("/echo", () => Results.json({ received: true }));')

    if ($SuiteName -like "route.*" -and $RouteShape -eq "generated-table") {
        for ($index = 0; $index -lt $RouteCount; $index += 1) {
            $lines.Add("app.get(""/routes/$index"", () => Results.text(""r$index""));")
        }
    }

    if ($SuiteName -eq "bridge") {
        $lines.Add('app.get("/bridge/text", () => Results.text("ok"));')
        $lines.Add('app.get("/bridge/json", () => Results.json({ ok: true }));')
        $lines.Add('app.get("/bridge/created", () => Results.created("/bridge/created", { id: 1 }));')
        $lines.Add('app.get("/bridge/no-content", () => Results.noContent());')
        $lines.Add('app.get("/bridge/route/{name}", (ctx) => Results.json({ name: ctx.route.name }));')
        $lines.Add('app.get("/bridge/query", (ctx) => Results.json({ x: ctx.query.x }));')
        $lines.Add('app.get("/bridge/header", (ctx) => Results.text(ctx.header.userAgent));')
        $lines.Add('app.post("/bridge/body", () => Results.json({ received: true }));')
        $lines.Add('app.get("/bridge/async", async () => Results.text("async"));')
    }

    if ($SuiteName -eq "middleware") {
        $lines.Add('app.get("/middleware/none", () => Results.text("none"));')
        $lines.Add('const oneMiddleware = app.group("/middleware");')
        $lines.Add('oneMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('oneMiddleware.get("/one", () => Results.text("one"));')
        $lines.Add('const fiveMiddleware = app.group("/middleware");')
        $lines.Add('fiveMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('fiveMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('fiveMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('fiveMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('fiveMiddleware.use(async (_ctx, next) => await next());')
        $lines.Add('fiveMiddleware.get("/five", () => Results.text("five"));')
        $lines.Add('const shortCircuit = app.group("/middleware");')
        $lines.Add('shortCircuit.use(() => Results.noContent());')
        $lines.Add('shortCircuit.get("/short", () => Results.text("not reached"));')
        $lines.Add('app.useCors({ origins: ["https://bench.local"], methods: ["GET"], headers: ["x-bench"] });')
        $lines.Add('app.get("/middleware/cors", () => Results.text("cors"));')
        $lines.Add('app.mapHealthChecks({ path: "/middleware/health", checks: [{ name: "bench", check: () => true }] });')
        $lines.Add('app.use(ProblemDetails.defaults());')
        $lines.Add('app.get("/middleware/problem", () => { throw new Error("bench failure"); });')
    }

    $lines.Add("")
    $lines.Add("export default app;")
    Write-BenchUtf8File (Join-Path $src "main.ts") (($lines -join [Environment]::NewLine) + [Environment]::NewLine)
}

function Invoke-SloppyBuild {
    param(
        [System.Collections.IDictionary]$RuntimeInfo,
        [string]$ProjectDir
    )

    $outPath = Join-Path $ProjectDir "sloppy-build.out.log"
    $errPath = Join-Path $ProjectDir "sloppy-build.err.log"
    Push-Location $ProjectDir
    try {
        & $RuntimeInfo.path build > $outPath 2> $errPath
        if ($LASTEXITCODE -ne 0) {
            $message = Get-Content -LiteralPath $errPath -Raw -ErrorAction SilentlyContinue
            if ([string]::IsNullOrWhiteSpace($message)) {
                $message = Get-Content -LiteralPath $outPath -Raw -ErrorAction SilentlyContinue
            }
            throw "sloppy build failed: $message"
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-HttpSuiteForRuntime {
    param(
        [string]$RuntimeName,
        [System.Collections.IDictionary]$RuntimeInfo,
        [string]$SuiteName,
        [hashtable[]]$Workloads,
        [int]$RouteCount = 0,
        [string]$RouteShape = "generated-table"
    )

    if ($RuntimeInfo.status -ne "AVAILABLE") {
        return @($Workloads | ForEach-Object {
                New-BenchResult $_.id $SuiteName $RuntimeName "UNAVAILABLE" $RuntimeInfo.reason $WarmupRequests $Requests -Extra (Get-BenchWorkloadExtra $_)
            })
    }

    if ($SuiteName -eq "route.compact-prefix" -and $RuntimeName -eq "sloppy") {
        return @($Workloads | ForEach-Object {
                $reason = "Sloppy source-input route declarations currently emit generated route table entries; no equivalent compact-prefix app shape exists yet."
                New-BenchResult $_.id $SuiteName $RuntimeName "SKIPPED" $reason $WarmupRequests $Requests `
                    -Correctness @{ checked = $false; status = "SKIPPED"; details = $reason } `
                    -Extra (Get-BenchWorkloadExtra $_)
            })
    }

    if (($SuiteName -eq "bridge" -or $SuiteName -eq "middleware") -and $RuntimeName -ne "sloppy") {
        return @($Workloads | ForEach-Object {
                New-BenchResult $_.id $SuiteName $RuntimeName "SKIPPED" "suite measures Sloppy-specific framework or bridge behavior" $WarmupRequests $Requests
            })
    }

    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-local-bench-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    $process = $null
    $client = [System.Net.Http.HttpClient]::new()
    $client.Timeout = [TimeSpan]::FromSeconds($TimeoutSeconds)
    $port = Get-FreePort
    $baseUrl = "http://127.0.0.1:${port}"
    $results = @()
    $readyClient = $null

    try {
        if ($RuntimeName -eq "node") {
            $script = Join-Path $tempDir "server.js"
            New-NodeBenchApp $script
            $process = Start-BenchProcess $RuntimeInfo.path @($script) $tempDir @{
                SLOPPY_BENCH_PORT = $port
                SLOPPY_BENCH_ROUTE_COUNT = $RouteCount
                SLOPPY_BENCH_ROUTE_SHAPE = $RouteShape
            } $RuntimeName
        }
        elseif ($RuntimeName -eq "bun") {
            $script = Join-Path $tempDir "server.js"
            New-BunBenchApp $script
            $process = Start-BenchProcess $RuntimeInfo.path @($script) $tempDir @{
                SLOPPY_BENCH_PORT = $port
                SLOPPY_BENCH_ROUTE_COUNT = $RouteCount
                SLOPPY_BENCH_ROUTE_SHAPE = $RouteShape
            } $RuntimeName
        }
        elseif ($RuntimeName -eq "deno") {
            $script = Join-Path $tempDir "server.ts"
            New-DenoBenchApp $script
            $process = Start-BenchProcess $RuntimeInfo.path @(
                "run",
                "--allow-net=127.0.0.1",
                "--allow-env=SLOPPY_BENCH_PORT,SLOPPY_BENCH_ROUTE_COUNT,SLOPPY_BENCH_ROUTE_SHAPE",
                $script
            ) $tempDir @{
                SLOPPY_BENCH_PORT = $port
                SLOPPY_BENCH_ROUTE_COUNT = $RouteCount
                SLOPPY_BENCH_ROUTE_SHAPE = $RouteShape
            } $RuntimeName
        }
        elseif ($RuntimeName -eq "sloppy") {
            New-SloppyBenchApp $tempDir $SuiteName $RouteCount $RouteShape
            try {
                Invoke-SloppyBuild $RuntimeInfo $tempDir
            }
            catch {
                $buildError = $_.Exception.Message
                return @($Workloads | ForEach-Object {
                        New-BenchResult $_.id $SuiteName $RuntimeName "FAIL" $buildError $WarmupRequests $Requests -Extra (Get-BenchWorkloadExtra $_)
                    })
            }
            $process = Start-BenchProcess $RuntimeInfo.path @(
                "run",
                "--artifacts",
                (Join-Path $tempDir ".sloppy"),
                "--host",
                "127.0.0.1",
                "--port",
                [string]$port
            ) $tempDir @{} $RuntimeName
        }

        $readyClient = [System.Net.Http.HttpClient]::new()
        $readyClient.Timeout = [TimeSpan]::FromSeconds(1)

        $startupWatch = [System.Diagnostics.Stopwatch]::StartNew()
        $ready = Wait-BenchServerReady $readyClient $baseUrl $process $TimeoutSeconds
        $startupWatch.Stop()
        if (-not $ready) {
            $status = "FAIL"
            $reason = "runtime server did not become ready within $TimeoutSeconds seconds"
            $failureDetails = Get-BenchProcessFailureDetails $tempDir $RuntimeName $process $baseUrl
            if (-not [string]::IsNullOrWhiteSpace($failureDetails)) {
                $reason = "$reason; $failureDetails"
            }
            if ($RuntimeName -eq "sloppy") {
                $reason = "$reason; V8-enabled runtime may be unavailable"
                $status = "UNAVAILABLE"
            }
            return @($Workloads | ForEach-Object {
                    New-BenchResult $_.id $SuiteName $RuntimeName $status $reason $WarmupRequests $Requests `
                        -StartupMs $startupWatch.Elapsed.TotalMilliseconds `
                        -Extra (Get-BenchWorkloadExtra $_)
                })
        }

        foreach ($workload in $Workloads) {
            if ($RuntimeName -eq "sloppy" -and -not [string]::IsNullOrWhiteSpace($workload.skipOnSloppy)) {
                $results += New-BenchResult `
                    $workload.id `
                    $SuiteName `
                    $RuntimeName `
                    "SKIPPED" `
                    $workload.skipOnSloppy `
                    $WarmupRequests `
                    $Requests `
                    -StartupMs $startupWatch.Elapsed.TotalMilliseconds `
                    -Correctness @{ checked = $false; status = "SKIPPED"; details = $workload.skipOnSloppy } `
                    -Extra (Get-BenchWorkloadExtra $workload)
                continue
            }

            if ($SuiteName -eq "concurrency" -or $workload.concurrency) {
                $measurement = Invoke-ConcurrentHttpWorkload $client $baseUrl $workload $process $WarmupRequests $Requests
                $status = $(if ($measurement.errors -eq 0) { "PASS" } else { "FAIL" })
                $reason = $(if ($measurement.errors -eq 0) { $null } else { $measurement.firstError })
                $extra = Get-BenchWorkloadExtra $workload
                $extra["concurrency"] = [int]$workload.concurrency
                $extra["wallMs"] = $measurement.wallMs
                $extra["measuredBatches"] = $measurement.measuredBatches
                $extra["latencyModel"] = "concurrent batch wall time"
                $results += New-BenchResult `
                    $workload.id `
                    $SuiteName `
                    $RuntimeName `
                    $status `
                    $reason `
                    $WarmupRequests `
                    $Requests `
                    -LatenciesMs $measurement.latencies `
                    -Errors $measurement.errors `
                    -StartupMs $startupWatch.Elapsed.TotalMilliseconds `
                    -RequestsPerSecond $measurement.requestsPerSecond `
                    -SuppressThroughput:($measurement.errors -ne 0) `
                    -ProcessProfile $measurement.profile `
                    -Correctness @{ checked = $true; status = $status; details = $(if ($measurement.errors -eq 0) { "status, content-type, and body validated" } else { $measurement.firstError }) } `
                    -Extra $extra
                continue
            }

            $latencies = @()
            $errors = 0
            $firstError = $null
            $profileBefore = $null
            for ($iteration = 0; $iteration -lt ($WarmupRequests + $Requests); $iteration += 1) {
                if ($iteration -eq $WarmupRequests) {
                    $profileBefore = Get-BenchProcessSnapshot $process
                }
                $request = $null
                $response = $null
                try {
                    $request = New-BenchHttpRequestMessage $workload $baseUrl
                    $watch = [System.Diagnostics.Stopwatch]::StartNew()
                    $response = $client.SendAsync($request).GetAwaiter().GetResult()
                    $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
                    $watch.Stop()
                    $contentType = $null
                    if ($null -ne $response.Content.Headers.ContentType) {
                        $contentType = $response.Content.Headers.ContentType.ToString()
                    }
                    $check = Test-BenchResponse $workload @{
                        status = [int]$response.StatusCode
                        body = $body
                        contentType = $contentType
                    }
                    if ($null -ne $check) {
                        $errors += 1
                        if ($null -eq $firstError) {
                            $firstError = $check
                        }
                    }
                    elseif ($iteration -ge $WarmupRequests) {
                        $latencies += $watch.Elapsed.TotalMilliseconds
                    }
                }
                catch {
                    $errors += 1
                    if ($null -eq $firstError) {
                        $firstError = Get-BenchExceptionMessage $_.Exception
                    }
                }
                finally {
                    if ($null -ne $response) {
                        $response.Dispose()
                    }
                    if ($null -ne $request) {
                        $request.Dispose()
                    }
                }
            }

            $profileAfter = Get-BenchProcessSnapshot $process
            $processProfile = Get-BenchProcessProfile $profileBefore $profileAfter $latencies.Count
            $status = $(if ($errors -eq 0) { "PASS" } else { "FAIL" })
            $reason = $(if ($errors -eq 0) { $null } else { $firstError })
            $results += New-BenchResult `
                $workload.id `
                $SuiteName `
                $RuntimeName `
                $status `
                $reason `
                $WarmupRequests `
                $Requests `
                -LatenciesMs $latencies `
                -Errors $errors `
                -StartupMs $startupWatch.Elapsed.TotalMilliseconds `
                -ProcessProfile $processProfile `
                -Correctness @{ checked = $true; status = $status; details = $(if ($errors -eq 0) { "status, content-type, and body validated" } else { $firstError }) } `
                -Extra (Get-BenchWorkloadExtra $workload)
        }

        return $results
    }
    finally {
        Stop-BenchProcess $process
        if ($null -ne $readyClient) {
            $readyClient.Dispose()
        }
        $client.Dispose()
        if ($env:SLOPPY_BENCH_KEEP_TEMP -ne "1") {
            Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-StartupSuiteForRuntime {
    param(
        [string]$RuntimeName,
        [System.Collections.IDictionary]$RuntimeInfo
    )

    $workloadId = "startup.server_to_first_health"
    if ($RuntimeInfo.status -ne "AVAILABLE") {
        return @(
            New-BenchResult "startup.build_minimal" "startup" $RuntimeName "UNAVAILABLE" $RuntimeInfo.reason 0 1
            New-BenchResult $workloadId "startup" $RuntimeName "UNAVAILABLE" $RuntimeInfo.reason 0 1
        )
    }

    $results = @()
    if ($RuntimeName -eq "sloppy") {
        $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-startup-bench-" + [Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
        try {
            New-SloppyBenchApp $tempDir "http" 0
            $watch = [System.Diagnostics.Stopwatch]::StartNew()
            try {
                Invoke-SloppyBuild $RuntimeInfo $tempDir
                $watch.Stop()
                $artifactDir = Join-Path $tempDir ".sloppy"
                $planPath = Join-Path $artifactDir "app.plan.json"
                $appPath = Join-Path $artifactDir "app.js"
                $planBytes = $(if (Test-Path -LiteralPath $planPath) {
                        (Get-Item -LiteralPath $planPath).Length
                    }
                    else {
                        $null
                    })
                $appBytes = $(if (Test-Path -LiteralPath $appPath) {
                        (Get-Item -LiteralPath $appPath).Length
                    }
                    else {
                        $null
                    })
                $artifactBytes = 0
                Get-ChildItem -LiteralPath $artifactDir -File -Recurse -ErrorAction SilentlyContinue |
                    ForEach-Object { $artifactBytes += $_.Length }
                $results += New-BenchResult `
                    "startup.build_minimal" `
                    "startup" `
                    $RuntimeName `
                    "PASS" `
                    $null `
                    0 `
                    1 `
                    -StartupMs $watch.Elapsed.TotalMilliseconds `
                    -Correctness @{ checked = $true; status = "PASS"; details = "sloppy build produced app.plan.json and app.js" } `
                    -Extra @{
                        buildMs = [Math]::Round($watch.Elapsed.TotalMilliseconds, 4)
                        artifactBytes = $artifactBytes
                        planBytes = $planBytes
                        appJsBytes = $appBytes
                    }
            }
            catch {
                $watch.Stop()
                $results += New-BenchResult `
                    "startup.build_minimal" `
                    "startup" `
                    $RuntimeName `
                    "FAIL" `
                    $_.Exception.Message `
                    0 `
                    1 `
                    -StartupMs $watch.Elapsed.TotalMilliseconds
            }
        }
        finally {
            Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    $result = @(Invoke-HttpSuiteForRuntime $RuntimeName $RuntimeInfo "http" @(
        @{ id = $workloadId; method = "GET"; path = "/health"; expectedStatus = 200; expectedBody = "ok"; expectedContentType = "text/plain" }
    ) 0)
    $result[0]["suite"] = "startup"
    return @($results + $result)
}

function Invoke-SqliteSuiteForRuntime {
    param(
        [string]$RuntimeName,
        [System.Collections.IDictionary]$RuntimeInfo
    )

    $ids = @(
        "sqlite.open_init",
        "sqlite.query_simple",
        "sqlite.query_one",
        "sqlite.insert",
        "sqlite.transaction_batch"
    )

    if ($RuntimeName -ne "sloppy") {
        return @($ids | ForEach-Object {
                New-BenchResult $_ "sqlite" $RuntimeName "SKIPPED" "SQLite/provider bridge suite is Sloppy-specific" 0 0
            })
    }

    return @($ids | ForEach-Object {
            New-BenchResult $_ "sqlite" $RuntimeName "SKIPPED" "SQLite/provider benchmark is deferred until the local V8 provider path is selected for this machine" 0 0
        })
}

function Invoke-CompareResults {
    param([string[]]$Paths)

    if ($Paths.Count -ne 2) {
        throw "-Compare expects exactly two result JSON paths"
    }

    $before = Get-Content -LiteralPath $Paths[0] -Raw | ConvertFrom-Json
    $after = Get-Content -LiteralPath $Paths[1] -Raw | ConvertFrom-Json
    $beforeMap = @{}
    foreach ($item in $before.benchmarks) {
        $beforeMap["$($item.runtime)|$($item.id)"] = $item
    }

    $rows = @()
    foreach ($item in $after.benchmarks) {
        $key = "$($item.runtime)|$($item.id)"
        $base = $beforeMap[$key]
        $rows += [ordered]@{
            id = $item.id
            runtime = $item.runtime
            beforeStatus = $(if ($null -eq $base) { $null } else { $base.status })
            afterStatus = $item.status
            beforeP95Ms = $(if ($null -eq $base) { $null } else { $base.p95Ms })
            afterP95Ms = $item.p95Ms
            deltaP95Ms = $(if ($null -ne $base -and $null -ne $base.p95Ms -and $null -ne $item.p95Ms) {
                    [Math]::Round($item.p95Ms - $base.p95Ms, 4)
                }
                else {
                    $null
                })
            beforeRequestsPerSecond = $(if ($null -eq $base) { $null } else { $base.requestsPerSecond })
            afterRequestsPerSecond = $item.requestsPerSecond
        }
    }

    return [ordered]@{
        schemaVersion = 1
        comparedAt = (Get-Date).ToUniversalTime().ToString("o")
        before = (Resolve-Path -LiteralPath $Paths[0]).Path
        after = (Resolve-Path -LiteralPath $Paths[1]).Path
        benchmarks = $rows
    }
}

if ($Compare.Count -gt 0) {
    $comparison = Invoke-CompareResults $Compare
    $json = $comparison | ConvertTo-Json -Depth 32
    if (-not [string]::IsNullOrWhiteSpace($Out)) {
        $outDir = Split-Path -Parent $Out
        if (-not [string]::IsNullOrWhiteSpace($outDir)) {
            New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        }
        $json | Set-Content -LiteralPath $Out -Encoding UTF8
    }
    else {
        $json
    }
    exit 0
}

$selectedSuites = Split-BenchSelection $Suite
if ($selectedSuites -contains "all") {
    $selectedSuites = $AllowedSuites
}
foreach ($suiteName in $selectedSuites) {
    if ($AllowedSuites -notcontains $suiteName) {
        throw "unknown benchmark suite '$suiteName'"
    }
}

$selectedRuntimes = Split-BenchSelection $Runtime
if ($selectedRuntimes -contains "all") {
    $selectedRuntimes = $AllowedRuntimes
}
foreach ($runtimeName in $selectedRuntimes) {
    if ($AllowedRuntimes -notcontains $runtimeName) {
        throw "unknown runtime '$runtimeName'"
    }
}

$runtimeInfo = [ordered]@{}
foreach ($runtimeName in $selectedRuntimes) {
    $runtimeInfo[$runtimeName] = Get-RuntimeInfo $runtimeName
}

$allResults = @()
foreach ($suiteName in $selectedSuites) {
    if ($suiteName -eq "http") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-HttpSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName] "http" (Get-HttpWorkloads)
        }
    }
    elseif ($suiteName -eq "route" -or $suiteName -eq "route.generated-table" -or $suiteName -eq "route.compact-prefix") {
        $routeShapes = @()
        if ($suiteName -eq "route") {
            $routeShapes = @("generated-table", "compact-prefix")
        }
        elseif ($suiteName -eq "route.generated-table") {
            $routeShapes = @("generated-table")
        }
        else {
            $routeShapes = @("compact-prefix")
        }
        foreach ($runtimeName in $selectedRuntimes) {
            foreach ($shape in $routeShapes) {
                foreach ($count in @(10, 100, 1000)) {
                    $workloads = @(Get-RouteWorkloads $shape | Where-Object { $_.routeCount -eq $count })
                    $allResults += Invoke-HttpSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName] "route.$shape" $workloads $count $shape
                }
            }
        }
    }
    elseif ($suiteName -eq "bridge") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-HttpSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName] "bridge" (Get-BridgeWorkloads)
        }
    }
    elseif ($suiteName -eq "concurrency") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-HttpSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName] "concurrency" (Get-ConcurrencyWorkloads)
        }
    }
    elseif ($suiteName -eq "middleware") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-HttpSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName] "middleware" (Get-MiddlewareWorkloads)
        }
    }
    elseif ($suiteName -eq "sqlite") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-SqliteSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName]
        }
    }
    elseif ($suiteName -eq "startup") {
        foreach ($runtimeName in $selectedRuntimes) {
            $allResults += Invoke-StartupSuiteForRuntime $runtimeName $runtimeInfo[$runtimeName]
        }
    }
}

$document = [ordered]@{
    schemaVersion = 1
    startedAt = (Get-Date).ToUniversalTime().ToString("o")
    git = Get-BenchGitInfo
    host = Get-BenchHostInfo
    configuration = [ordered]@{
        suites = @($selectedSuites)
        runtimes = @($selectedRuntimes)
        warmupRequests = $WarmupRequests
        requests = $Requests
        timeoutSeconds = $TimeoutSeconds
    }
    runtimes = $runtimeInfo
    benchmarks = $allResults
}

$jsonText = $document | ConvertTo-Json -Depth 32
if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $outDir = Split-Path -Parent $Out
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $jsonText | Set-Content -LiteralPath $Out -Encoding UTF8
}
else {
    $jsonText
}
