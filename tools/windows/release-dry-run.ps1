param(
    [string]$Preset = "windows-release",
    [string]$OutputDir = "artifacts/packages",
    [switch]$SkipPackage,
    [switch]$SkipSmoke,
    [string]$SummaryPath = "artifacts/release-dry-run/windows-summary.json"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File failed with exit code $LASTEXITCODE"
    }
}

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
}

function Get-PackageConfiguration {
    switch ($Preset) {
        "windows-dev" { return "Debug" }
        "windows-release" { return "Release" }
        "windows-relwithdebinfo" { return "RelWithDebInfo" }
        default {
            throw "Preset '$Preset' is not supported by the release dry-run package lane. Use windows-dev, windows-release, or windows-relwithdebinfo."
        }
    }
}

$startedAt = [DateTimeOffset]::UtcNow
$packageDir = Resolve-RepoPath -Path $OutputDir
$summaryFile = Resolve-RepoPath -Path $SummaryPath
$devScript = Join-Path $PSScriptRoot "dev.ps1"
$packageScript = Join-Path $PSScriptRoot "package.ps1"
$powerShellExe = (Get-Process -Id $PID).Path
if ([string]::IsNullOrWhiteSpace($powerShellExe)) {
    $powerShellExe = if ($PSVersionTable.PSEdition -eq "Core") { "pwsh" } else { "powershell" }
}

Invoke-Native $powerShellExe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "check-release-artifacts.ps1"), "-SelfTest")
Invoke-Native $powerShellExe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "check-release-artifacts.ps1"), "-PackageDirectory", $packageDir)

if (-not $SkipPackage) {
    Invoke-Native $powerShellExe @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $packageScript,
        "-Configuration",
        (Get-PackageConfiguration),
        "-OutputDir",
        $packageDir
    )
}

$packagePath = ""
if (Test-Path -LiteralPath $packageDir -PathType Container) {
    $latest = Get-ChildItem -LiteralPath $packageDir -File |
        Where-Object { $_.Name -eq "sloppy-windows-x64.zip" -or $_.Name -like "sloppy-*-windows-x64.zip" } |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -ne $latest) {
        $packagePath = $latest.FullName
    }
}

if (-not $SkipSmoke) {
    if ([string]::IsNullOrWhiteSpace($packagePath)) {
        throw "release dry-run could not find a package archive under $packageDir."
    }
    Invoke-Native $powerShellExe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $devScript, "test-package", "-PackagePath", $packagePath)
}

Invoke-Native $powerShellExe @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "check-release-artifacts.ps1"), "-PackageDirectory", $packageDir)

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $summaryFile) | Out-Null
[ordered]@{
    kind = "sloppy-alpha-release-dry-run"
    platform = "windows"
    arch = "x64"
    preset = $Preset
    packageDirectory = $packageDir
    packagePath = $packagePath
    checksumPath = Join-Path $packageDir "SHA256SUMS.txt"
    packageBuilt = (-not $SkipPackage)
    packageSmokeRun = (-not $SkipSmoke)
    publicReleaseCreated = $false
    secretsRequired = $false
    startedAtUtc = $startedAt.ToString("o")
    completedAtUtc = ([DateTimeOffset]::UtcNow).ToString("o")
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryFile -Encoding ASCII

Write-Host "release dry-run summary: $summaryFile"
Write-Host "release dry-run completed without creating a public release."
