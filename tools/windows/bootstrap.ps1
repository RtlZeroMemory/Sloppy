param(
    [ValidateSet("OFF", "AUTO", "REQUIRED")]
    [string]$V8Mode = "AUTO",
    [switch]$ProvisionV8
)

$ErrorActionPreference = "Stop"

$doctorScript = Join-Path $PSScriptRoot "deps-doctor.ps1"
$fetchV8Script = Join-Path $PSScriptRoot "fetch-v8.ps1"

Write-Host "Checking Sloppy foundation toolchain..."
if ($ProvisionV8 -and $V8Mode -ne "OFF") {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $fetchV8Script
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    Write-Host ""
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $doctorScript -V8Mode $V8Mode
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Bootstrap validation completed."
Write-Host "vcpkg manifest mode is required for C dependencies such as yyjson, llhttp, libuv, OpenSSL, sqlite3, libpq, and ODBC where enabled."
Write-Host "V8 modes:"
Write-Host "  OFF      Do not validate or enable V8."
Write-Host "  AUTO     Report a compatible SDK when one is present; absence is optional unavailable."
Write-Host "  REQUIRED Fail when a compatible SDK is missing, wrong, or corrupt."
Write-Host "  -ProvisionV8 downloads the pinned Windows x64 SDK artifact when V8Mode is not OFF."
