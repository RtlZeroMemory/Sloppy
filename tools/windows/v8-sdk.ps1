$SlV8ExpectedRevision = "7221f49fdb6c89cce6be08005732ebcab3c45b38"
$SlV8ExpectedCrLibcxxRevision = "af4386908c3762433d412689038de6e6333f5921"
$SlV8DefaultPlatform = "windows-x64"
$SlDepsManifestPath = Join-Path $PSScriptRoot "../deps/sloppy-deps.json"
if (Test-Path -LiteralPath $SlDepsManifestPath -PathType Leaf) {
    $SlDepsManifest = Get-Content -LiteralPath $SlDepsManifestPath -Raw | ConvertFrom-Json
    if ($SlDepsManifest.v8Sdk.v8Revision) {
        $SlV8ExpectedRevision = [string]$SlDepsManifest.v8Sdk.v8Revision
    }
    if ($SlDepsManifest.v8Sdk.crLibcxxRevision) {
        $SlV8ExpectedCrLibcxxRevision = [string]$SlDepsManifest.v8Sdk.crLibcxxRevision
    }
}

function Write-SlV8ExpectedLayout {
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

function Test-SlV8SdkManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [switch]$Quiet
    )

    $manifestPath = Join-Path $Root "share/sloppy-v8-sdk.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        if (-not $Quiet) {
            Write-Host "V8 SDK manifest is missing: share/sloppy-v8-sdk.json"
        }
        return $false
    }

    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    } catch {
        if (-not $Quiet) {
            Write-Host "V8 SDK manifest is not valid JSON: $manifestPath"
            Write-Host "  $($_.Exception.Message)"
        }
        return $false
    }

    $errors = New-Object System.Collections.Generic.List[string]
    if ($manifest.name -ne "sloppy-v8-sdk") {
        $errors.Add("name must be sloppy-v8-sdk")
    }
    if ($manifest.platform -ne "windows-x64") {
        $errors.Add("platform must be windows-x64")
    }
    if ($manifest.v8Revision -ne $SlV8ExpectedRevision) {
        $errors.Add("v8Revision must be $SlV8ExpectedRevision")
    }
    if ($manifest.buildType -ne "release") {
        $errors.Add("buildType must be release")
    }
    if ($manifest.abi.crLibcxxRevision -ne $SlV8ExpectedCrLibcxxRevision) {
        $errors.Add("abi.crLibcxxRevision must be $SlV8ExpectedCrLibcxxRevision")
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
        if (-not $Quiet) {
            Write-Host "V8 SDK manifest is incompatible:"
            foreach ($manifestError in $errors) {
                Write-Host "  - $manifestError"
            }
        }
        return $false
    }

    return $true
}

function Test-SlV8SdkLayout {
    param(
        [string]$Root,

        [switch]$Quiet
    )

    if ([string]::IsNullOrWhiteSpace($Root)) {
        if (-not $Quiet) {
            Write-Host "SLOPPY_V8_ROOT is empty."
        }
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
            $monolithLibraries = @(
                Get-ChildItem -LiteralPath $libDir -Filter "v8_monolith*.lib" -File -ErrorAction SilentlyContinue
            )
            $coreLibraries = @(
                Get-ChildItem -LiteralPath $libDir -Filter "v8.lib" -File -ErrorAction SilentlyContinue
            )
            $libcxxLibraries = @(
                Get-ChildItem -LiteralPath $libDir -Filter "libc++*.lib" -File -ErrorAction SilentlyContinue
            )

            if ($monolithLibraries.Count -eq 0 -and $coreLibraries.Count -eq 0) {
                $missing.Add("lib/v8_monolith*.lib or lib/v8.lib")
            }

            foreach ($pattern in @("v8_libplatform*.lib", "v8_libbase*.lib")) {
                $libraryMatches = @(
                    Get-ChildItem -LiteralPath $libDir -Filter $pattern -File -ErrorAction SilentlyContinue
                )
                if ($libraryMatches.Count -eq 0) {
                    $missing.Add("lib/$pattern")
                }
            }

            if ($monolithLibraries.Count -gt 0 -and $libcxxLibraries.Count -gt 0) {
                foreach ($relativePath in @("support/libcxx/include/memory", "support/libcxx/buildtools/__config_site")) {
                    $path = Join-Path $Root $relativePath
                    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                        $missing.Add($relativePath)
                    }
                }
            }

            if (($monolithLibraries.Count -gt 0 -or $coreLibraries.Count -gt 0) -and
                -not (Test-SlV8SdkManifest -Root $Root -Quiet:$Quiet)) {
                $missing.Add("compatible share/sloppy-v8-sdk.json")
            }
        }
    }

    if ($missing.Count -gt 0) {
        if (-not $Quiet) {
            Write-Host "V8 SDK layout is invalid. Missing:"
            foreach ($item in $missing) {
                Write-Host "  - $item"
            }
            Write-Host ""
            Write-SlV8ExpectedLayout
        }
        return $false
    }

    if (-not $Quiet) {
        Write-Host "V8 SDK layout is valid: $Root"
    }
    return $true
}

