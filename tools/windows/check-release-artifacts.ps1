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
    foreach ($needle in @("GitHub Release archives are the canonical artifacts", "@slopware/sloppy", "Sloppy apps still run through Sloppy-managed artifacts")) {
        Assert-TextContains -Text $contractText -Needle $needle -Message "Artifact contract doc missing required wording: $needle"
    }

    $releaseInfraPlan = Read-RequiredText -Relative "RELEASE_INFRA_PLAN.md"
    foreach ($needle in @("@slopware/sloppy", "0.1.0-alpha.0", "npm login --auth-type=web", "Trusted Publishing", "types/index.d.ts", "sloppy-v8-sdk-cache")) {
        Assert-TextContains -Text $releaseInfraPlan -Needle $needle -Message "Release infrastructure plan missing required release detail: $needle"
    }

    $contract = Read-JsonFile -Relative "docs/release/artifact-contract.json"
    Assert-True ($contract.schemaVersion -eq 1) "Artifact contract schemaVersion must be 1."
    foreach ($archiveName in @("sloppy-windows-x64.zip", "sloppy-linux-x64.tar.gz", "sloppy-macos-arm64.tar.gz")) {
        $match = @($contract.archiveMatrix | Where-Object { $_.archiveName -eq $archiveName })
        Assert-True ($match.Count -eq 1) "Artifact contract missing archive '$archiveName'."
    }
    Assert-True ($contract.npmPackages.root -eq "@slopware/sloppy") "Artifact contract must name @slopware/sloppy."
    Assert-True ($contract.npmPackages.publishTag -eq "alpha") "Artifact contract npm publishTag must be alpha."
    Assert-True ($contract.npmPackages.publishWorkflow -eq ".github/workflows/npm-publish.yml") "Artifact contract must name the npm publish workflow."

    $npmPublishWorkflow = Read-RequiredText -Relative ".github/workflows/npm-publish.yml"
    foreach ($needle in @("id-token: write", "publish-alpha", "--registry https://registry.npmjs.org/", "npm publish", "Trusted Publishing", "--provenance")) {
        Assert-TextContains -Text $npmPublishWorkflow -Needle $needle -Message "npm publish workflow missing required publishing guard: $needle"
    }
    Assert-TextContains -Text $npmPublishWorkflow -Needle "slopware-sloppy-[0-9]*.tgz" -Message "npm publish workflow must consume @slopware root tarballs."
    Assert-TextContains -Text $npmPublishWorkflow -Needle "slopware-sloppy-linux-x64-*.tgz" -Message "npm publish workflow must consume @slopware platform tarballs."
    Assert-TextContains -Text $npmPublishWorkflow -Needle "for template in minimal-api api package-api; do" -Message "npm publish workflow must smoke current API templates."
    Assert-TextContains -Text $npmPublishWorkflow -Needle 'if [[ "$template" == "package-api" ]]; then' -Message "npm publish workflow must install package-api local dependencies before build smoke."
    Assert-TextContains -Text $npmPublishWorkflow -Needle 'npm install --ignore-scripts --no-audit "${{ steps.tarballs.outputs.root }}" "${{ steps.tarballs.outputs.linux }}"' -Message "npm publish workflow must install package-api from local release tarballs during first-alpha smoke."
    Assert-True (-not $npmPublishWorkflow.Contains("for template in minimal-api full-api dogfood; do")) "npm publish workflow must not smoke removed template names."
    Assert-True (-not $npmPublishWorkflow.Contains("registry-url:")) "npm publish workflow must not configure setup-node token auth."
    Assert-True (-not $npmPublishWorkflow.Contains("NODE_AUTH_TOKEN")) "npm publish workflow must not use token auth."
    Assert-TextContains -Text $contractText -Needle "npm login --auth-type=web" -Message "Artifact contract must document manual browser-auth publish path."
    Assert-TextContains -Text $contractText -Needle "0.1.0-alpha.0" -Message "Artifact contract must name the first alpha version."
    Assert-True ($contract.npmPackages.manualPublishAuth -eq "npm login --auth-type=web") "Artifact contract JSON must record manual browser-auth publish path."
    Assert-True ($contract.npmPackages.types -eq "types/index.d.ts") "Artifact contract JSON must record root TypeScript declarations."

    foreach ($template in @("api", "cli", "minimal-api", "package-api")) {
        $templatePackage = Read-JsonFile -Relative "templates/$template/package.json"
        $devDependency = $templatePackage.devDependencies.PSObject.Properties["@slopware/sloppy"]
        Assert-True ($null -ne $devDependency) "Template $template must include @slopware/sloppy as a dev dependency for editor declarations."
        Assert-True ($devDependency.Value -eq "0.1.0-alpha.0") "Template $template must pin @slopware/sloppy to first alpha declarations."
        $templateConfig = Read-RequiredText -Relative "templates/$template/tsconfig.json"
        Assert-TextContains -Text $templateConfig -Needle '"@slopware/sloppy"' -Message "Template $template tsconfig must load Sloppy declaration package."
    }

    $dependencyAudit = Read-JsonFile -Relative "docs/release/runtime-dependency-audit.json"
    Assert-True ($dependencyAudit.legalReviewStatus -eq "incomplete") "Runtime dependency audit must keep final legal review incomplete."
    foreach ($dependency in @("v8", "openssl-tls", "sqlite", "libpq", "sql-server-odbc", "vcpkg-native-runtime")) {
        $match = @($dependencyAudit.dependencies | Where-Object { $_.id -eq $dependency })
        Assert-True ($match.Count -eq 1) "Runtime dependency audit missing '$dependency'."
    }

    $installMatrix = Read-JsonFile -Relative "docs/release/install-verification-matrix.json"
    foreach ($lane in @("windows-archive", "linux-archive", "npm-root-launcher", "npm-platform-packages", "v8-package-lane")) {
        $match = @($installMatrix.lanes | Where-Object { $_.id -eq $lane })
        Assert-True ($match.Count -eq 1) "Install verification matrix missing lane '$lane'."
    }

    $knownLimitations = Read-RequiredText -Relative "docs/release/KNOWN_LIMITATIONS.md"
    foreach ($heading in @("# Known Limitations", "## Platform Status", "## V8 SDK and Runtime", "## Package and Release Limits", "## Deferred Release Work")) {
        Assert-TextContains -Text $knownLimitations -Needle $heading -Message "Known limitations template missing heading: $heading"
    }

    $releaseNotes = Read-RequiredText -Relative "RELEASE_NOTES.md"
    foreach ($heading in @("# Release Notes", "## Release Type", "## Evidence Summary", "## Included Artifact Areas", "## Deferred", "## Known Limitations", "## Boundary Confirmation")) {
        Assert-TextContains -Text $releaseNotes -Needle $heading -Message "Release notes missing heading: $heading"
    }

    $changelog = Read-RequiredText -Relative "CHANGELOG.md"
    foreach ($heading in @("# Changelog", "## Policy", "## Unreleased")) {
        Assert-TextContains -Text $changelog -Needle $heading -Message "Changelog policy missing heading: $heading"
    }

    $licenses = Read-RequiredText -Relative "docs/release/LICENSES.md"
    Assert-TextContains -Text $licenses -Needle "# License and Notice Policy" -Message "License policy template missing title."
    Assert-TextContains -Text $licenses -Needle "complete third-party license review" -Message "License policy must require third-party review before publishing."

    $notice = Read-RequiredText -Relative "docs/release/NOTICE.md"
    Assert-TextContains -Text $notice -Needle "# Notice Policy" -Message "Notice policy missing title."
    Assert-TextContains -Text $notice -Needle "No secrets" -Message "Notice template must name no-secrets policy."
}

