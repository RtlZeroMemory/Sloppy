param(
    [string]$PackagePath = "",
    [string]$OutputDir = "artifacts/npm",
    [ValidateSet("alpha")]
    [string]$PublishTag = "alpha",
    [switch]$SkipInstallSmoke,
    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$OutputRoot = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    [System.IO.Path]::GetFullPath($OutputDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $Root $OutputDir))
}

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $Root,
        [int[]]$AllowedExitCodes = @(0)
    )

    Push-Location $WorkingDirectory
    try {
        & $File @Arguments
        if (-not ($AllowedExitCodes -contains $LASTEXITCODE)) {
            throw "$File failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Resolve-PackagePath {
    param([switch]$PreferHostPlatform)

    if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
        return (Resolve-Path -LiteralPath $PackagePath).Path
    }

    $packageDir = Join-Path $Root "artifacts/packages"
    $packages = @()
    if (Test-Path -LiteralPath $packageDir -PathType Container) {
        $packages = @(Get-ChildItem -LiteralPath $packageDir -File |
            Where-Object { $_.Name -in @("sloppy-windows-x64.zip", "sloppy-linux-x64.tar.gz", "sloppy-macos-arm64.tar.gz", "sloppy-macos-x64.tar.gz") -or $_.Name -like "sloppy-*-windows-x64.zip" } |
            Sort-Object LastWriteTimeUtc -Descending)
    }
    if ($packages.Count -eq 0) {
        throw "No release archive found under $packageDir. Run package/release-dry-run first or pass -PackagePath."
    }
    if ($PreferHostPlatform) {
        $hostPreferred = $packages | Where-Object { $_.Name -eq "sloppy-windows-x64.zip" } | Select-Object -First 1
        if ($null -ne $hostPreferred) {
            return $hostPreferred.FullName
        }
    }
    return $packages[0].FullName
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force |
        Copy-Item -Destination $Destination -Recurse -Force
}

function Copy-NpmTemplateGitignorePlaceholders {
    param([string]$TemplatesRoot)

    Get-ChildItem -LiteralPath $TemplatesRoot -Recurse -Force -File -Filter ".gitignore" |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $_.DirectoryName "gitignore") -Force
        }
}

function Get-PlatformPackageName {
    param([object]$Manifest)

    $triplet = [string]$Manifest.platformTriplet
    switch ($triplet) {
        "windows-x64" { return "sloppy-win32-x64" }
        "linux-x64" { return "sloppy-linux-x64" }
        "macos-arm64" { throw "macOS npm packages are not staged by this alpha workflow yet; hosted package proof is future work." }
        "macos-x64" { throw "macOS npm packages are not staged by this alpha workflow yet; hosted package proof is future work." }
        default { throw "Unsupported npm platform package triplet in manifest: $triplet" }
    }
}

function Get-NpmTarballName {
    param([object]$PackageJson)

    $name = [string]$PackageJson.name
    $version = [string]$PackageJson.version
    $fileName = $name
    if ($fileName.StartsWith("@")) {
        $fileName = $fileName.Substring(1)
    }
    $fileName = $fileName -replace "/", "-"
    return "$fileName-$version.tgz"
}

function Assert-NoNativeInstallScripts {
    param([string]$PackageJsonPath)

    $json = Get-Content -LiteralPath $PackageJsonPath -Raw | ConvertFrom-Json
    if ($null -ne $json.scripts) {
        foreach ($property in $json.scripts.PSObject.Properties) {
            if ($property.Name -match '^(preinstall|install|postinstall|prepare)$') {
                throw "$PackageJsonPath contains forbidden native install lifecycle script '$($property.Name)'."
            }
            if ([string]$property.Value -match 'node-gyp|cmake|cargo|vcpkg|fetch-v8|build-v8|postinstall') {
                throw "$PackageJsonPath script '$($property.Name)' contains forbidden native build/download command."
            }
        }
    }
    if ($json.publishConfig.tag -ne "alpha") {
        throw "$PackageJsonPath must publish with alpha dist-tag for dry-run policy."
    }
}

