param(
    [string]$Preset = "windows-dev",
    [string]$Url = "",
    [switch]$H2Spec,
    [switch]$Curl,
    [switch]$Nghttp,
    [switch]$H2LoadSmoke
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$BuildDir = Join-Path (Join-Path $Root "build") $Preset

function Write-Evidence {
    param(
        [string]$Lane,
        [string]$Status,
        [string]$Detail
    )

    Write-Host "$Lane`t$Status`t$Detail"
}

function Resolve-Tool {
    param([string]$Name)

    return Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
}

function Invoke-OptionalTool {
    param(
        [string]$Lane,
        [string]$Tool,
        [string[]]$Arguments
    )

    $command = Resolve-Tool $Tool
    if ($null -eq $command) {
        Write-Evidence $Lane "UNAVAILABLE" "$Tool is not on PATH"
        return
    }
    if ([string]::IsNullOrWhiteSpace($Url)) {
        Write-Evidence $Lane "SKIPPED" "provide -Url to run $Tool against a live HTTP/2 endpoint"
        return
    }

    & $command.Source @Arguments
    if ($LASTEXITCODE -eq 0) {
        Write-Evidence $Lane "PASS" "$Tool completed"
    } else {
        Write-Evidence $Lane "FAIL" "$Tool exited with code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

function Invoke-CurlHttp2 {
    $command = Resolve-Tool "curl.exe"
    if ($null -eq $command) {
        Write-Evidence "http2.curl" "UNAVAILABLE" "curl.exe is not on PATH"
        return
    }
    $version = & $command.Source --version
    if ($LASTEXITCODE -ne 0 -or -not (($version -join "`n") -match '\bHTTP2\b')) {
        Write-Evidence "http2.curl" "UNAVAILABLE" "curl.exe was built without HTTP/2 support"
        return
    }
    Invoke-OptionalTool "http2.curl" "curl.exe" @("--http2", "--fail", "--silent", "--show-error", $Url)
}

$ranExternal = $H2Spec -or $Curl -or $Nghttp -or $H2LoadSmoke

ctest --test-dir $BuildDir -R "core\.http2|conformance\.transport\.http2_" --output-on-failure
if ($LASTEXITCODE -eq 0) {
    Write-Evidence "http2.local_ctest" "PASS" "core and transport HTTP/2 lanes passed"
} else {
    Write-Evidence "http2.local_ctest" "FAIL" "CTest exited with code $LASTEXITCODE"
    exit $LASTEXITCODE
}

if (-not $ranExternal -or $H2Spec) {
    $h2specArgs = @()
    $h2specLane = "http2.h2spec"
    if (-not [string]::IsNullOrWhiteSpace($Url)) {
        try {
            $target = [Uri]$Url
        } catch {
            Write-Evidence $h2specLane "FAIL" "malformed -Url '$Url'"
            exit 1
        }
        $port = if ($target.Port -gt 0) { $target.Port } elseif ($target.Scheme -eq "http") { 80 } else { 443 }
        $h2specArgs = @("-h", $target.Host, "-p", [string]$port)
        if ($target.Scheme -eq "https") {
            $h2specArgs += "-t"
        }
    }
    Invoke-OptionalTool $h2specLane "h2spec" $h2specArgs
}
if (-not $ranExternal -or $Curl) {
    Invoke-CurlHttp2
}
if (-not $ranExternal -or $Nghttp) {
    Invoke-OptionalTool "http2.nghttp" "nghttp" @("-nv", $Url)
}
if (-not $ranExternal -or $H2LoadSmoke) {
    Invoke-OptionalTool "http2.h2load" "h2load" @("-n", "10", "-c", "1", $Url)
}