function Test-ReleaseTextHygiene {
    foreach ($relative in @(
        "LICENSE",
        "packages/npm/sloppy/LICENSE",
        "RELEASE_INFRA_PLAN.md",
        "docs/release/index.md",
        "docs/release/KNOWN_LIMITATIONS.md",
        "docs/release/LICENSES.md",
        "docs/release/NOTICE.md",
        "RELEASE_NOTES.md"
    )) {
        $path = Join-Path $Root $relative
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required release policy text is missing: $relative"
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $path) {
            $lineNumber += 1
            Assert-True (-not ($line -match '(?i)\b(TODO|placeholder|skeleton)\b')) "Release policy text contains construction wording at ${relative}:${lineNumber}: $($line.Trim())"
        }
    }
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
    Assert-TextContains -Text $workflow -Needle "contents: read" -Message "Release artifact workflow must keep repository contents read-only for dry-run."
    foreach ($needle in @(
        "tools\windows\fetch-v8.ps1 -Platform windows-x64",
        "gh release download sloppy-v8-sdk-cache",
        "sloppy-v8-sdk-linux-x64-*.tar.gz",
        "sloppy-v8-sdk-darwin-arm64-*.tar.gz",
        "sloppy-v8-sdk-darwin-x64-*.tar.gz"
    )) {
        Assert-TextContains -Text $workflow -Needle $needle -Message "Release artifact workflow must restore existing V8 SDK artifact: $needle"
    }
    Assert-True (-not ($workflow -match "(?m)^\s*run:\s*.*build-v8")) "Release artifact workflow must not rebuild V8 in a run step."
    $permissionsBlock = [regex]::Match($workflow, "(?m)^permissions:\r?\n(?<block>(?:^[ ]+.+\r?\n?)*)").Groups["block"].Value
    Assert-True (-not [string]::IsNullOrWhiteSpace($permissionsBlock)) "Release artifact workflow must declare top-level permissions."
    foreach ($line in ($permissionsBlock -split "\r?\n")) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        $trimmedPermission = $line.Trim()
        $allowedPermission = $trimmedPermission -eq "contents: read" -or $trimmedPermission -eq "packages: write"
        Assert-True $allowedPermission "Release artifact workflow permissions must be limited to contents: read and packages: write. Found: $trimmedPermission"
        Assert-True (-not ($trimmedPermission -match "^\*:|:\s*\*")) "Release artifact workflow must not grant wildcard permissions. Found: $trimmedPermission"
    }

    $allowedSecretReferences = @(
        '${{ secrets.VCPKG_PAT }}'
    )
    $workflowWithoutAllowedSecrets = $workflow
    foreach ($allowedSecretReference in $allowedSecretReferences) {
        $workflowWithoutAllowedSecrets = $workflowWithoutAllowedSecrets.Replace($allowedSecretReference, "")
    }

    $forbidden = @(
        "gh release create",
        "gh release upload",
        "softprops/action-gh-release",
        "actions/create-release",
        "secrets."
    )
    foreach ($needle in $forbidden) {
        Assert-True (-not $workflowWithoutAllowedSecrets.Contains($needle)) "Release artifact dry-run workflow must not use '$needle'."
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
    Assert-True (Test-Path -LiteralPath $packagesRoot -PathType Container) "npm package root is missing: packages/npm"
    $expectedPackages = @(
        "sloppy",
        "sloppy-win32-x64",
        "sloppy-linux-x64",
        "sloppy-darwin-arm64",
        "sloppy-darwin-x64"
    )
    foreach ($package in $expectedPackages) {
        $packageJsonPath = Join-Path $packagesRoot "$package/package.json"
        Assert-True (Test-Path -LiteralPath $packageJsonPath -PathType Leaf) "npm package missing package.json for $package."
        $packageJson = Get-Content -LiteralPath $packageJsonPath -Raw | ConvertFrom-Json
        Assert-True ($packageJson.publishConfig.tag -eq "alpha") "npm package $package must use alpha publishConfig tag."
        if ($package -ne "sloppy") {
            $files = @($packageJson.files)
            Assert-True ($files -contains "templates/") "npm platform package $package must include templates/ for sloppy create."
        }
        if ($null -ne $packageJson.scripts) {
            foreach ($property in $packageJson.scripts.PSObject.Properties) {
                Assert-True (-not ($property.Name -match '^(preinstall|install|postinstall|prepare)$')) "npm package $package must not define install lifecycle script '$($property.Name)'."
                Assert-True (-not ([string]$property.Value -match 'node-gyp|cmake|cargo|vcpkg|fetch-v8|build-v8|postinstall')) "npm package $package script '$($property.Name)' contains native build/download command."
            }
        }
    }

    $rootPackage = Get-Content -LiteralPath (Join-Path $packagesRoot "sloppy/package.json") -Raw | ConvertFrom-Json
    Assert-True (Test-Path -LiteralPath (Join-Path $packagesRoot "sloppy/LICENSE") -PathType Leaf) "Root npm package must include a LICENSE file matching its license metadata."
    Assert-True ($rootPackage.version -eq "0.1.0-alpha.0") "First @slopware alpha package version must be 0.1.0-alpha.0."
    Assert-True ($rootPackage.types -eq "types/index.d.ts") "Root npm package must expose TypeScript declarations."
    Assert-True (Test-Path -LiteralPath (Join-Path $packagesRoot "sloppy/types/index.d.ts") -PathType Leaf) "Root npm package must include TypeScript declaration entrypoint."
    Assert-True (@($rootPackage.files) -contains "types/") "Root npm package files list must include types/."
    foreach ($dependency in @("@slopware/sloppy-win32-x64", "@slopware/sloppy-linux-x64", "@slopware/sloppy-darwin-arm64", "@slopware/sloppy-darwin-x64")) {
        $property = $rootPackage.optionalDependencies.PSObject.Properties[$dependency]
        Assert-True ($null -ne $property) "Root npm package missing optional dependency '$dependency'."
    }
    Assert-True (-not [string]::IsNullOrWhiteSpace($rootPackage.version)) "Root npm package version must not be empty."
    foreach ($package in @("sloppy-win32-x64", "sloppy-linux-x64", "sloppy-darwin-arm64", "sloppy-darwin-x64")) {
        $platformPackage = Get-Content -LiteralPath (Join-Path $packagesRoot "$package/package.json") -Raw | ConvertFrom-Json
        Assert-True ($platformPackage.version -eq $rootPackage.version) "npm package $package version '$($platformPackage.version)' must match root version '$($rootPackage.version)'."
        $dependencyName = [string]$platformPackage.name
        $dependency = $rootPackage.optionalDependencies.PSObject.Properties[$dependencyName]
        Assert-True ($null -ne $dependency) "Root npm package missing optional dependency '$dependencyName'."
        Assert-True ($dependency.Value -eq $rootPackage.version) "Root optional dependency '$dependencyName' must pin version '$($rootPackage.version)', found '$($dependency.Value)'."
    }

    $launcher = Read-RequiredText -Relative "packages/npm/sloppy/bin/sloppy.js"
    Assert-TextContains -Text $launcher -Needle "resolvePlatformPackage" -Message "npm launcher must resolve platform package."
    Assert-TextContains -Text $launcher -Needle "is not installed" -Message "npm launcher must fail clearly when platform package is missing."
    Assert-TextContains -Text $launcher -Needle "SLOPPY_SLOPPYC" -Message "npm launcher must expose packaged sloppyc to sloppy build/package."
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
Test-ReleaseTextHygiene
Test-NpmPackagePolicy
Test-ReleaseWorkflow
Test-PackageChecksums -Directory $PackageDirectory

Write-Host "release artifact templates and checksum checks passed."
exit 0
