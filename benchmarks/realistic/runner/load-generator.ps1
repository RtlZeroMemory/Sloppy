param(
    [Parameter(Mandatory = $true)][string]$BaseUrl,
    [Parameter(Mandatory = $true)][string]$RequestsJson,
    [int]$Connections = 1,
    [int]$DurationSeconds = 5,
    [int]$WarmupSeconds = 0,
    [long]$Seed = 1,
    [switch]$Single,
    [switch]$CaptureBody
)

$ErrorActionPreference = "Stop"

function New-RequestMessage($BaseUrl, $Spec) {
    $uri = [Uri]::new([Uri]::new($BaseUrl), [string]$Spec.path)
    $method = if ($Spec.method) { [string]$Spec.method } else { "GET" }
    $message = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($method), $uri)
    if ($Spec.headers) {
        foreach ($header in $Spec.headers.PSObject.Properties) {
            [void]$message.Headers.TryAddWithoutValidation($header.Name, [string]$header.Value)
        }
    }
    if ($null -ne $Spec.body) {
        $message.Content = [System.Net.Http.StringContent]::new([string]$Spec.body, [Text.Encoding]::UTF8)
        if ($Spec.headers -and $Spec.headers."content-type") {
            $message.Content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse([string]$Spec.headers."content-type")
        }
    }
    return $message
}

function Invoke-One($Client, $BaseUrl, $Spec, [bool]$CaptureBody) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $message = New-RequestMessage $BaseUrl $Spec
        $response = $Client.SendAsync($message).GetAwaiter().GetResult()
        $bytes = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
        $sw.Stop()
        [pscustomobject]@{
            ok = $true
            timeout = $false
            statusCode = [int]$response.StatusCode
            bytes = $bytes.Length
            body = if ($CaptureBody) { [Text.Encoding]::UTF8.GetString($bytes) } else { "" }
            durationMs = $sw.Elapsed.TotalMilliseconds
        }
    } catch {
        $sw.Stop()
        $message = $_.Exception.Message
        [pscustomobject]@{
            ok = $false
            timeout = $message -match "timeout|timed out"
            statusCode = 0
            bytes = 0
            body = ""
            durationMs = $sw.Elapsed.TotalMilliseconds
            error = $message
        }
    }
}

function Get-Percentile($Values, [double]$Pct) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Min($sorted.Count - 1, [Math]::Max(0, [Math]::Ceiling(($Pct / 100.0) * $sorted.Count) - 1))
    return [double]$sorted[$index]
}

$requests = @($RequestsJson | ConvertFrom-Json)
$handler = [System.Net.Http.SocketsHttpHandler]::new()
$handler.MaxConnectionsPerServer = [Math]::Max(1, $Connections)
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(5)

if ($Single) {
    Invoke-One $client $BaseUrl $requests[0] $CaptureBody | ConvertTo-Json -Depth 8 -Compress
    exit 0
}

