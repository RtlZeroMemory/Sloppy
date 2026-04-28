param()

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

$requiredTools = @(
    "git",
    "cmake",
    "ninja",
    "vcpkg",
    "clang-cl",
    "lld-link",
    "cargo"
)

$missing = @()
$sloppyIsCi = -not [string]::IsNullOrWhiteSpace($env:CI) -and
    $env:CI -ne "0" -and
    $env:CI -ne "false"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

function Find-SlVcpkg {
    $localCandidate = Join-Path $root ".sdeps/vcpkg/vcpkg.exe"
    if (Test-Path -LiteralPath $localCandidate) {
        return $localCandidate
    }

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidate = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $command = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return $null
}

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
    if ($tool -eq "vcpkg") {
        $source = Find-SlVcpkg
    } else {
        $command = Get-Command $tool -ErrorAction SilentlyContinue
        $source = if ($null -eq $command) { $null } else { $command.Source }
    }

    if ($null -eq $source) {
        Write-Host "[missing] $tool" -ForegroundColor Red
        $missing += $tool
    } else {
        Write-Host "[ok]      $tool -> $source"
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing required tools: $($missing -join ', ')" -ForegroundColor Red
    Write-Host "Install the missing tools, then rerun tools/windows/bootstrap.ps1."
    Write-Host "Set VCPKG_ROOT or put vcpkg on PATH so CMake can install manifest dependencies."
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

Write-Host "vcpkg manifest mode is required for C dependencies such as yyjson."
Write-Host "V8 remains optional. Use tools/windows/build-v8.ps1 for maintainer source builds or fetch-v8.ps1 -ValidateOnly for an existing SDK."
