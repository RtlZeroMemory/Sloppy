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

function Write-PackageReadme {
    param([string]$Path)

    $content = @"
# Sloppy Local Package

This is an experimental development artifact, not a public release.

The package contains the currently built `sloppy` runtime CLI, the `sloppyc` compiler CLI,
and the source-controlled bootstrap stdlib assets. It does not install anything, mutate
PATH, fetch dependencies, include a package manager, or claim production readiness.

Use `bin/sloppy --version`, `bin/sloppy --help`, `bin/sloppyc --version`, or
`bin/sloppyc --help` for basic outside-checkout smoke checks.

V8 SDK headers and import libraries are intentionally excluded. Dynamic V8 runtime files
are included only when the package was created with `-IncludeV8Runtime` and a compatible
SDK `bin/` directory was available.
"@

    Set-Content -LiteralPath $Path -Value $content -Encoding ASCII
}

function Copy-NativeRuntimeDependencies {
    param(
        [string]$BuildDirectory,
        [string]$Destination
    )

    $runtimeBin = Join-Path $BuildDirectory "vcpkg_installed/x64-windows/bin"
    if (-not (Test-Path -LiteralPath $runtimeBin -PathType Container)) {
        throw "Required runtime dependency directory is missing: $runtimeBin"
    }

    $runtimeFiles = @(Get-ChildItem -LiteralPath $runtimeBin -Filter "*.dll" -File)
    if ($runtimeFiles.Count -eq 0) {
        throw "No runtime dependency DLLs were found under $runtimeBin."
    }

    foreach ($file in $runtimeFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Destination $file.Name) -Force
    }
}

function Write-ThirdPartyNotices {
    param(
        [string]$Path,
        [string[]]$RuntimeFiles
    )

    $content = @"
# Third Party Notices

This experimental local package may include runtime DLLs restored by vcpkg for Sloppy's
current native dependencies, such as yyjson, llhttp, libuv, sqlite3, libpq, OpenSSL, zlib,
and lz4.

The package does not include V8 SDK headers/import libraries, database drivers, package
manager metadata, installers, or signed release metadata. Dependency license review and a
complete release notice file are still required before any public release.

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
$PackageVersion = "0.0.0-dev"
$Platform = "windows"
$Arch = "x64"
$Commit = Get-GitValue @("rev-parse", "--short", "HEAD")
if ([string]::IsNullOrWhiteSpace($Commit)) {
    $Commit = "unknown"
}

if (-not $SkipBuild) {
    Import-SlVisualStudioEnvironment

    $devScript = Join-Path $PSScriptRoot "dev.ps1"
    Invoke-Native "powershell" @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $devScript,
        "configure",
        "-Preset",
        $Preset
    )
    Invoke-Native "powershell" @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $devScript,
        "build",
        "-Preset",
        $Preset
    )

    $cargoArgs = @("build", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml"))
    if ($Configuration -eq "Release") {
        $cargoArgs += "--release"
    }
    Invoke-Native "cargo" $cargoArgs
}

$SloppyExe = Join-Path $BuildDir "sloppy.exe"
$SloppycExe = Join-Path (Join-Path $Root "compiler/target/$CargoProfile") "sloppyc.exe"
$PackageName = "sloppy-$PackageVersion-$Platform-$Arch"
$StageRoot = Join-Path $PackageOutputRoot "stage"
$PackageRoot = Join-Path $StageRoot $PackageName

Remove-Item -LiteralPath $PackageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $PackageRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "bin") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "share/sloppy/licenses") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PackageRoot "share/sloppy/schemas") | Out-Null

Copy-RequiredFile -Source $SloppyExe -Destination (Join-Path $PackageRoot "bin/sloppy.exe")
Copy-RequiredFile -Source $SloppycExe -Destination (Join-Path $PackageRoot "bin/sloppyc.exe")
Copy-NativeRuntimeDependencies -BuildDirectory $BuildDir -Destination (Join-Path $PackageRoot "bin")
Copy-DirectoryContents -Source (Join-Path $Root "stdlib/sloppy") -Destination (Join-Path $PackageRoot "lib/sloppy/stdlib/sloppy")
Copy-RequiredFile -Source (Join-Path $Root "LICENSE.md") -Destination (Join-Path $PackageRoot "LICENSE")
Write-PackageReadme -Path (Join-Path $PackageRoot "README.md")
$nativeRuntimeFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $PackageRoot "bin") -Filter "*.dll" -File |
        Select-Object -ExpandProperty Name
)
Write-ThirdPartyNotices -Path (Join-Path $PackageRoot "THIRD_PARTY_NOTICES.md") -RuntimeFiles $nativeRuntimeFiles

$containsExamples = $false
if ($IncludeExamples) {
    Copy-DirectoryContents -Source (Join-Path $Root "examples") -Destination (Join-Path $PackageRoot "share/sloppy/examples")
    $containsExamples = $true
}

$containsV8Runtime = $false
if ($IncludeV8Runtime) {
    $resolvedV8Root = $V8Root
    if ([string]::IsNullOrWhiteSpace($resolvedV8Root)) {
        $resolvedV8Root = $env:SLOPPY_V8_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($resolvedV8Root)) {
        throw "-IncludeV8Runtime requires -V8Root or SLOPPY_V8_ROOT."
    }

    $v8Bin = Join-Path $resolvedV8Root "bin"
    if (-not (Test-Path -LiteralPath $v8Bin -PathType Container)) {
        throw "-IncludeV8Runtime was requested, but no V8 runtime bin directory exists: $v8Bin"
    }

    $runtimeFiles = @(
        Get-ChildItem -LiteralPath $v8Bin -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in @(".dll", ".so", ".dylib") }
    )
    if ($runtimeFiles.Count -eq 0) {
        throw "-IncludeV8Runtime was requested, but no runtime DLL/shared library files were found under $v8Bin."
    }

    $v8Destination = Join-Path $PackageRoot "lib/sloppy/engines/v8"
    New-Item -ItemType Directory -Force -Path $v8Destination | Out-Null
    foreach ($file in $runtimeFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $v8Destination $file.Name) -Force
    }
    $containsV8Runtime = $true
}

$manifest = [ordered]@{
    name = "sloppy"
    version = $PackageVersion
    platform = $Platform
    arch = $Arch
    configuration = $Configuration
    commit = $Commit
    containsV8Runtime = $containsV8Runtime
    containsV8Sdk = $false
    containsStdlib = $true
    containsExamples = $containsExamples
    containsNativeRuntimeDependencies = $true
    tools = @("sloppy", "sloppyc")
    layoutVersion = 1
    notes = @(
        "experimental",
        "not production ready",
        "no installer",
        "no package manager",
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
    $smokeArgs = @("-PackagePath", $ArchivePath)
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
