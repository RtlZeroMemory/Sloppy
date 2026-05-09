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

function Test-Tool {
    param([string]$Name)

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    return $null -ne $command
}

function Invoke-OptionalTool {
    param(
        [string]$Lane,
        [string]$Tool,
        [string[]]$Arguments
    )

    if (-not (Test-Tool $Tool)) {
        Write-Evidence $Lane "UNAVAILABLE" "$Tool is not on PATH"
        return
    }
    if ([string]::IsNullOrWhiteSpace($Url)) {
        Write-Evidence $Lane "SKIPPED" "provide -Url to run $Tool against a live HTTP/2 endpoint"
        return
    }

    & $Tool @Arguments
    if ($LASTEXITCODE -eq 0) {
        Write-Evidence $Lane "PASS" "$Tool completed"
    } else {
        Write-Evidence $Lane "FAIL" "$Tool exited with code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
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
    if (-not [string]::IsNullOrWhiteSpace($Url)) {
        $target = [Uri]$Url
        $port = if ($target.Port -gt 0) { $target.Port } elseif ($target.Scheme -eq "http") { 80 } else { 443 }
        $h2specArgs = @("-h", $target.Host, "-p", [string]$port)
        if ($target.Scheme -eq "https") {
            $h2specArgs += "-t"
        }
    }
    Invoke-OptionalTool "http2.h2spec" "h2spec" $h2specArgs
}
if (-not $ranExternal -or $Curl) {
    Invoke-OptionalTool "http2.curl" "curl.exe" @("--http2", "--fail", "--silent", "--show-error", $Url)
}
if (-not $ranExternal -or $Nghttp) {
    Invoke-OptionalTool "http2.nghttp" "nghttp" @("-nv", $Url)
}
if (-not $ranExternal -or $H2LoadSmoke) {
    Invoke-OptionalTool "http2.h2load" "h2load" @("-n", "10", "-c", "1", $Url)
}
