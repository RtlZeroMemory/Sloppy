param(
    [switch]$SelfTest,
    [string]$ScanRoot
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ([string]::IsNullOrWhiteSpace($ScanRoot)) {
    $Root = $RepoRoot
} else {
    $Root = (Resolve-Path -LiteralPath $ScanRoot).Path
}

$forbiddenHeaders = @(
    "windows.h",
    "winsock2.h",
    "io.h",
    "unistd.h",
    "pthread.h",
    "sys/epoll.h",
    "sys/event.h"
)

function Test-AllowedPlatformPath {
    param(
        [string]$RelativePath,
        [string]$Header
    )

    $normalized = $RelativePath -replace "\\", "/"

    if ($Header -in @("windows.h", "winsock2.h", "io.h")) {
        return $normalized.StartsWith("src/platform/win32/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    if ($Header -in @("unistd.h", "pthread.h")) {
        return $normalized.StartsWith("src/platform/posix/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith("src/platform/linux/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith("src/platform/macos/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    if ($Header -eq "sys/epoll.h") {
        return $normalized.StartsWith("src/platform/linux/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    if ($Header -eq "sys/event.h") {
        return $normalized.StartsWith("src/platform/macos/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    return $false
}

$includePattern = '^\s*#\s*include\s*[<"]([^>"]+)[>"]'

function Get-PlatformBoundaryFiles {
    param([string]$RootPath)

    $paths = @(
        (Join-Path $RootPath "include"),
        (Join-Path $RootPath "src")
    ) | Where-Object { Test-Path -LiteralPath $_ }

    if ($paths.Count -eq 0) {
        return @()
    }

    return @(Get-ChildItem -Path $paths -Recurse -File |
        Where-Object { $_.Extension -in @(".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx") })
}

function Invoke-PlatformBoundaryScan {
    param([string]$RootPath)

    $files = Get-PlatformBoundaryFiles -RootPath $RootPath
    $violations = @()

    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($RootPath.Length).TrimStart('\', '/') -replace "\\", "/"
        $lineNumber = 0

        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber += 1
            if ($line -notmatch $includePattern) {
                continue
            }

            $header = $Matches[1]
            if ($header -notin $forbiddenHeaders) {
                continue
            }

            if (-not (Test-AllowedPlatformPath -RelativePath $relativePath -Header $header)) {
                $violations += "${relativePath}:${lineNumber}: forbidden platform header <$header>"
            }
        }
    }

    return $violations
}

function New-SelfTestFile {
    param(
        [string]$Path,
        [string]$Header
    )

    $directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    Set-Content -LiteralPath $Path -Value "#include <$Header>" -Encoding ascii
}

function Invoke-PlatformBoundarySelfTest {
    $fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-platform-boundary-self-test-" + [System.Guid]::NewGuid().ToString("N"))

    try {
        New-SelfTestFile -Path (Join-Path $fixtureRoot "include/core_windows_forbidden.h") -Header "windows.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/core/posix_forbidden.c") -Header "unistd.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/platform/win32/allowed_windows.c") -Header "windows.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/platform/win32/allowed_winsock.c") -Header "winsock2.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/platform/posix/allowed_posix.c") -Header "unistd.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/platform/linux/allowed_epoll.c") -Header "sys/epoll.h"
        New-SelfTestFile -Path (Join-Path $fixtureRoot "src/platform/macos/allowed_event.c") -Header "sys/event.h"

        $violations = Invoke-PlatformBoundaryScan -RootPath $fixtureRoot
        $expected = @(
            "include/core_windows_forbidden.h:1: forbidden platform header <windows.h>",
            "src/core/posix_forbidden.c:1: forbidden platform header <unistd.h>"
        )

        foreach ($expectedViolation in $expected) {
            if ($expectedViolation -notin $violations) {
                throw "platform boundary self-test did not report expected violation: $expectedViolation"
            }
        }

        if ($violations.Count -ne $expected.Count) {
            throw "platform boundary self-test expected $($expected.Count) violations, found $($violations.Count): $($violations -join '; ')"
        }
    } finally {
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
        }
    }

    Write-Host "platform boundary self-test passed."
}

if ($SelfTest) {
    Invoke-PlatformBoundarySelfTest
    exit 0
}

Invoke-PlatformBoundarySelfTest

$violations = Invoke-PlatformBoundaryScan -RootPath $Root

if ($violations.Count -gt 0) {
    Write-Host "Platform boundary violations found:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Host "  $violation" -ForegroundColor Red
    }
    exit 1
}

Write-Host "platform boundary check passed."
