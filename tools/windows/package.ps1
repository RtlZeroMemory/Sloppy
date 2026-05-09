param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Release",

    [string]$OutputDir = "artifacts/packages",

    [switch]$IncludeExamples,
    [switch]$IncludeV8Runtime,
    [string]$V8Root = "",
    [switch]$SkipBuild,
    [switch]$Smoke,
    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")
. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$PackageOutputRoot = if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    [System.IO.Path]::GetFullPath($OutputDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $Root $OutputDir))
}

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $Root
    )

    Push-Location $WorkingDirectory
    try {
        & $File @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$File failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Get-PresetName {
    switch ($Configuration) {
        "Debug" { return "windows-dev" }
        "Release" { return "windows-release" }
        "RelWithDebInfo" { return "windows-relwithdebinfo" }
    }
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required package input is missing: $Source"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Required package directory is missing: $Source"
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force |
        Copy-Item -Destination $Destination -Recurse -Force
}

function Get-GitValue {
    param([string[]]$Arguments)

    $value = & git @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    return ($value | Select-Object -First 1).Trim()
}

function Get-PackageVersion {
    $packageJsonPath = Join-Path $Root "packages/npm/sloppy/package.json"
    if (-not (Test-Path -LiteralPath $packageJsonPath -PathType Leaf)) {
        throw "Package version source is missing: $packageJsonPath"
    }

    $packageJson = Get-Content -LiteralPath $packageJsonPath -Raw | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($packageJson.version)) {
        throw "Root npm package version must not be empty."
    }

    return [string]$packageJson.version
}

function Write-PackageReadme {
    param([string]$Path)

    $content = @"
# Sloppy Local Package

This is an experimental development artifact for local validation.

The package contains the currently built `sloppy` runtime CLI, the `sloppyc` compiler CLI,
the source-controlled bootstrap stdlib assets, and example sources. It does not install
anything, mutate PATH, fetch dependencies, include a package manager, or change production
readiness status.

Use `bin/sloppy --version`, `bin/sloppy --help`, `bin/sloppyc --version`, or
`bin/sloppyc --help` for basic outside-checkout smoke checks.

V8 SDK headers and import libraries are intentionally excluded. V8 runtime support is
included only when the package was created with `-IncludeV8Runtime`; it may be linked into
the packaged runtime or bundled as dynamic runtime files depending on the SDK layout.
"@

    Set-Content -LiteralPath $Path -Value $content -Encoding ASCII
}

function Write-KnownLimitations {
    param([string]$Path)

    $content = @"
# Known Limitations

This package is an experimental pre-alpha development artifact.

- Publishing uses a separate release step.
- Production readiness is tracked separately.
- Node, Bun, Deno, npm, and package-manager compatibility are separate tracks.
- V8 execution, live provider readiness, TLS hardening, and release readiness use their own
  lanes.
- V8 SDK headers, import libraries, and source/build trees are intentionally excluded.
- PostgreSQL and SQL Server live-provider behavior requires separate opt-in evidence.
- Signing, notarization, installers, auto-update, and package-manager distribution are not
  included.
"@

    Set-Content -LiteralPath $Path -Value $content -Encoding ASCII
}

function Write-LicensePolicy {
    param([string]$Path)

    $content = @"
# Licenses

This experimental package includes Sloppy source-license text in the repository root
`LICENSE.md` file, copied into this package as `LICENSE`, and may include runtime
dependency DLLs restored by vcpkg for local package smoke.

Complete third-party license review remains required before publishing.
"@

    Set-Content -LiteralPath $Path -Value $content -Encoding ASCII
}

function Copy-NativeRuntimeDependencies {
    param(
        [string]$BuildDirectory,
        [string]$Destination,
        [string]$PackageConfiguration
    )

    $runtimeRelativePath = if ($PackageConfiguration -eq "Debug") {
        "vcpkg_installed/x64-windows/debug/bin"
    } else {
        "vcpkg_installed/x64-windows/bin"
    }
    $runtimeBin = Join-Path $BuildDirectory $runtimeRelativePath
    if (-not (Test-Path -LiteralPath $runtimeBin -PathType Container)) {
        throw "Required vcpkg runtime dependency directory is missing: $runtimeBin. Run the matching build first, or remove -SkipBuild so packaging can populate vcpkg_installed."
    }

    $runtimeFiles = @(Get-ChildItem -LiteralPath $runtimeBin -Filter "*.dll" -File)
    if ($runtimeFiles.Count -eq 0) {
        throw "No runtime dependency DLLs were found under $runtimeBin. Run the matching build first, or remove -SkipBuild so packaging can populate vcpkg_installed."
    }

    foreach ($file in $runtimeFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Destination $file.Name) -Force
    }
}

