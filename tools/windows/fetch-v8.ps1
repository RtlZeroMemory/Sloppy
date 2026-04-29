param(
    [string]$Destination = ".sdeps/v8",
    [string]$V8Root = "",
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

$ExpectedV8Revision = "7221f49fdb6c89cce6be08005732ebcab3c45b38"
$ExpectedCrLibcxxRevision = "af4386908c3762433d412689038de6e6333f5921"

function Write-ExpectedLayout {
    Write-Host "Expected SLOPPY_V8_ROOT layout:"
    Write-Host "  include/v8.h"
    Write-Host "  include/libplatform/libplatform.h"
    Write-Host "  lib/v8_monolith*.lib"
    Write-Host "  lib/v8_libplatform*.lib"
    Write-Host "  lib/v8_libbase*.lib"
    Write-Host "  lib/libc++*.lib  # required for current custom-libc++ V8 source builds"
    Write-Host "  support/libcxx/include/"
    Write-Host "  support/libcxx/buildtools/__config_site"
    Write-Host "    or split SDK libraries:"
    Write-Host "      lib/v8.lib"
    Write-Host "      lib/v8_libplatform*.lib"
    Write-Host "      lib/v8_libbase*.lib"
    Write-Host "  bin/  # optional runtime DLLs for dynamic SDKs"
    Write-Host "  share/sloppy-v8-sdk.json"
}

function Test-V8SdkManifest {
    param([string]$Root)

    $manifestPath = Join-Path $Root "share/sloppy-v8-sdk.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Write-Host "V8 SDK manifest is missing: share/sloppy-v8-sdk.json"
        return $false
    }

    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    } catch {
        Write-Host "V8 SDK manifest is not valid JSON: $manifestPath"
        Write-Host "  $($_.Exception.Message)"
        return $false
    }

    $errors = New-Object System.Collections.Generic.List[string]
    if ($manifest.name -ne "sloppy-v8-sdk") {
        $errors.Add("name must be sloppy-v8-sdk")
    }
    if ($manifest.platform -ne "windows-x64") {
        $errors.Add("platform must be windows-x64")
    }
    if ($manifest.v8Revision -ne $ExpectedV8Revision) {
        $errors.Add("v8Revision must be $ExpectedV8Revision")
    }
    if ($manifest.buildType -ne "release") {
        $errors.Add("buildType must be release")
    }
    if ($manifest.abi.crLibcxxRevision -ne $ExpectedCrLibcxxRevision) {
        $errors.Add("abi.crLibcxxRevision must be $ExpectedCrLibcxxRevision")
    }
    if ($manifest.abi.v8TargetArch -ne "x64") {
        $errors.Add("abi.v8TargetArch must be x64")
    }
    if ($manifest.abi.v8CompressPointers -ne $true) {
        $errors.Add("abi.v8CompressPointers must be true")
    }
    if ($manifest.abi.v8CompressPointersInSharedCage -ne $true) {
        $errors.Add("abi.v8CompressPointersInSharedCage must be true")
    }
    if ($manifest.abi.v8_31BitSmisOn64BitArch -ne $true) {
        $errors.Add("abi.v8_31BitSmisOn64BitArch must be true")
    }
    if ($manifest.abi.v8EnableSandbox -ne $true) {
        $errors.Add("abi.v8EnableSandbox must be true")
    }

    if ($errors.Count -gt 0) {
        Write-Host "V8 SDK manifest is incompatible:"
        foreach ($error in $errors) {
            Write-Host "  - $error"
        }
        return $false
    }

    return $true
}