function Add-SlV8Candidate {
    param(
        [System.Collections.Generic.List[string]]$Candidates,
        [string]$CandidatePath
    )

    if ([string]::IsNullOrWhiteSpace($CandidatePath)) {
        return
    }

    $expanded = [Environment]::ExpandEnvironmentVariables($CandidatePath)
    if (-not [System.IO.Path]::IsPathRooted($expanded)) {
        $expanded = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $expanded))
    } else {
        $expanded = [System.IO.Path]::GetFullPath($expanded)
    }

    foreach ($candidate in @(
            $expanded,
            (Join-Path $expanded $SlV8DefaultPlatform),
            (Join-Path $expanded "v8/$SlV8DefaultPlatform"),
            (Join-Path $expanded ".sdeps/v8/$SlV8DefaultPlatform")
        )) {
        if (-not $Candidates.Contains($candidate)) {
            $Candidates.Add($candidate)
        }
    }
}

function Add-SlV8PathListCandidates {
    param(
        [System.Collections.Generic.List[string]]$Candidates,
        [string]$PathList
    )

    if ([string]::IsNullOrWhiteSpace($PathList)) {
        return
    }

    foreach ($path in $PathList.Split([System.IO.Path]::PathSeparator)) {
        Add-SlV8Candidate -Candidates $Candidates -CandidatePath $path
    }
}

function Get-SlV8GitWorktreeRoots {
    param([string]$RepoRoot)

    $git = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        return @()
    }
    if ([string]::IsNullOrWhiteSpace($RepoRoot) -or
        -not (Test-Path -LiteralPath (Join-Path $RepoRoot ".git")))
    {
        return @()
    }

    $output = & $git.Source -C $RepoRoot worktree list --porcelain 2>$null
    if ($LASTEXITCODE -ne 0) {
        return @()
    }

    $roots = New-Object System.Collections.Generic.List[string]
    foreach ($line in $output) {
        if ($line -match '^worktree\s+(.+)$') {
            $roots.Add($matches[1])
        }
    }

    return $roots.ToArray()
}

function Resolve-SlV8SdkRoot {
    param(
        [string]$RepoRoot,
        [string]$V8Root,
        [string[]]$SearchRoots = @(),
        [switch]$Require
    )

    if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
        $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    Add-SlV8Candidate -Candidates $candidates -CandidatePath $V8Root
    Add-SlV8Candidate -Candidates $candidates -CandidatePath $env:SLOPPY_V8_ROOT
    Add-SlV8PathListCandidates -Candidates $candidates -PathList $env:SLOPPY_V8_SDK_HINTS

    foreach ($searchRoot in $SearchRoots) {
        Add-SlV8Candidate -Candidates $candidates -CandidatePath $searchRoot
    }

    Add-SlV8Candidate -Candidates $candidates -CandidatePath (Join-Path $RepoRoot ".sdeps/v8/$SlV8DefaultPlatform")

    foreach ($worktreeRoot in Get-SlV8GitWorktreeRoots -RepoRoot $RepoRoot) {
        Add-SlV8Candidate -Candidates $candidates -CandidatePath (Join-Path $worktreeRoot ".sdeps/v8/$SlV8DefaultPlatform")
    }

    foreach ($candidate in $candidates) {
        if (Test-SlV8SdkLayout -Root $candidate -Quiet) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($Require) {
        $message = @(
            "No compatible Sloppy V8 SDK was found.",
            "Checked explicit -V8Root, SLOPPY_V8_ROOT, SLOPPY_V8_SDK_HINTS, this worktree's .sdeps, and registered git worktrees.",
            "Set SLOPPY_V8_ROOT to an SDK root, or set SLOPPY_V8_SDK_HINTS to one or more portable search roots separated by '$([System.IO.Path]::PathSeparator)'.",
            "You can validate discovery with: .\tools\windows\resolve-v8-sdk.ps1"
        ) -join [Environment]::NewLine
        throw $message
    }

    return $null
}

function Resolve-SlV8SdkRootForMode {
    param(
        [string]$RepoRoot,
        [string]$V8Root,
        [string[]]$SearchRoots = @(),
        [ValidateSet("OFF", "AUTO", "REQUIRED")]
        [string]$Mode = "AUTO"
    )

    if ($Mode -eq "OFF") {
        return [pscustomobject]@{
            Mode = $Mode
            Status = "optional unavailable"
            Root = $null
            Platform = $SlV8DefaultPlatform
            V8Revision = $SlV8ExpectedRevision
            Detail = "V8 mode is OFF; SDK validation was intentionally disabled."
        }
    }

    try {
        $resolved = Resolve-SlV8SdkRoot `
            -RepoRoot $RepoRoot `
            -V8Root $V8Root `
            -SearchRoots $SearchRoots `
            -Require:($Mode -eq "REQUIRED")
    } catch {
        if ($Mode -eq "REQUIRED") {
            throw
        }
        $resolved = $null
    }

    if ([string]::IsNullOrWhiteSpace($resolved)) {
        return [pscustomobject]@{
            Mode = $Mode
            Status = if ($Mode -eq "REQUIRED") { "missing" } else { "optional unavailable" }
            Root = $null
            Platform = $SlV8DefaultPlatform
            V8Revision = $SlV8ExpectedRevision
            Detail = "No compatible Sloppy V8 SDK was found. AUTO does not enable V8 or count as V8 evidence."
        }
    }

    return [pscustomobject]@{
        Mode = $Mode
        Status = "found"
        Root = $resolved
        Platform = $SlV8DefaultPlatform
        V8Revision = $SlV8ExpectedRevision
        Detail = "Compatible Sloppy V8 SDK resolved."
    }
}
