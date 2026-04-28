param(
    [switch]$StrictAlloc
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

$sourceExtensions = @(".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx")
$excludedPrefixes = @(
    "build/",
    "compiler/target/",
    "target/",
    ".sdeps/",
    ".sloppy/",
    "vcpkg_installed/",
    ".git/"
)

$forbiddenOsHeaders = @(
    "windows.h",
    "winsock2.h",
    "ws2tcpip.h",
    "io.h",
    "unistd.h",
    "pthread.h",
    "sys/epoll.h",
    "sys/event.h"
)

$allowedAllocPrefixes = @(
    "src/core/arena.",
    "src/core/alloc.",
    "src/memory/"
)

function Convert-ToRepoPath {
    param([string]$Path)

    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    return $fullPath.Substring($Root.Length).TrimStart('\', '/') -replace "\\", "/"
}

function Test-ExcludedPath {
    param([string]$RelativePath)

    foreach ($prefix in $excludedPrefixes) {
        if ($RelativePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Test-SourcePath {
    param([string]$RelativePath)

    if (-not ($RelativePath.StartsWith("include/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("src/", [System.StringComparison]::OrdinalIgnoreCase))) {
        return $false
    }

    $extension = [System.IO.Path]::GetExtension($RelativePath)
    return $extension -in $sourceExtensions
}

function Test-PlatformPath {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("src/platform/", [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-V8Path {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("src/engine/v8/", [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-AllowedAllocPath {
    param([string]$RelativePath)

    foreach ($prefix in $allowedAllocPrefixes) {
        if ($RelativePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function New-Finding {
    param(
        [string]$File,
        [int]$Line,
        [string]$Pattern,
        [string]$Rule,
        [string]$Fix,
        [string]$Severity
    )

    [pscustomobject]@{
        File = $File
        Line = $Line
        Pattern = $Pattern
        Rule = $Rule
        Fix = $Fix
        Severity = $Severity
    }
}

function Get-TrackedSourceFiles {
    $git = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        return @()
    }

    Push-Location $Root
    try {
        $tracked = & $git.Source ls-files -- include src
        if ($LASTEXITCODE -ne 0) {
            return @()
        }

        return @($tracked | Where-Object {
                (Test-SourcePath $_) -and
                -not (Test-ExcludedPath $_) -and
                (Test-Path -LiteralPath (Join-Path $Root $_))
            } | ForEach-Object { Join-Path $Root $_ })
    } finally {
        Pop-Location
    }
}

function Get-RecursiveSourceFiles {
    $paths = @(
        (Join-Path $Root "include"),
        (Join-Path $Root "src")
    )

    return @(Get-ChildItem -Path $paths -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            $relativePath = Convert-ToRepoPath $_.FullName
            if ((Test-SourcePath $relativePath) -and -not (Test-ExcludedPath $relativePath)) {
                $_.FullName
            }
        })
}

$files = Get-TrackedSourceFiles
if ($files.Count -eq 0) {
    $files = Get-RecursiveSourceFiles
}

$violations = @()
$warnings = @()

$includePattern = '^\s*#\s*include\s*[<"]([^>"]+)[>"]'
$unsafeFunctionPattern = '\b(gets|strcpy|strcat|sprintf|vsprintf)\s*\('
$allocPattern = '\b(malloc|free|realloc|calloc)\s*\('

foreach ($file in $files) {
    $relativePath = Convert-ToRepoPath $file
    $lineNumber = 0

    foreach ($line in Get-Content -LiteralPath $file) {
        $lineNumber += 1

        if ($line -match $includePattern) {
            $header = $Matches[1]
            if (($header -in $forbiddenOsHeaders) -and -not (Test-PlatformPath $relativePath)) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Pattern "#include <$header>" `
                    -Rule "OS headers are allowed only under src/platform/*." `
                    -Fix "Move platform behavior behind docs/platform-abstraction.md." `
                    -Severity "error"
            }

            if (($header -match '^v8') -and -not (Test-V8Path $relativePath)) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Pattern "#include <$header>" `
                    -Rule "V8 headers are allowed only under src/engine/v8/*." `
                    -Fix "Use the engine-neutral bridge described in docs/c-standards.md." `
                    -Severity "error"
            }
        }

        if (($line -match 'v8::') -and -not (Test-V8Path $relativePath)) {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern "v8::" `
                -Rule "V8 types must not leak outside src/engine/v8/*." `
                -Fix "Move V8 usage behind the bridge described in docs/architecture.md." `
                -Severity "error"
        }

        if ($line -match $unsafeFunctionPattern) {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern $Matches[1] `
                -Rule "Unsafe C string/format functions are forbidden." `
                -Fix "Use bounded helpers and follow docs/c-standards.md." `
                -Severity "error"
        }

        if (($line -match $allocPattern) -and -not (Test-AllowedAllocPath $relativePath)) {
            $finding = New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern $Matches[1] `
                -Rule "Raw allocation belongs in allocator modules." `
                -Fix "Use future allocator APIs; see docs/c-standards.md." `
                -Severity "warning"

            if ($StrictAlloc) {
                $finding.Severity = "error"
                $violations += $finding
            } else {
                $warnings += $finding
            }
        }
    }
}

function Write-Finding {
    param([object]$Finding)

    Write-Host ("  {0}:{1}: {2}: {3}" -f $Finding.File, $Finding.Line, $Finding.Severity, $Finding.Rule)
    Write-Host ("    pattern: {0}" -f $Finding.Pattern)
    Write-Host ("    suggested fix: {0}" -f $Finding.Fix)
}

if ($warnings.Count -gt 0) {
    Write-Host "C standards warnings found:" -ForegroundColor Yellow
    foreach ($warning in $warnings) {
        Write-Finding $warning
    }
}

if ($violations.Count -gt 0) {
    Write-Host "C standards violations found:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Finding $violation
    }
    exit 1
}

Write-Host "C standards check passed."