function Write-Notice {
    param(
        [string]$Path,
        [string[]]$RuntimeFiles
    )

    $content = @"
# Notice

This experimental local package may include runtime DLLs restored by vcpkg for Sloppy's
current native dependencies, such as yyjson, llhttp, libuv, sqlite3, libpq, OpenSSL, zlib,
and lz4.

The package does not include V8 SDK headers/import libraries, database drivers, package
manager metadata, installers, or signed release metadata. Dependency license review and a
complete release notice file are still required before publishing.

Included runtime files:

"@

    foreach ($file in $RuntimeFiles) {
        $content += "- $file`n"
    }

    Set-Content -LiteralPath $Path -Value $content -Encoding ASCII
}

$Preset = Get-PresetName
$BuildDir = Join-Path (Join-Path $Root "build") $Preset
$CargoProfile = if ($Configuration -eq "Release") { "release" } else { "debug" }
$PackageVersion = Get-PackageVersion
$Platform = "windows"
$Arch = "x64"
$PlatformTriplet = "windows-x64"
$Commit = Get-GitValue @("rev-parse", "--short", "HEAD")
if ([string]::IsNullOrWhiteSpace($Commit)) {
    $Commit = "unknown"
}
$PowerShellExe = (Get-Process -Id $PID).Path
if ([string]::IsNullOrWhiteSpace($PowerShellExe)) {
    $PowerShellExe = if ($PSVersionTable.PSEdition -eq "Core") { "pwsh" } else { "powershell" }
}

if (-not $SkipBuild) {
    Import-SlVisualStudioEnvironment

    $devScript = Join-Path $PSScriptRoot "dev.ps1"
    $configureArgs = @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $devScript,
        "configure",
        "-Preset",
        $Preset
    )
    if ($IncludeV8Runtime) {
        $configureArgs += "-EnableV8"
        if (-not [string]::IsNullOrWhiteSpace($V8Root)) {
            $configureArgs += @("-V8Root", $V8Root)
        }
    }
    Invoke-Native $PowerShellExe $configureArgs
    Invoke-Native "cmake" @("--build", "--preset", $Preset, "--target", "sloppy")

    $cargoArgs = @("build", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml"))
    if ($Configuration -eq "Release") {
        $cargoArgs += "--release"
    }
    Invoke-Native "cargo" $cargoArgs
}

$SloppyExe = Join-Path $BuildDir "sloppy.exe"
$SloppycExe = Join-Path (Join-Path $Root "compiler/target/$CargoProfile") "sloppyc.exe"
$PackageName = "sloppy-$Platform-$Arch"
$StageRoot = Join-Path $PackageOutputRoot "stage"
$PackageRoot = Join-Path $StageRoot $PackageName

Remove-Item -LiteralPath $PackageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "bin") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "stdlib") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "templates") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "examples") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "docs") | Out-Null

Copy-RequiredFile -Source $SloppyExe -Destination (Join-Path $PackageRoot "bin/sloppy.exe")
Copy-RequiredFile -Source $SloppycExe -Destination (Join-Path $PackageRoot "bin/sloppyc.exe")
Copy-NativeRuntimeDependencies -BuildDirectory $BuildDir -Destination (Join-Path $PackageRoot "bin") -PackageConfiguration $Configuration
Copy-DirectoryContents -Source (Join-Path $Root "stdlib/sloppy") -Destination (Join-Path $PackageRoot "stdlib/sloppy")
Copy-DirectoryContents -Source (Join-Path $Root "templates") -Destination (Join-Path $PackageRoot "templates")
Copy-DirectoryContents -Source (Join-Path $Root "examples") -Destination (Join-Path $PackageRoot "examples")
Copy-RequiredFile -Source (Join-Path $Root "LICENSE.md") -Destination (Join-Path $PackageRoot "LICENSE")
Write-PackageReadme -Path (Join-Path $PackageRoot "README.md")
Write-KnownLimitations -Path (Join-Path $PackageRoot "docs/KNOWN_LIMITATIONS.md")
Write-LicensePolicy -Path (Join-Path $PackageRoot "docs/LICENSES.md")

$containsExamples = $true

$containsV8Runtime = $false
$v8RuntimeStatus = "not bundled"
$v8RuntimeDirectory = ""
$v8RuntimeNotes = "no V8 runtime"
if ($IncludeV8Runtime) {
    $resolvedV8Root = Resolve-SlV8SdkRoot -RepoRoot $Root -V8Root $V8Root -Require

    $v8Bin = Join-Path $resolvedV8Root "bin"
    $runtimeFiles = @()
    if (Test-Path -LiteralPath $v8Bin -PathType Container) {
        $runtimeFiles = @(
            Get-ChildItem -LiteralPath $v8Bin -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in @(".dll", ".so", ".dylib") }
        )
    }

    if ($runtimeFiles.Count -gt 0) {
        $v8Destination = Join-Path $PackageRoot "engines/v8"
        New-Item -ItemType Directory -Force -Path $v8Destination | Out-Null
        foreach ($file in $runtimeFiles) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $v8Destination $file.Name) -Force
        }
        $v8RuntimeStatus = "bundled shared runtime"
        $v8RuntimeDirectory = "engines/v8"
        $v8RuntimeNotes = "V8 runtime DLLs were bundled from the Sloppy-owned SDK"
    } else {
        $v8RuntimeStatus = "linked runtime"
        $v8RuntimeNotes = "V8 is linked into the packaged Windows runtime from the Sloppy-owned SDK"
    }
    $containsV8Runtime = $true
}

$nativeRuntimeFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $PackageRoot "bin") -Filter "*.dll" -File |
        ForEach-Object { "bin/$($_.Name)" }
)
if ($containsV8Runtime) {
    $v8RuntimeRoot = Join-Path $PackageRoot "engines/v8"
    if (Test-Path -LiteralPath $v8RuntimeRoot -PathType Container) {
        $nativeRuntimeFiles += @(
            Get-ChildItem -LiteralPath $v8RuntimeRoot -File |
                ForEach-Object { "engines/v8/$($_.Name)" }
        )
    } else {
        $nativeRuntimeFiles += "bin/sloppy.exe (V8 linked runtime)"
    }
}
Write-Notice -Path (Join-Path $PackageRoot "docs/NOTICE.md") -RuntimeFiles $nativeRuntimeFiles

$manifest = [ordered]@{
    manifestSchema = "sloppy.release-artifact.v1"
    manifestVersion = 1
    name = "sloppy"
    version = $PackageVersion
    archiveName = "$PackageName.zip"
    packageRoot = $PackageName
    platform = $Platform
    arch = $Arch
    platformTriplet = $PlatformTriplet
    configuration = $Configuration
    commit = $Commit
    releaseKind = "dry-run"
    publicReleaseCreated = $false
    canonicalDistribution = "github-release-archive"
    npmPackageSource = "platform packages must be generated from this tested archive content"
    platformStatus = "experimental"
    runtimeUserStatus = "experimental pending outside-checkout package smoke evidence"
    compiler = [ordered]@{
        name = "sloppyc"
        profile = $CargoProfile
        included = $true
    }
    v8 = [ordered]@{
        sdkIncluded = $false
        runtimeIncluded = $containsV8Runtime
        status = $v8RuntimeStatus
        version = "pinned by tools/deps/sloppy-deps.json"
        runtimeDirectory = $v8RuntimeDirectory
        notes = $v8RuntimeNotes
    }
    enabledFeatures = @(
        "native-runtime",
        "stdlib",
        "compiler"
    )
    dependencyStatuses = [ordered]@{
        nativeRuntimeDependencies = "bundled"
        v8Sdk = "excluded"
        v8Runtime = $v8RuntimeStatus
        liveProviders = "not configured"
        runtimeDependencyAudit = "docs/release/runtime-dependency-audit.json"
    }
    providers = [ordered]@{
        sqlite = "packaged runtime dependency status only; provider behavior evidence is separate"
        postgresql = "live-provider evidence is separate"
        sqlserver = "driver/runtime availability evidence is separate"
    }
    containsV8Runtime = $containsV8Runtime
    containsV8Sdk = $false
    containsStdlib = $true
    containsTemplates = $true
    containsExamples = $containsExamples
    containsNativeRuntimeDependencies = $true
    knownLimitations = "docs/KNOWN_LIMITATIONS.md"
    checksums = [ordered]@{
        file = "SHA256SUMS.txt"
        algorithm = "SHA-256"
    }
    tools = @("sloppy", "sloppyc")
    layoutVersion = 1
    notes = @(
        "experimental",
        "dry-run artifact",
        "production readiness tracked separately",
        "dry-run only",
        "no installer",
        "no package manager",
        "npm launcher packages may reuse this archive but do not add npm app dependency support",
        "no auto update",
        "no signed or notarized artifacts"
    )
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PackageRoot "manifest.json") -Encoding ASCII

New-Item -ItemType Directory -Force -Path $PackageOutputRoot | Out-Null
$ArchivePath = Join-Path $PackageOutputRoot "$PackageName.zip"
$ChecksumPath = Join-Path $PackageOutputRoot "SHA256SUMS.txt"
Remove-Item -LiteralPath $ArchivePath -Force -ErrorAction SilentlyContinue

Compress-Archive -LiteralPath $PackageRoot -DestinationPath $ArchivePath -Force
$checksum = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$checksum  $(Split-Path -Leaf $ArchivePath)" | Set-Content -LiteralPath $ChecksumPath -Encoding ASCII

Write-Host "Created package: $ArchivePath"
Write-Host "SHA256: $checksum"
Write-Host "Checksum file: $ChecksumPath"

if ($Smoke) {
    $smokeScript = Join-Path $PSScriptRoot "test-package.ps1"
    $smokeArgs = @(
        "-PackagePath",
        $ArchivePath,
        "-MetadataPath",
        (Join-Path $Root "tests/fixtures/package/windows-default/case.json")
    )
    if ($IncludeV8Runtime) {
        $smokeArgs += "-RequireV8Runtime"
    }
    if ($KeepTemp) {
        $smokeArgs += "-KeepTemp"
    }
    $nativeArgs = @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $smokeScript
    )
    $nativeArgs += $smokeArgs
    Invoke-Native "powershell" $nativeArgs
}
