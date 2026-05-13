param(
    [switch]$SelfTest,
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath $Root).Path

function Convert-ToRepoPath {
    param([string]$Path)

    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    return $fullPath.Substring($Root.Length).TrimStart('\', '/') -replace "\\", "/"
}

function New-Finding {
    param(
        [string]$File,
        [int]$Line,
        [string]$Rule,
        [string]$Message
    )

    [pscustomobject]@{
        File = $File
        Line = $Line
        Rule = $Rule
        Message = $Message
    }
}

function Test-SourceTextPath {
    param([string]$RelativePath)

    $extension = [System.IO.Path]::GetExtension($RelativePath)
    if ($extension -in @(".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".inc")) {
        return $RelativePath.StartsWith("include/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("src/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("tests/", [System.StringComparison]::OrdinalIgnoreCase) -or
            $RelativePath.StartsWith("benchmarks/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    if ($extension -in @(".js", ".mjs", ".ts", ".md")) {
        return $RelativePath.StartsWith("examples/", [System.StringComparison]::OrdinalIgnoreCase)
    }

    return $false
}

function Get-TrackedFilesOrRecursive {
    $git = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -ne $git -and (Test-Path -LiteralPath (Join-Path $Root ".git"))) {
        $tracked = & $git.Source -C $Root ls-files -- include src tests benchmarks examples cmake tools/windows 2>$null
        if ($LASTEXITCODE -eq 0) {
            return @($tracked | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) })
        }
    }

    $roots = @("include", "src", "tests", "benchmarks", "examples", "cmake", "tools/windows") |
        ForEach-Object { Join-Path $Root $_ } |
        Where-Object { Test-Path -LiteralPath $_ }
    return @(Get-ChildItem -Path $roots -Recurse -File | ForEach-Object { Convert-ToRepoPath $_.FullName })
}

function Test-V8AllowedPath {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("src/engine/v8/", [System.StringComparison]::OrdinalIgnoreCase) -or
        $RelativePath.StartsWith("tests/unit/engine/", [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-LibuvAllowedPath {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("src/platform/libuv/", [System.StringComparison]::OrdinalIgnoreCase) -or
        $RelativePath -eq "tests/unit/core/test_http_transport.cc" -or
        $RelativePath -eq "tests/unit/core/test_net_tcp_client.cc" -or
        $RelativePath -eq "tests/unit/core/test_provider_executor.c"
}

function Test-ProviderNativeAllowedPath {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("src/data/", [System.StringComparison]::OrdinalIgnoreCase) -or
        $RelativePath -eq "src/engine/v8/intrinsics_postgres.cc" -or
        $RelativePath -eq "src/engine/v8/intrinsics_sqlite.cc" -or
        $RelativePath -eq "src/engine/v8/intrinsics_sqlserver.cc"
}

function Add-Finding {
    param(
        [System.Collections.ArrayList]$Findings,
        [string]$File,
        [int]$Line,
        [string]$Rule,
        [string]$Message
    )

    [void]$Findings.Add((New-Finding -File $File -Line $Line -Rule $Rule -Message $Message))
}

function Invoke-TextBoundaryScan {
    $findings = [System.Collections.ArrayList]::new()
    $includePattern = '^\s*#\s*include\s*[<"]([^>"]+)[>"]'

    foreach ($relativePath in Get-TrackedFilesOrRecursive) {
        $normalizedPath = $relativePath -replace "\\", "/"
        if (-not (Test-SourceTextPath $normalizedPath)) {
            continue
        }

        $fullPath = Join-Path $Root $normalizedPath
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $fullPath) {
            $lineNumber += 1
            $trimmedLine = $line.TrimStart()
            $header = $null
            if ($line -match $includePattern) {
                $header = ($Matches[1] -replace "\\", "/").Trim()
            }

            if ($normalizedPath.StartsWith("src/core/", [System.StringComparison]::OrdinalIgnoreCase) -and
                $null -ne $header -and
                ($header -match '(^|/)(engine/v8|src/engine|platform/(win32|posix|libuv)|src/platform|src/data|cli)/' -or
                    $header -match '^sloppy/data_(postgres|sqlite|sqlserver)\.h$'))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB001" `
                    "src/core must not include engine, platform backend, provider-specific, CLI, or compiler internals."
            }

            if ($normalizedPath.StartsWith("include/sloppy/", [System.StringComparison]::OrdinalIgnoreCase) -and
                $null -ne $header -and
                ($header -match '^(v8|uv|windows\.h|winsock2\.h|ws2tcpip\.h|unistd\.h|pthread\.h|libpq-fe\.h|sqlite3\.h|sql\.h|sqlext\.h)'))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB002" `
                    "public headers must not expose engine, platform, or provider dependency headers."
            }

            if (($line -match '\bv8::' -or ($null -ne $header -and $header -match '^v8')) -and
                -not (Test-V8AllowedPath $normalizedPath))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB003" `
                    "V8 types and headers are confined to src/engine/v8 and V8-specific tests."
            }

            if (($line -match '\buv_[A-Za-z0-9_]+\b|\buv_loop_t\b|\buv_tcp_t\b|\buv_handle_t\b' -or
                    ($null -ne $header -and $header -eq "uv.h")) -and
                -not (Test-LibuvAllowedPath $normalizedPath))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB004" `
                    "libuv types and headers are confined to src/platform/libuv and explicit libuv integration tests."
            }

            if ((-not ($trimmedLine.StartsWith("//") -or $trimmedLine.StartsWith("*") -or $trimmedLine.StartsWith("/*"))) -and
                ($line -match '\b(PGconn|PGresult|sqlite3|SQLHENV|SQLHDBC|SQLHSTMT|SQLHANDLE)\b' -or
                    ($null -ne $header -and $header -match '^(libpq-fe\.h|sqlite3\.h|sql\.h|sqlext\.h)$')) -and
                -not (Test-ProviderNativeAllowedPath $normalizedPath))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB005" `
                    "provider dependency types are confined to provider implementations and the scoped V8 provider bridge."
            }

            if ($normalizedPath.StartsWith("src/data/", [System.StringComparison]::OrdinalIgnoreCase) -and
                ($line -match '\bv8::' -or ($null -ne $header -and $header -match '^v8')))
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB006" `
                    "provider implementations must not enter V8 directly."
            }

            if ($normalizedPath.StartsWith("examples/", [System.StringComparison]::OrdinalIgnoreCase) -and
                -not $normalizedPath.EndsWith("/test.mjs", [System.StringComparison]::OrdinalIgnoreCase) -and
                $line -match '\.\./\.\./stdlib/sloppy')
            {
                Add-Finding $findings $normalizedPath $lineNumber "PB007" `
                    "public examples must import the Sloppy facade instead of reaching into ../../stdlib; tests may use source-relative stdlib imports."
            }
        }
    }

    return $findings
}

function Invoke-CMakeOwnershipScan {
    $findings = [System.Collections.ArrayList]::new()
    $sourcesPath = Join-Path $Root "cmake/SloppySources.cmake"
    if (-not (Test-Path -LiteralPath $sourcesPath)) {
        Add-Finding $findings "cmake/SloppySources.cmake" 0 "PB100" "CMake source ownership file is missing."
        return $findings
    }

    $sourcesText = Get-Content -LiteralPath $sourcesPath -Raw
    foreach ($variable in @(
            "SLOPPY_CORE_KERNEL_SOURCES",
            "SLOPPY_DATA_SOURCES",
            "SLOPPY_ENGINE_SOURCES",
            "SLOPPY_PLATFORM_COMMON_SOURCES",
            "SLOPPY_PLATFORM_SYSTEM_SOURCES",
            "SLOPPY_V8_SOURCES",
            "SLOPPY_C_LINT_SOURCES"
        )) {
        if ($sourcesText -notmatch [regex]::Escape($variable)) {
            Add-Finding $findings "cmake/SloppySources.cmake" 0 "PB101" "Missing CMake ownership variable $variable."
        }
    }

    $cmakeText = ""
    foreach ($file in @("CMakeLists.txt", "cmake/SloppySources.cmake", "cmake/SloppyCli.cmake")) {
        $path = Join-Path $Root $file
        if (Test-Path -LiteralPath $path) {
            $cmakeText += "`n" + (Get-Content -LiteralPath $path -Raw)
        }
    }

    $registeredSources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($match in [regex]::Matches($cmakeText, 'src/[A-Za-z0-9_./-]+\.(?:cxx|cpp|cc|c)')) {
        [void]$registeredSources.Add($match.Value)
    }

    $lintIndex = $sourcesText.IndexOf("SLOPPY_C_LINT_SOURCES", [System.StringComparison]::Ordinal)
    $ownershipText = if ($lintIndex -ge 0) { $sourcesText.Substring(0, $lintIndex) } else { $sourcesText }
    $duplicates = [regex]::Matches($ownershipText, 'src/[A-Za-z0-9_./-]+\.(?:cxx|cpp|cc|c)') |
        ForEach-Object { $_.Value } |
        Group-Object |
        Where-Object { $_.Count -gt 1 -and $_.Name -notin @("src/core/bytes_simd_avx2.c", "src/core/string_simd_avx2.c") }
    foreach ($duplicate in $duplicates) {
        Add-Finding $findings "cmake/SloppySources.cmake" 0 "PB102" "Duplicate CMake source registration for $($duplicate.Name)."
    }

    $trackedSources = Get-TrackedFilesOrRecursive |
        Where-Object {
            $_ -match '^src/(core|data|engine|platform)/.+\.(c|cc|cpp|cxx)$' -or
            $_ -in @("src/main.c", "src/cli/sloppyrc.c")
        }
    foreach ($source in $trackedSources) {
        if (-not $registeredSources.Contains($source)) {
            Add-Finding $findings $source 0 "PB103" "Tracked implementation source is not registered in the owned CMake source lists."
        }
    }

    if ($sourcesText -notmatch 'src/cli/sloppyrc\.c' -or $sourcesText -notmatch 'src/cli/\*\.inc') {
        $cStandardsPath = Join-Path $Root "tools/windows/check-c-standards.ps1"
        $cStandardsText = if (Test-Path -LiteralPath $cStandardsPath) {
            Get-Content -LiteralPath $cStandardsPath -Raw
        } else {
            ""
        }
        if ($cStandardsText -notmatch 'src/cli/' -or $cStandardsText -notmatch '\.inc') {
            Add-Finding $findings "tools/windows/check-c-standards.ps1" 0 "PB104" `
                "C standards scanner must cover src/cli/*.inc implementation fragments."
        }
    }

    return $findings
}

function Invoke-PhysicalBoundaryScan {
    $findings = [System.Collections.ArrayList]::new()
    foreach ($finding in Invoke-TextBoundaryScan) {
        [void]$findings.Add($finding)
    }
    foreach ($finding in Invoke-CMakeOwnershipScan) {
        [void]$findings.Add($finding)
    }
    return $findings
}

function Invoke-PhysicalBoundarySelfTest {
    $fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-physical-boundaries-" + [System.Guid]::NewGuid().ToString("N"))
    try {
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "src/core") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "src/engine/v8") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "src/platform/libuv") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "tests/unit/core") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "examples/bad") | Out-Null
        Set-Content -LiteralPath (Join-Path $fixtureRoot "src/core/bad.c") -Encoding ascii -Value @'
#include "sloppy/data_sqlite.h"
void bad(void) {}
'@
        Set-Content -LiteralPath (Join-Path $fixtureRoot "src/engine/v8/ok.cc") -Encoding ascii -Value @'
#include <v8.h>
void ok(v8::Isolate*) {}
'@
        Set-Content -LiteralPath (Join-Path $fixtureRoot "src/platform/libuv/ok.c") -Encoding ascii -Value @'
#include <uv.h>
void ok(void) { uv_loop_t loop; (void)loop; }
'@
        Set-Content -LiteralPath (Join-Path $fixtureRoot "tests/unit/core/test_provider_executor.c") -Encoding ascii -Value @'
#include <uv.h>
void ok(void) { uv_loop_t loop; (void)loop; }
'@
        Set-Content -LiteralPath (Join-Path $fixtureRoot "examples/bad/app.js") -Encoding ascii -Value @'
import { Sloppy } from "../../stdlib/sloppy/index.js";
'@
        Set-Content -LiteralPath (Join-Path $fixtureRoot "CMakeLists.txt") -Encoding ascii -Value @'
add_library(x src/core/bad.c src/engine/v8/ok.cc src/platform/libuv/ok.c)
'@
        New-Item -ItemType Directory -Force -Path (Join-Path $fixtureRoot "cmake") | Out-Null
        Set-Content -LiteralPath (Join-Path $fixtureRoot "cmake/SloppySources.cmake") -Encoding ascii -Value @'
set(SLOPPY_CORE_KERNEL_SOURCES src/core/bad.c)
set(SLOPPY_DATA_SOURCES)
set(SLOPPY_ENGINE_SOURCES)
set(SLOPPY_PLATFORM_COMMON_SOURCES src/platform/libuv/ok.c)
set(SLOPPY_PLATFORM_SYSTEM_SOURCES)
set(SLOPPY_V8_SOURCES src/engine/v8/ok.cc)
set(SLOPPY_C_LINT_SOURCES)
'@

        $script:Root = (Resolve-Path -LiteralPath $fixtureRoot).Path
        if (-not (Test-LibuvAllowedPath "tests/unit/core/test_provider_executor.c")) {
            throw "physical boundary self-test did not allow test_provider_executor.c"
        }
        $findings = Invoke-PhysicalBoundaryScan
        foreach ($expectedRule in @("PB001", "PB007")) {
            if ($expectedRule -notin @($findings | ForEach-Object { $_.Rule })) {
                throw "physical boundary self-test did not report $expectedRule"
            }
        }
        $providerExecutorFinding = @($findings | Where-Object {
                $_.Rule -eq "PB004" -and $_.File -eq "tests/unit/core/test_provider_executor.c"
            })
        if ($providerExecutorFinding.Count -ne 0) {
            throw "physical boundary self-test reported PB004 for allowed provider executor test"
        }
    } finally {
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
        }
        $script:Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
    }

    Write-Host "physical boundary scanner self-test passed."
}

if ($SelfTest) {
    Invoke-PhysicalBoundarySelfTest
    exit 0
}

Invoke-PhysicalBoundarySelfTest
$findings = Invoke-PhysicalBoundaryScan

if ($findings.Count -gt 0) {
    Write-Host "Physical boundary violations found:" -ForegroundColor Red
    foreach ($finding in $findings) {
        Write-Host ("  {0}:{1}: {2}: {3}" -f $finding.File, $finding.Line, $finding.Rule, $finding.Message) -ForegroundColor Red
    }
    exit 1
}

Write-Host "physical boundary check passed."
