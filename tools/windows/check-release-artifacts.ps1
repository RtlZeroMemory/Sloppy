param(
    [string]$Root = "",
    [string]$PackageDirectory = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
} else {
    $Root = (Resolve-Path -LiteralPath $Root).Path
}

if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Join-Path $Root "artifacts/packages"
} elseif (-not [System.IO.Path]::IsPathRooted($PackageDirectory)) {
    $PackageDirectory = Join-Path $Root $PackageDirectory
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Read-RequiredText {
    param([string]$Relative)

    $path = Join-Path $Root $Relative
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required release file is missing: $Relative"
    return Get-Content -LiteralPath $path -Raw
}

function Assert-TextContains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Message
    )

    Assert-True ($Text.Contains($Needle)) $Message
}

function Get-Sha256Hex {
    param([string]$Path)

    $getFileHash = Get-Command Get-FileHash -ErrorAction SilentlyContinue
    if ($null -ne $getFileHash) {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hash = $sha256.ComputeHash($stream)
            return ([System.BitConverter]::ToString($hash) -replace "-", "").ToLowerInvariant()
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Test-ReleaseTemplates {
    $contractText = Read-RequiredText -Relative "docs/release/artifact-contract.md"
    foreach ($needle in @("GitHub Release archives are the canonical artifacts", "@sloppy/runtime", "do not add npm package imports")) {
        Assert-TextContains -Text $contractText -Needle $needle -Message "Artifact contract doc missing required wording: $needle"
    }

    $contract = Read-JsonFile -Relative "docs/release/artifact-contract.json"
    Assert-True ($contract.schemaVersion -eq 1) "Artifact contract schemaVersion must be 1."
    foreach ($archiveName in @("sloppy-windows-x64.zip", "sloppy-linux-x64.tar.gz", "sloppy-macos-arm64.tar.gz")) {
        $match = @($contract.archiveMatrix | Where-Object { $_.archiveName -eq $archiveName })
        Assert-True ($match.Count -eq 1) "Artifact contract missing archive '$archiveName'."
    }
    Assert-True ($contract.npmPackages.root -eq "@sloppy/runtime") "Artifact contract must name @sloppy/runtime."
    Assert-True ($contract.npmPackages.publishTag -eq "alpha") "Artifact contract npm publishTag must be alpha."

    $dependencyAudit = Read-JsonFile -Relative "docs/release/runtime-dependency-audit.json"
    Assert-True ($dependencyAudit.legalReviewStatus -eq "incomplete") "Runtime dependency audit must not claim final legal review."
    foreach ($dependency in @("v8", "openssl-tls", "sqlite", "libpq", "sql-server-odbc", "vcpkg-native-runtime")) {
        $match = @($dependencyAudit.dependencies | Where-Object { $_.id -eq $dependency })
        Assert-True ($match.Count -eq 1) "Runtime dependency audit missing '$dependency'."
    }

    $installMatrix = Read-JsonFile -Relative "docs/release/install-verification-matrix.json"
    foreach ($lane in @("windows-archive", "linux-archive", "npm-root-launcher", "npm-platform-packages", "v8-package-lane")) {
        $match = @($installMatrix.lanes | Where-Object { $_.id -eq $lane })
        Assert-True ($match.Count -eq 1) "Install verification matrix missing lane '$lane'."
    }

    $handoff = Read-RequiredText -Relative "docs/release/alpha-gate-handoff.md"
    foreach ($needle in @("# Alpha Gate Handoff", "#918", "#300", "#684", "must not be used as proof that alpha shipped", "exact blocker is recorded")) {
        Assert-TextContains -Text $handoff -Needle $needle -Message "Alpha gate handoff missing required wording: $needle"
    }

    $verifier = Read-RequiredText -Relative "docs/release/post-merge-verifier.md"
    foreach ($needle in @("/goal Verify Sloppy alpha packaging", "Contributor trial:", "Normal user archive trial:", "Normal user npm trial:", "do not publish npm")) {
        Assert-TextContains -Text $verifier -Needle $needle -Message "Post-merge verifier handoff missing required prompt text: $needle"
    }

    $knownLimitations = Read-RequiredText -Relative "docs/release/KNOWN_LIMITATIONS.md"
    foreach ($heading in @("# Known Limitations", "## Platform Status", "## V8 SDK and Runtime", "## Package and Release Limits", "## Deferred Release Work")) {
        Assert-TextContains -Text $knownLimitations -Needle $heading -Message "Known limitations template missing heading: $heading"
    }

    $releaseNotes = Read-RequiredText -Relative "RELEASE_NOTES.md"
    foreach ($heading in @("# Release Notes Skeleton", "## Release Type", "## Evidence Summary", "## Shipped", "## Deferred", "## Known Limitations", "## No-Claims Confirmation")) {
        Assert-TextContains -Text $releaseNotes -Needle $heading -Message "Release notes skeleton missing heading: $heading"
    }

    $changelog = Read-RequiredText -Relative "CHANGELOG.md"
    foreach ($heading in @("# Changelog", "## Policy", "## Unreleased")) {
        Assert-TextContains -Text $changelog -Needle $heading -Message "Changelog policy missing heading: $heading"
    }

    $licenses = Read-RequiredText -Relative "docs/release/LICENSES.md"
    Assert-TextContains -Text $licenses -Needle "# License and Notice Policy" -Message "License policy template missing title."
    Assert-TextContains -Text $licenses -Needle "complete third-party license review" -Message "License policy must require third-party review before public release."

    $notice = Read-RequiredText -Relative "docs/release/NOTICE.md"
    Assert-TextContains -Text $notice -Needle "# Notice Skeleton" -Message "Notice template missing title."
    Assert-TextContains -Text $notice -Needle "No secrets" -Message "Notice template must name no-secrets policy."
}

function Read-JsonFile {
    param([string]$Relative)

    $path = Join-Path $Root $Relative
    Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required release JSON file is missing: $Relative"
    try {
        return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in ${Relative}: $($_.Exception.Message)"
    }
}

function Test-ReleaseWorkflow {
    $workflowRelative = ".github/workflows/release-artifacts.yml"
    $workflowPath = Join-Path $Root $workflowRelative
    Assert-True (Test-Path -LiteralPath $workflowPath -PathType Leaf) "Release artifact workflow is missing: $workflowRelative"

    $workflow = Get-Content -LiteralPath $workflowPath -Raw
    Assert-TextContains -Text $workflow -Needle "workflow_dispatch:" -Message "Release artifact workflow must be manual workflow_dispatch."
    Assert-TextContains -Text $workflow -Needle "dry_run:" -Message "Release artifact workflow must expose dry_run input."
    Assert-TextContains -Text $workflow -Needle "only supports dry_run=true" -Message "Release artifact workflow must fail closed when dry_run is false."
    $onBlock = [regex]::Match($workflow, "(?m)^on:\r?\n(?<block>(?:^[ ]+.+\r?\n?)*)").Groups["block"].Value
    Assert-True (-not [string]::IsNullOrWhiteSpace($onBlock)) "Release artifact workflow must declare workflow_dispatch under top-level on."
    foreach ($line in ($onBlock -split "\r?\n")) {
        if ($line -match "^\s{2}(push|pull_request|schedule|repository_dispatch|workflow_run|release):") {
            throw "Release artifact workflow must be triggered only by workflow_dispatch. Found: $($line.Trim())"
        }
    }
    Assert-TextContains -Text $workflow -Needle "actions/upload-artifact@v4" -Message "Release artifact workflow must upload package/checksum artifacts."
    Assert-TextContains -Text $workflow -Needle "SHA256SUMS.txt" -Message "Release artifact workflow must preserve checksum artifacts."
    Assert-TextContains -Text $workflow -Needle "contents: read" -Message "Release artifact workflow should not need write permissions for dry-run."
    $permissionsBlock = [regex]::Match($workflow, "(?m)^permissions:\r?\n(?<block>(?:^[ ]+.+\r?\n?)*)").Groups["block"].Value
    Assert-True (-not [string]::IsNullOrWhiteSpace($permissionsBlock)) "Release artifact workflow must declare top-level permissions."
    foreach ($line in ($permissionsBlock -split "\r?\n")) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        Assert-True ($line -match "^\s+[A-Za-z0-9_-]+:\s*read\s*$") "Release artifact workflow permissions must be read-only. Found: $($line.Trim())"
        Assert-True (-not ($line -match ":\s*write\b|\*")) "Release artifact workflow must not grant write or wildcard permissions. Found: $($line.Trim())"
    }

    $forbidden = @(
        "gh release create",
        "gh release upload",
        "softprops/action-gh-release",
        "actions/create-release",
        "secrets."
    )
    foreach ($needle in $forbidden) {
        Assert-True (-not $workflow.Contains($needle)) "Release artifact dry-run workflow must not use '$needle'."
    }
}

function Test-PackageChecksums {
    param([string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        Write-Host "release artifact checksum check skipped: package directory is unavailable: $Directory"
        return
    }

    $checksumPath = Join-Path $Directory "SHA256SUMS.txt"
    if (-not (Test-Path -LiteralPath $checksumPath -PathType Leaf)) {
        Write-Host "release artifact checksum check skipped: SHA256SUMS.txt is unavailable under $Directory"
        return
    }

    $archives = @(
        Get-ChildItem -LiteralPath $Directory -File |
            Where-Object { $_.Name -like "sloppy-*.zip" -or $_.Name -like "sloppy-*.tar.gz" }
    )
    if ($archives.Count -eq 0) {
        throw "SHA256SUMS.txt exists but no sloppy package archives were found under $Directory."
    }

    $checksumLines = @(Get-Content -LiteralPath $checksumPath)
    $archiveNames = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::Ordinal)
    foreach ($archive in $archives) {
        $null = $archiveNames.Add($archive.Name)
    }
    foreach ($line in $checksumLines) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith("#")) {
            continue
        }
        if ($trimmed -notmatch "^[0-9a-fA-F]{64}\s+\*?(?<name>.+)$") {
            throw "SHA256SUMS.txt contains a malformed checksum line: $trimmed"
        }
        $name = $matches["name"]
        if (-not $archiveNames.Contains($name)) {
            throw "SHA256SUMS.txt contains a stale entry for missing archive: $name"
        }
    }
    foreach ($archive in $archives) {
        $actualHash = Get-Sha256Hex -Path $archive.FullName
        $expectedName = [regex]::Escape($archive.Name)
        $matchingLine = $checksumLines |
            Where-Object { $_ -match "^$actualHash\s+\*?$expectedName$" } |
            Select-Object -First 1
        if (-not $matchingLine) {
            throw "SHA256SUMS.txt does not contain the current hash for $($archive.Name)."
        }
    }
}

