param(
    [string]$Destination = ".sdeps/v8",
    [string]$V8Root = "",
    [string]$Platform = "windows-x64",
    [string]$SourceUrl = "",
    [string]$SourceArchive = "",
    [string]$Sha256 = "",
    [switch]$ValidateOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

function Resolve-RepoPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path value must not be empty."
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
}

function Assert-SafeDirectoryTarget {
    param(
        [string]$Name,
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $repoRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathRoot = [System.IO.Path]::GetPathRoot($fullPath).TrimEnd('\', '/')

    if ([string]::IsNullOrWhiteSpace($fullPath) -or
        $fullPath -eq "." -or
        $fullPath -eq ".." -or
        $fullPath -eq $pathRoot -or
        $fullPath -eq $repoRoot) {
        throw "Refusing to use unsafe ${Name}: $Path"
    }

    return $fullPath
}

function Read-SlV8ArtifactSource {
    param([string]$RequestedPlatform)

    if (-not (Test-Path -LiteralPath $SlDepsManifestPath -PathType Leaf)) {
        throw "Dependency manifest was not found: $SlDepsManifestPath"
    }

    $manifest = Get-Content -LiteralPath $SlDepsManifestPath -Raw | ConvertFrom-Json
    $artifactSource = $manifest.v8Sdk.artifactSource
    if ($null -eq $artifactSource -or $artifactSource.downloadImplemented -ne $true) {
        throw "V8 SDK artifact download is not implemented in the dependency manifest."
    }

    $platformEntry = $artifactSource.platforms.PSObject.Properties[$RequestedPlatform].Value
    if ($null -eq $platformEntry -or $platformEntry.status -ne "available") {
        throw "No available V8 SDK artifact source is configured for platform '$RequestedPlatform'."
    }

    return $platformEntry
}

function Get-ArchiveNameFromUrl {
    param([string]$Url)

    try {
        $uri = [uri]$Url
        $leaf = [System.IO.Path]::GetFileName($uri.AbsolutePath)
        return [uri]::UnescapeDataString($leaf)
    } catch {
        throw "Invalid V8 SDK artifact URL: $Url"
    }
}

function Assert-Sha256 {
    param(
        [string]$Path,
        [string]$ExpectedSha256
    )

    if ([string]::IsNullOrWhiteSpace($ExpectedSha256) -or $ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "A 64-character SHA-256 checksum is required for V8 SDK artifact validation."
    }

    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $expected = $ExpectedSha256.ToLowerInvariant()
    if ($actual -ne $expected) {
        throw "V8 SDK artifact checksum mismatch. Expected $expected but found $actual for $Path."
    }
}

function Expand-ZipArchive {
    param(
        [string]$ArchivePath,
        [string]$DestinationPath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($ArchivePath, $DestinationPath)
}

if ($ValidateOnly) {
    $rootToValidate = $V8Root
    if ([string]::IsNullOrWhiteSpace($rootToValidate)) {
        $rootToValidate = Resolve-SlV8SdkRoot -RepoRoot $Root -Require
    }

    if (-not (Test-SlV8SdkLayout -Root $rootToValidate)) {
        exit 1
    }

    exit 0
}

if ($Platform -ne $SlV8DefaultPlatform) {
    throw "V8 SDK artifact fetch is currently available for $SlV8DefaultPlatform only. Requested: $Platform"
}

$destinationRoot = Assert-SafeDirectoryTarget -Name "Destination" -Path (Resolve-RepoPath $Destination)
$sdkRoot = Assert-SafeDirectoryTarget -Name "SDK root" -Path (Join-Path $destinationRoot $Platform)

if (-not $Force -and (Test-SlV8SdkLayout -Root $sdkRoot -Quiet)) {
    Write-Host "Compatible Sloppy V8 SDK already exists:"
    Write-Host "  $sdkRoot"
    exit 0
}

$source = Read-SlV8ArtifactSource -RequestedPlatform $Platform
$url = if ([string]::IsNullOrWhiteSpace($SourceUrl)) { [string]$source.url } else { $SourceUrl }
$expectedSha256 = if ([string]::IsNullOrWhiteSpace($Sha256)) { [string]$source.sha256 } else { $Sha256 }

if ([string]::IsNullOrWhiteSpace($SourceArchive) -and [string]::IsNullOrWhiteSpace($url)) {
    throw "No V8 SDK artifact URL is configured for $Platform."
}

New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
$archivePath = ""
if (-not [string]::IsNullOrWhiteSpace($SourceArchive)) {
    $archivePath = [System.IO.Path]::GetFullPath($SourceArchive)
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "V8 SDK source archive was not found: $archivePath"
    }
} else {
    $downloadRoot = Join-Path $destinationRoot "_downloads"
    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
    $archiveName = Get-ArchiveNameFromUrl -Url $url
    if ([string]::IsNullOrWhiteSpace($archiveName)) {
        throw "Could not determine V8 SDK archive name from URL: $url"
    }

    $archivePath = Join-Path $downloadRoot $archiveName
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        try {
            Assert-Sha256 -Path $archivePath -ExpectedSha256 $expectedSha256
            Write-Host "Using cached Sloppy V8 SDK artifact:"
            Write-Host "  $archivePath"
        } catch {
            Write-Host "Cached Sloppy V8 SDK artifact failed checksum validation; downloading again."
            Remove-Item -LiteralPath $archivePath -Force
        }
    }

    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        $tempArchive = "$archivePath.download"
        Remove-Item -LiteralPath $tempArchive -Force -ErrorAction SilentlyContinue
        Write-Host "Downloading Sloppy V8 SDK artifact:"
        Write-Host "  $url"
        $oldProgressPreference = $ProgressPreference
        $ProgressPreference = "SilentlyContinue"
        try {
            Invoke-WebRequest -Uri $url -OutFile $tempArchive -UseBasicParsing
        } finally {
            $ProgressPreference = $oldProgressPreference
        }
        Move-Item -LiteralPath $tempArchive -Destination $archivePath -Force
    }
}

Assert-Sha256 -Path $archivePath -ExpectedSha256 $expectedSha256

$extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-v8-sdk-extract-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
try {
    Expand-ZipArchive -ArchivePath $archivePath -DestinationPath $extractRoot

    $extractedSdkRoot = Join-Path $extractRoot $Platform
    if (-not (Test-Path -LiteralPath $extractedSdkRoot -PathType Container)) {
        $extractedSdkRoot = $extractRoot
    }

    if (-not (Test-SlV8SdkLayout -Root $extractedSdkRoot)) {
        throw "Downloaded V8 SDK artifact did not contain a compatible SDK layout."
    }

    Remove-Item -LiteralPath $sdkRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sdkRoot) | Out-Null
    Move-Item -LiteralPath $extractedSdkRoot -Destination $sdkRoot -Force
} finally {
    Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not (Test-SlV8SdkLayout -Root $sdkRoot)) {
    throw "Installed V8 SDK failed validation: $sdkRoot"
}

Write-Host ""
Write-Host "Installed Sloppy V8 SDK:"
Write-Host "  $sdkRoot"
