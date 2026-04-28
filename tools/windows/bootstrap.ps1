param()

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

$requiredTools = @(
    "git",
    "cmake",
    "ninja",
    "clang-cl",
    "lld-link",
    "cargo"
)

$missing = @()
$sloppyIsCi = -not [string]::IsNullOrWhiteSpace($env:CI) -and
    $env:CI -ne "0" -and
    $env:CI -ne "false"

try {
    Import-SlVisualStudioEnvironment
} catch {
    if ($sloppyIsCi) {
        throw
    }

    Write-Warning $_
}

Write-Host "Checking Sloppy foundation toolchain..."

foreach ($tool in $requiredTools) {
    $command = Get-Command $tool -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        Write-Host "[missing] $tool" -ForegroundColor Red
        $missing += $tool
    } else {
        Write-Host "[ok]      $tool -> $($command.Source)"
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing required tools: $($missing -join ', ')" -ForegroundColor Red
    Write-Host "Install the missing tools, then rerun tools/windows/bootstrap.ps1."
    Write-Host "TODO: add optional vcpkg bootstrap guidance once dependencies are introduced."
    exit 1
}

$hasCHeader = Test-SlEnvFile -EnvValue $env:INCLUDE -FileName "stdio.h"
$hasKernelLib = Test-SlEnvFile -EnvValue $env:LIB -FileName "kernel32.lib"
$hasRuntimeLib = (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrt.lib") -or
    (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrtd.lib")

Write-Host ""
Write-Host "Required tools are available."
if (-not ($hasCHeader -and $hasKernelLib -and $hasRuntimeLib)) {
    Write-Host ""
    $message = "This shell does not appear to have a complete MSVC/Windows SDK build environment."
    if ($sloppyIsCi) {
        Write-Host "Error: $message" -ForegroundColor Red
        Write-Host "Expected environment pieces include stdio.h, kernel32.lib, and msvcrt/msvcrtd.lib."
        exit 1
    }

    Write-Host "Warning: $message" -ForegroundColor Yellow
    Write-Host "Run from a Visual Studio Developer PowerShell/Command Prompt or fix the MSVC/Windows SDK installation."
    Write-Host "Expected environment pieces include stdio.h, kernel32.lib, and msvcrt/msvcrtd.lib."
} else {
    Write-Host "MSVC/Windows SDK build environment is available."
}

Write-Host "vcpkg manifest files are present, but no C dependencies are required yet."
Write-Host "V8 fetching is intentionally not implemented in this phase."