function Test-NpmPackagePolicy {
    $packagesRoot = Join-Path $Root "packages/npm"
    Assert-True (Test-Path -LiteralPath $packagesRoot -PathType Container) "npm package skeleton root is missing: packages/npm"
    $expectedPackages = @(
        "runtime",
        "runtime-win32-x64",
        "runtime-linux-x64-gnu",
        "runtime-darwin-arm64",
        "runtime-darwin-x64"
    )
    foreach ($package in $expectedPackages) {
        $packageJsonPath = Join-Path $packagesRoot "$package/package.json"
        Assert-True (Test-Path -LiteralPath $packageJsonPath -PathType Leaf) "npm package skeleton missing package.json for $package."
        $packageJson = Get-Content -LiteralPath $packageJsonPath -Raw | ConvertFrom-Json
        Assert-True ($packageJson.publishConfig.tag -eq "alpha") "npm package $package must use alpha publishConfig tag."
        if ($null -ne $packageJson.scripts) {
            foreach ($property in $packageJson.scripts.PSObject.Properties) {
                Assert-True (-not ($property.Name -match '^(preinstall|install|postinstall|prepare)$')) "npm package $package must not define install lifecycle script '$($property.Name)'."
                Assert-True (-not ([string]$property.Value -match 'node-gyp|cmake|cargo|vcpkg|fetch-v8|build-v8|postinstall')) "npm package $package script '$($property.Name)' contains native build/download command."
            }
        }
    }

    $rootPackage = Get-Content -LiteralPath (Join-Path $packagesRoot "runtime/package.json") -Raw | ConvertFrom-Json
    Assert-True (Test-Path -LiteralPath (Join-Path $packagesRoot "runtime/LICENSE") -PathType Leaf) "Root npm runtime package must include a LICENSE file matching its license metadata."
    foreach ($dependency in @("@sloppy/runtime-win32-x64", "@sloppy/runtime-linux-x64-gnu", "@sloppy/runtime-darwin-arm64", "@sloppy/runtime-darwin-x64")) {
        $property = $rootPackage.optionalDependencies.PSObject.Properties[$dependency]
        Assert-True ($null -ne $property) "Root npm runtime package missing optional dependency '$dependency'."
    }

    $launcher = Read-RequiredText -Relative "packages/npm/runtime/bin/sloppy.js"
    Assert-TextContains -Text $launcher -Needle "resolvePlatformPackage" -Message "npm launcher must resolve platform package."
    Assert-TextContains -Text $launcher -Needle "is not installed" -Message "npm launcher must fail clearly when platform package is missing."
}