function Test-V8SdkLayout {
    param([string]$Root)

    if ([string]::IsNullOrWhiteSpace($Root)) {
        Write-Host "SLOPPY_V8_ROOT is empty."
        return $false
    }

    $missing = New-Object System.Collections.Generic.List[string]

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        $missing.Add("SDK root directory: $Root")
    } else {
        $requiredFiles = @(
            "include/v8.h",
            "include/libplatform/libplatform.h"
        )

        foreach ($relativePath in $requiredFiles) {
            $path = Join-Path $Root $relativePath
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                $missing.Add($relativePath)
            }
        }

        $libDir = Join-Path $Root "lib"
        if (-not (Test-Path -LiteralPath $libDir -PathType Container)) {
            $missing.Add("lib/")
        } else {
            $monolithLibraries = @(Get-ChildItem -LiteralPath $libDir -Filter "v8_monolith*.lib" -File -ErrorAction SilentlyContinue)
            $coreLibraries = @(Get-ChildItem -LiteralPath $libDir -Filter "v8.lib" -File -ErrorAction SilentlyContinue)
            $libcxxLibraries = @(Get-ChildItem -LiteralPath $libDir -Filter "libc++*.lib" -File -ErrorAction SilentlyContinue)

            if ($monolithLibraries.Count -eq 0 -and $coreLibraries.Count -eq 0) {
                $missing.Add("lib/v8_monolith*.lib or lib/v8.lib")
            }

            $libraryPatterns = @(
                "v8_libplatform*.lib",
                "v8_libbase*.lib"
            )

            foreach ($pattern in $libraryPatterns) {
                $matches = @(Get-ChildItem -LiteralPath $libDir -Filter $pattern -File -ErrorAction SilentlyContinue)
                if ($matches.Count -eq 0) {
                    $missing.Add("lib/$pattern")
                }
            }

            if ($monolithLibraries.Count -gt 0 -and $libcxxLibraries.Count -gt 0) {
                $libcxxRequiredFiles = @(
                    "support/libcxx/include/memory",
                    "support/libcxx/buildtools/__config_site"
                )

                foreach ($relativePath in $libcxxRequiredFiles) {
                    $path = Join-Path $Root $relativePath
                    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                        $missing.Add($relativePath)
                    }
                }
            }

            if (($monolithLibraries.Count -gt 0 -or $coreLibraries.Count -gt 0) -and
                -not (Test-V8SdkManifest -Root $Root)) {
                $missing.Add("compatible share/sloppy-v8-sdk.json")
            }
        }
    }

    if ($missing.Count -gt 0) {
        Write-Host "V8 SDK layout is invalid. Missing:"
        foreach ($item in $missing) {
            Write-Host "  - $item"
        }
        Write-Host ""
        Write-ExpectedLayout
        return $false
    }

    Write-Host "V8 SDK layout is valid: $Root"
    return $true
}

$rootToValidate = $V8Root
if ([string]::IsNullOrWhiteSpace($rootToValidate) -and
    -not [string]::IsNullOrWhiteSpace($env:SLOPPY_V8_ROOT)) {
    $rootToValidate = $env:SLOPPY_V8_ROOT
}

if ($ValidateOnly) {
    if (-not (Test-V8SdkLayout -Root $rootToValidate)) {
        exit 1
    }

    exit 0
}

Write-Host "V8 SDK fetch is not implemented yet."
Write-Host "This helper intentionally downloads nothing until a pinned, verified SDK source exists."
Write-Host ""
Write-Host "Normal contributor workflow for TASK 07.A:"
Write-Host "  1. Obtain a Sloppy-compatible prebuilt V8 SDK from the future documented source."
Write-Host "  2. Set SLOPPY_V8_ROOT to the SDK root."
Write-Host "  3. Validate it with:"
Write-Host "       .\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root <sdk-root>"
Write-Host "  4. Configure V8 explicitly with:"
Write-Host "       .\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8 -V8Root <sdk-root>"
Write-Host ""
Write-Host "The default non-V8 build does not need this SDK."
Write-Host ""
Write-ExpectedLayout
Write-Host ""
Write-Host "Default future destination, once fetching is implemented: $Destination"

if (-not [string]::IsNullOrWhiteSpace($rootToValidate)) {
    Write-Host ""
    if (-not (Test-V8SdkLayout -Root $rootToValidate)) {
        exit 1
    }
}
