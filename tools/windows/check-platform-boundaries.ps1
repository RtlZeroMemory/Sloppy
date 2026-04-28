param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

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

$paths = @(
    (Join-Path $Root "include"),
    (Join-Path $Root "src")
)

$files = Get-ChildItem -Path $paths -Recurse -File |
    Where-Object { $_.Extension -in @(".c", ".h", ".cc", ".cpp", ".hpp") }

$violations = @()
$includePattern = '^\s*#\s*include\s*[<"]([^>"]+)[>"]'

foreach ($file in $files) {
    $relativePath = $file.FullName.Substring($Root.Length).TrimStart('\', '/')
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

if ($violations.Count -gt 0) {
    Write-Host "Platform boundary violations found:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Host "  $violation" -ForegroundColor Red
    }
    exit 1
}

Write-Host "platform boundary check passed."
