param(
    [switch]$SelfTest,
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath $Root).Path

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
            $RelativePath.StartsWith("src/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("tests/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("benchmarks/", [System.StringComparison]::OrdinalIgnoreCase))) {
        return $false
    }

    $extension = [System.IO.Path]::GetExtension($RelativePath)
    if ($extension -in $sourceExtensions) {
        return $true
    }

    return $RelativePath.StartsWith("src/cli/", [System.StringComparison]::OrdinalIgnoreCase) -and
        $extension -eq ".inc"
}

function Test-ImplementationPath {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("include/", [System.StringComparison]::OrdinalIgnoreCase) -or
        $RelativePath.StartsWith("src/", [System.StringComparison]::OrdinalIgnoreCase)
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
        $tracked = & $git.Source -C $Root ls-files -- include src tests benchmarks 2>$null
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
        (Join-Path $Root "src"),
        (Join-Path $Root "tests"),
        (Join-Path $Root "benchmarks")
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
$unsafeFunctionPattern = '\b(gets|strcpy|strncpy|strcat|sprintf|vsprintf|strdup)\s*\('
$cstringTerminatorPattern = '\bsl_str_copy_to_arena_nul\s*\('
$memoryPrimitivePattern = '\b(snprintf|strlen|memcpy|memmove|memcmp|memset)\s*\('
$ignoredStdioVoidCastPattern = '\(void\)\s*(snprintf|fprintf|fputs|printf|fputc)\s*\('
$allocPattern = '\b(malloc|free|realloc|calloc)\s*\('
$analysisSuppressionPattern = '\bNOLINT(?:NEXTLINE|BEGIN|END)?\b'
$validAnalysisSuppressionPattern = 'sloppy-analysis-suppress:\s*#\d+\s+.+;\s*remove when .+$'

function Test-AllowedMemoryPrimitiveBoundary {
    param(
        [string]$RelativePath,
        [string]$FunctionName
    )

    if ($RelativePath -eq "src/core/string.c" -and $FunctionName -eq "strlen") {
        return $true
    }

    if (($RelativePath -eq "src/core/string.c" -or $RelativePath -eq "src/core/bytes.c") -and
        $FunctionName -eq "memcmp") {
        return $true
    }

    return $false
}

function Test-AllowedCStringTerminatorBoundary {
    param([string]$RelativePath)

    return $RelativePath -eq "src/core/string.c" -or $RelativePath -eq "include/sloppy/string.h"
}

foreach ($file in $files) {
    $relativePath = Convert-ToRepoPath $file
    $lineNumber = 0
    $previousLine = ""
    $insideNolintBlock = $false

    foreach ($line in Get-Content -LiteralPath $file) {
        $lineNumber += 1
        $allowanceContext = "$previousLine $line"

        if ((Test-ImplementationPath $relativePath) -and $line -match $includePattern) {
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

        if ((Test-ImplementationPath $relativePath) -and
            ($line -match 'v8::') -and -not (Test-V8Path $relativePath)) {
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

        $isNolintBegin = $line -match '\bNOLINTBEGIN\b'
        $isNolintEnd = $line -match '\bNOLINTEND\b'
        $hasAnalysisSuppression = $line -match $analysisSuppressionPattern
        $hasValidAnalysisSuppression = $allowanceContext -match $validAnalysisSuppressionPattern
        if ($hasAnalysisSuppression -and
            -not ($isNolintEnd -and $insideNolintBlock) -and
            -not $hasValidAnalysisSuppression) {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern "NOLINT" `
                -Rule "Static-analysis suppressions need an issue, reason, and removal condition." `
                -Fix "Use `sloppy-analysis-suppress: #issue reason; remove when condition` on the suppression line." `
                -Severity "error"
        }
        if ($hasAnalysisSuppression -and $isNolintBegin -and $hasValidAnalysisSuppression) {
            $insideNolintBlock = $true
        }
        if ($hasAnalysisSuppression -and $isNolintEnd) {
            $insideNolintBlock = $false
        }

        if ((Test-ImplementationPath $relativePath) -and
            ($line -match $cstringTerminatorPattern) -and
            -not (Test-AllowedCStringTerminatorBoundary -RelativePath $relativePath))
        {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern "sl_str_copy_to_arena_nul" `
                -Rule "Raw NUL-append copies are not C-string boundary validation." `
                -Fix "Use sl_str_copy_to_arena_cstr for OS/env/config/app-host boundaries, or add a documented core exception." `
                -Severity "error"
        }

        if ((Test-ImplementationPath $relativePath) -and $line -match $memoryPrimitivePattern) {
            $functionName = $Matches[1]
            if (-not (Test-AllowedMemoryPrimitiveBoundary -RelativePath $relativePath -FunctionName $functionName)) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Pattern $functionName `
                    -Rule "Use Slop memory/string/buffer primitives instead of ad hoc low-level operations." `
                    -Fix "Use SlStr/SlBytes, arena copy helpers, SlStringBuilder/SlByteBuilder, checked helpers, or add a reusable primitive in the memory/string module." `
                    -Severity "error"
            }
        }

        if ($line -match $ignoredStdioVoidCastPattern) {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern ("(void)" + $Matches[1]) `
                -Rule "Do not cast ignored stdio/format return values to void." `
                -Fix "Call the function directly when failure is intentionally non-actionable, or check the return value when truncation or write failure matters." `
                -Severity "error"
        }

        if ((Test-ImplementationPath $relativePath) -and
            ($line -match $allocPattern) -and -not (Test-AllowedAllocPath $relativePath)) {
            $violations += New-Finding `
                -File $relativePath `
                -Line $lineNumber `
                -Pattern $Matches[1] `
                -Rule "Raw allocation belongs in allocator modules." `
                -Fix "Use future allocator APIs; see docs/c-standards.md." `
                -Severity "error"
        }

        $previousLine = $line
    }
}

if ($SelfTest) {
    $powerShell = (Get-Command pwsh, powershell -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if ($null -eq $powerShell) {
        throw "PowerShell was not found for C standards scanner self-test."
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-c-standards-" + [System.Guid]::NewGuid().ToString("N"))
    $validRoot = Join-Path $tempRoot "valid"
    $invalidRoot = Join-Path $tempRoot "invalid"
    try {
        New-Item -ItemType Directory -Force -Path (Join-Path $validRoot "src/core") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $validRoot "tests/unit/core") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $invalidRoot "src/core") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $invalidRoot "src/cli") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $invalidRoot "tests/unit/core") | Out-Null

        Set-Content -LiteralPath (Join-Path $validRoot "src/core/string.c") -Value @'
#include <string.h>
size_t ok_strlen(const char* text) { return strlen(text); }
int ok_memcmp(const char* a, const char* b, size_t n) { return memcmp(a, b, n); }
'@
        Set-Content -LiteralPath (Join-Path $validRoot "tests/unit/core/test_ok.c") -Value @'
/* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): sloppy-analysis-suppress: #805 fixture suppression; remove when fixture is replaced */
int ok_suppression(void) { return 0; }
/* NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): sloppy-analysis-suppress: #805 fixture block suppression; remove when fixture is replaced */
int ok_block_suppression(void) { int value = 1; return value; }
/* NOLINTEND(clang-analyzer-deadcode.DeadStores) */
void ok_write(FILE* file) { fputs("ok", file); }
'@

        Set-Content -LiteralPath (Join-Path $invalidRoot "src/core/bad.c") -Value @'
#include <string.h>
#include <stdlib.h>
void bad_copy(char* dst, const char* src) { strcpy(dst, src); }
void bad_memset(char* dst) { memset(dst, 0, 8); }
void* bad_alloc(void) { return malloc(16); }
/* NOLINTNEXTLINE(clang-analyzer-core.NullDereference) */
int bad_suppression(void) { return 0; }
void bad_void_cast(FILE* file) { (void)fputs("bad", file); }
'@
        Set-Content -LiteralPath (Join-Path $invalidRoot "src/cli/bad_fragment.inc") -Value @'
#include <string.h>
void bad_fragment_copy(char* dst, const char* src) { strcpy(dst, src); }
'@
        Set-Content -LiteralPath (Join-Path $invalidRoot "tests/unit/core/test_bad.c") -Value @'
#include <string.h>
void bad_test_copy(char* dst, const char* src) { strncpy(dst, src, 4); }
'@

        & $powerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Root $validRoot | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "C standards scanner self-test valid fixture failed."
        }

        $invalidOutput = & $powerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Root $invalidRoot *>&1
        if ($LASTEXITCODE -eq 0) {
            throw "C standards scanner self-test invalid fixture unexpectedly passed."
        }
        if (-not (($invalidOutput -join "`n") -match "src/cli/bad_fragment\.inc")) {
            throw "C standards scanner self-test did not scan CLI .inc fragments."
        }

        Write-Host "C standards scanner self-test passed."
        exit 0
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
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
