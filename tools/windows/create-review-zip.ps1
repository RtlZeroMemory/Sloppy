param(
    [string]$OutputPath = "Slop-review.zip",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$git = Get-Command "git" -ErrorAction SilentlyContinue
if ($null -eq $git) {
    throw "git is required to create a tracked-file review zip."
}

Push-Location $Root
try {
    $tracked = & $git.Source ls-files
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed with exit code $LASTEXITCODE"
    }

    $excludedPatterns = @(
        "^\.git/",
        "^artifacts/",
        "^build/",
        "^compiler/target/",
        "^target/",
        "^\.sdeps/",
        "^\.sloppy/",
        "^vcpkg_installed/",
        "\.exe$",
        "\.pdb$",
        "\.zip$",
        "\.7z$",
        "\.tar\.gz$"
    )

    $files = @($tracked | Where-Object {
            $path = $_ -replace "\\", "/"
            $include = $true
            foreach ($pattern in $excludedPatterns) {
                if ($path -match $pattern) {
                    $include = $false
                    break
                }
            }
            $include -and (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf)
        })

    if ($files.Count -eq 0) {
        throw "No tracked source files found for review zip."
    }

    $resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    if ((Test-Path -LiteralPath $resolvedOutput) -and -not $Force) {
        throw "Output already exists: $resolvedOutput. Pass -Force to overwrite."
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-review-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tempRoot | Out-Null

    try {
        foreach ($file in $files) {
            $source = Join-Path $Root $file
            $destination = Join-Path $tempRoot $file
            $destinationDir = Split-Path -Parent $destination
            New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination
        }

        $archiveItems = @(Get-ChildItem -LiteralPath $tempRoot -Force)
        Compress-Archive -LiteralPath $archiveItems.FullName -DestinationPath $resolvedOutput -Force:$Force
        Write-Host "Created review zip: $resolvedOutput"
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
} finally {
    Pop-Location
}