function Invoke-Phase([int]$Seconds, [bool]$Collect, [long]$PhaseSeed) {
    $jobs = for ($i = 0; $i -lt $Connections; $i++) {
        Start-ThreadJob -ArgumentList $BaseUrl, $RequestsJson, $Seconds, $Collect, ([long]($PhaseSeed + $i * 7919)) -ScriptBlock {
            param($BaseUrl, $RequestsJson, $Seconds, $Collect, $WorkerSeed)
            function New-RequestMessage($BaseUrl, $Spec) {
                $uri = [Uri]::new([Uri]::new($BaseUrl), [string]$Spec.path)
                $method = if ($Spec.method) { [string]$Spec.method } else { "GET" }
                $message = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($method), $uri)
                if ($Spec.headers) {
                    foreach ($header in $Spec.headers.PSObject.Properties) {
                        [void]$message.Headers.TryAddWithoutValidation($header.Name, [string]$header.Value)
                    }
                }
                if ($null -ne $Spec.body) {
                    $message.Content = [System.Net.Http.StringContent]::new([string]$Spec.body, [Text.Encoding]::UTF8)
                    if ($Spec.headers -and $Spec.headers."content-type") {
                        $message.Content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse([string]$Spec.headers."content-type")
                    }
                }
                return $message
            }
            function Invoke-One($Client, $BaseUrl, $Spec) {
                $sw = [Diagnostics.Stopwatch]::StartNew()
                try {
                    $message = New-RequestMessage $BaseUrl $Spec
                    $response = $Client.SendAsync($message).GetAwaiter().GetResult()
                    $bytes = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
                    $sw.Stop()
                    [pscustomobject]@{ ok = $true; timeout = $false; statusCode = [int]$response.StatusCode; bytes = $bytes.Length; durationMs = $sw.Elapsed.TotalMilliseconds }
                } catch {
                    $sw.Stop()
                    $m = $_.Exception.Message
                    [pscustomobject]@{ ok = $false; timeout = $m -match "timeout|timed out"; statusCode = 0; bytes = 0; durationMs = $sw.Elapsed.TotalMilliseconds; error = $m }
                }
            }
            $handler = [System.Net.Http.SocketsHttpHandler]::new()
            $handler.MaxConnectionsPerServer = 1
            $client = [System.Net.Http.HttpClient]::new($handler)
            $client.Timeout = [TimeSpan]::FromSeconds(5)
            $requests = @($RequestsJson | ConvertFrom-Json)
            $weighted = @($requests | ForEach-Object { [pscustomobject]@{ spec = $_; weight = if ($_.weight) { [int]$_.weight } else { 1 } } })
            $totalWeight = ($weighted | Measure-Object -Property weight -Sum).Sum
            $deadline = [DateTimeOffset]::UtcNow.AddSeconds($Seconds)
            $state = [uint32]([int64]$WorkerSeed -band 0xffffffffL)
            $latencies = New-Object System.Collections.Generic.List[double]
            $totalRequests = [long]0; $bytes = [long]0; $errors = [long]0; $timeouts = [long]0; $non2xx = [long]0
            $errorTypes = @{}
            while ([DateTimeOffset]::UtcNow -lt $deadline) {
                $state = [uint32](((1664525L * [uint64]$state + 1013904223L) -band 0xffffffffL))
                $pick = ([double]$state / [double][uint32]::MaxValue) * $totalWeight
                $cursor = 0
                $spec = $weighted[$weighted.Count - 1].spec
                foreach ($entry in $weighted) {
                    $cursor += $entry.weight
                    if ($pick -le $cursor) { $spec = $entry.spec; break }
                }
                $result = Invoke-One $client $BaseUrl $spec
                if (-not $Collect) { continue }
                if ($result.ok) {
                    $totalRequests++
                    $bytes += [long]$result.bytes
                    [void]$latencies.Add([double]$result.durationMs)
                    if ($result.statusCode -lt 200 -or $result.statusCode -ge 300) { $non2xx++ }
                } else {
                    $errors++
                    if ($result.timeout) { $timeouts++ }
                    $key = [string]$result.error
                    $errorTypes[$key] = 1 + [int]($errorTypes[$key] ?? 0)
                }
            }
            [pscustomobject]@{ totalRequests = $totalRequests; latencies = @($latencies); bytes = $bytes; errors = $errors; timeouts = $timeouts; non2xx = $non2xx; errorTypes = $errorTypes }
        }
    }
    $parts = Receive-Job -Job $jobs -Wait -AutoRemoveJob
    $latencies = New-Object System.Collections.Generic.List[double]
    $totalRequests = [long]0; $bytes = [long]0; $errors = [long]0; $timeouts = [long]0; $non2xx = [long]0
    $errorTypes = @{}
    foreach ($part in $parts) {
        $totalRequests += [long]$part.totalRequests
        $bytes += [long]$part.bytes
        $errors += [long]$part.errors
        $timeouts += [long]$part.timeouts
        $non2xx += [long]$part.non2xx
        foreach ($value in @($part.latencies)) { [void]$latencies.Add([double]$value) }
        if ($part.errorTypes) {
            foreach ($entry in $part.errorTypes.GetEnumerator()) {
                $errorTypes[$entry.Key] = [int]($errorTypes[$entry.Key] ?? 0) + [int]$entry.Value
            }
        }
    }
    [pscustomobject]@{
        totalRequests = $totalRequests
        latencies = @($latencies)
        bytes = $bytes
        errors = $errors
        timeouts = $timeouts
        non2xx = $non2xx
        errorTypes = $errorTypes
    }
}

if ($WarmupSeconds -gt 0) {
    [void](Invoke-Phase $WarmupSeconds $false ($Seed -bxor 0x9e3779b9))
}
$samples = Invoke-Phase $DurationSeconds $true $Seed
$lat = @($samples.latencies)
$sum = 0.0
foreach ($value in $lat) { $sum += [double]$value }
$result = [pscustomobject]@{
    requestsPerSecond = if ($DurationSeconds -gt 0) { [double]$samples.totalRequests / $DurationSeconds } else { 0 }
    latency = [pscustomobject]@{
        avgMs = if ($samples.totalRequests -gt 0) { $sum / [double]$samples.totalRequests } else { $null }
        p50Ms = Get-Percentile $lat 50
        p75Ms = Get-Percentile $lat 75
        p90Ms = Get-Percentile $lat 90
        p95Ms = Get-Percentile $lat 95
        p99Ms = Get-Percentile $lat 99
        maxMs = if ($lat.Count -gt 0) { [double](@($lat | Sort-Object)[-1]) } else { $null }
    }
    transfer = [pscustomobject]@{
        bytesPerSecond = if ($DurationSeconds -gt 0) { [double]$samples.bytes / $DurationSeconds } else { 0 }
        totalRequests = $samples.totalRequests
        errors = $samples.errors
        timeouts = $samples.timeouts
        non2xx = $samples.non2xx
    }
    raw = [pscustomobject]@{
        totalRequests = $samples.totalRequests
        latencySampleCount = $lat.Count
        bytes = $samples.bytes
        errors = $samples.errors
        timeouts = $samples.timeouts
        non2xx = $samples.non2xx
        errorTypes = $samples.errorTypes
    }
}
$result | ConvertTo-Json -Depth 12 -Compress