function Invoke-SelfTest {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-release-check-selftest-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    try {
        $archive = Join-Path $tempRoot "sloppy-windows-x64.zip"
        Set-Content -LiteralPath $archive -Value "fixture" -Encoding ASCII
        $hash = Get-Sha256Hex -Path $archive
        "$hash  sloppy-windows-x64.zip" | Set-Content -LiteralPath (Join-Path $tempRoot "SHA256SUMS.txt") -Encoding ASCII
        Test-PackageChecksums -Directory $tempRoot

        @(
            "$hash  sloppy-windows-x64.zip",
            "$hash  stale-package.zip"
        ) | Set-Content -LiteralPath (Join-Path $tempRoot "SHA256SUMS.txt") -Encoding ASCII
        $staleFailed = $false
        try {
            Test-PackageChecksums -Directory $tempRoot
        } catch {
            $staleFailed = $true
        }
        Assert-True $staleFailed "Release checksum scanner accepted a stale checksum fixture."

        "bad  sloppy-windows-x64.zip" | Set-Content -LiteralPath (Join-Path $tempRoot "SHA256SUMS.txt") -Encoding ASCII
        $failed = $false
        try {
            Test-PackageChecksums -Directory $tempRoot
        } catch {
            $failed = $true
        }
        Assert-True $failed "Release checksum scanner accepted a corrupt checksum fixture."
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "release artifact checker self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

Test-ReleaseTemplates
Test-NpmPackagePolicy
Test-ReleaseWorkflow
Test-PackageChecksums -Directory $PackageDirectory

Write-Host "release artifact templates and checksum checks passed."
exit 0