$npm = Get-Command npm -ErrorAction SilentlyContinue
if ($null -eq $npm) {
    throw "npm is required for npm dry-run packaging, but it was not found on PATH."
}
$node = Get-Command node -ErrorAction SilentlyContinue
if ($null -eq $node) {
    throw "node is required for npm dry-run launcher smoke, but it was not found on PATH."
}

$archivePath = Resolve-PackagePath -PreferHostPlatform:(-not $SkipInstallSmoke)
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-npm-dry-run-" + [System.Guid]::NewGuid().ToString("N"))
$extractRoot = Join-Path $tempRoot "extract"
$stageRoot = Join-Path $OutputRoot "stage"
$tarballRoot = Join-Path $OutputRoot "tarballs"

Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $tarballRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $extractRoot, $stageRoot, $tarballRoot | Out-Null

try {
    if ([System.IO.Path]::GetExtension($archivePath) -eq ".zip") {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot -Force
    } else {
        Invoke-Native "tar" @("-xzf", $archivePath, "-C", $extractRoot)
    }

    $roots = @(Get-ChildItem -LiteralPath $extractRoot -Directory)
    if ($roots.Count -ne 1) {
        throw "Expected exactly one package root in archive for npm staging, found $($roots.Count)."
    }
    $packageRoot = $roots[0].FullName
    $manifestPath = Join-Path $packageRoot "manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Archive package is missing manifest.json."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.manifestSchema -ne "sloppy.release-artifact.v1") {
        throw "Archive manifest is not a release artifact manifest."
    }
    if ($manifest.releaseKind -ne "dry-run") {
        throw "Archive manifest must declare releaseKind=dry-run for npm staging."
    }
    if ($manifest.publicReleaseCreated -ne $false) {
        throw "Archive manifest must declare publicReleaseCreated=false for npm staging."
    }
    if ($manifest.canonicalDistribution -ne "github-release-archive") {
        throw "Archive manifest must declare GitHub Release archives as the canonical distribution."
    }
    if ([string]$manifest.packageRoot -ne $roots[0].Name) {
        throw "Archive manifest packageRoot does not match the extracted archive root."
    }

    $runtimeStage = Join-Path $stageRoot "sloppy"
    Copy-DirectoryContents -Source (Join-Path $Root "packages/npm/sloppy") -Destination $runtimeStage

    $platformPackageDirectoryName = Get-PlatformPackageName -Manifest $manifest
    $platformSkeleton = Join-Path $Root "packages/npm/$platformPackageDirectoryName"
    $platformStage = Join-Path $stageRoot $platformPackageDirectoryName
    Copy-DirectoryContents -Source $platformSkeleton -Destination $platformStage
    foreach ($entry in @("bin", "stdlib", "templates", "examples", "docs", "manifest.json", "LICENSE", "README.md")) {
        $source = Join-Path $packageRoot $entry
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Archive package is missing npm platform package content: $entry"
        }
        Copy-Item -LiteralPath $source -Destination $platformStage -Recurse -Force
    }
    Copy-NpmTemplateGitignorePlaceholders -TemplatesRoot (Join-Path $platformStage "templates")

    Assert-NoNativeInstallScripts -PackageJsonPath (Join-Path $runtimeStage "package.json")
    Assert-NoNativeInstallScripts -PackageJsonPath (Join-Path $platformStage "package.json")

    Invoke-Native $npm.Source @("pack", "--dry-run") -WorkingDirectory $runtimeStage
    Invoke-Native $npm.Source @("pack", "--dry-run") -WorkingDirectory $platformStage
    Invoke-Native $npm.Source @("pack", "--pack-destination", $tarballRoot) -WorkingDirectory $platformStage
    Invoke-Native $npm.Source @("pack", "--pack-destination", $tarballRoot) -WorkingDirectory $runtimeStage

    $runtimePackageJson = Get-Content -LiteralPath (Join-Path $runtimeStage "package.json") -Raw | ConvertFrom-Json
    $platformPackageJson = Get-Content -LiteralPath (Join-Path $platformStage "package.json") -Raw | ConvertFrom-Json
    $runtimeTarballName = Get-NpmTarballName -PackageJson $runtimePackageJson
    $platformTarballName = Get-NpmTarballName -PackageJson $platformPackageJson
    $runtimeTarball = @(Get-ChildItem -LiteralPath $tarballRoot -Filter $runtimeTarballName -File | Select-Object -First 1)
    $platformTarball = @(Get-ChildItem -LiteralPath $tarballRoot -Filter $platformTarballName -File | Select-Object -First 1)
    if ($runtimeTarball.Count -eq 0 -or $platformTarball.Count -eq 0) {
        throw "npm dry-run did not produce both root and platform tarballs."
    }

    if (-not $SkipInstallSmoke) {
        $installRoot = Join-Path $tempRoot "install"
        New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
        Invoke-Native $npm.Source @("install", "--prefix", $installRoot, $runtimeTarball[0].FullName, $platformTarball[0].FullName)
        $launcher = Join-Path $installRoot "node_modules/@rtlzeromemory/sloppy/bin/sloppy.js"
        Invoke-Native $node.Source @($launcher, "--version") -WorkingDirectory $installRoot
        Invoke-Native $node.Source @($launcher, "doctor") -WorkingDirectory $installRoot

        $createRoot = Join-Path $tempRoot "created-work"
        New-Item -ItemType Directory -Force -Path $createRoot | Out-Null
        Invoke-Native $node.Source @($launcher, "create", "tmp-npm-app", "--template", "minimal-api") -WorkingDirectory $createRoot
        $createdApp = Join-Path $createRoot "tmp-npm-app"
        if (-not (Test-Path -LiteralPath (Join-Path $createdApp ".gitignore") -PathType Leaf)) {
            throw "npm dry-run create smoke did not create .gitignore."
        }
        if (Test-Path -LiteralPath (Join-Path $createdApp "gitignore")) {
            throw "npm dry-run create smoke leaked template gitignore placeholder."
        }
        Invoke-Native $node.Source @($launcher, "build") -WorkingDirectory $createdApp
        Push-Location $createdApp
        try {
            $runOutput = & $node.Source $launcher "run" "--once" "GET" "/health" 2>&1 | Out-String
            $runExit = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($runExit -eq 0) {
            if ($runOutput -notmatch "ok") {
                throw "npm dry-run create/build/run smoke did not return /health ok.`n$runOutput"
            }
        } elseif ($runOutput -notmatch "requires V8-enabled build") {
            throw "npm dry-run create/build/run smoke failed unexpectedly.`n$runOutput"
        }

        $missingRoot = Join-Path $tempRoot "missing-platform"
        New-Item -ItemType Directory -Force -Path $missingRoot | Out-Null
        Invoke-Native $npm.Source @("install", "--prefix", $missingRoot, "--omit=optional", $runtimeTarball[0].FullName)
        $missingLauncher = Join-Path $missingRoot "node_modules/@rtlzeromemory/sloppy/bin/sloppy.js"
        Invoke-Native $node.Source @($missingLauncher, "--version") -WorkingDirectory $missingRoot -AllowedExitCodes @(1)
        $oldPlatform = $env:SLOPPY_RUNTIME_PLATFORM
        try {
            $env:SLOPPY_RUNTIME_PLATFORM = "unsupported-os"
            Invoke-Native $node.Source @($missingLauncher, "--version") -WorkingDirectory $missingRoot -AllowedExitCodes @(1)
        } finally {
            $env:SLOPPY_RUNTIME_PLATFORM = $oldPlatform
        }
    }

    [ordered]@{
        kind = "sloppy-npm-dry-run"
        packageArchive = $archivePath
        rootPackage = "@rtlzeromemory/sloppy"
        platformPackage = [string]$platformPackageJson.name
        publishTag = $PublishTag
        nativeInstallScripts = $false
        nodeGyp = $false
        postinstallBuildOrDownload = $false
        npmPublished = $false
        tarballDirectory = $tarballRoot
        installSmokeRun = (-not $SkipInstallSmoke)
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputRoot "summary.json") -Encoding ASCII

    Write-Host "npm dry-run completed without publishing packages."
    Write-Host "npm tarballs: $tarballRoot"
    $global:LASTEXITCODE = 0
} finally {
    if ($KeepTemp) {
        Write-Host "Keeping npm dry-run temp directory: $tempRoot"
    } else {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
