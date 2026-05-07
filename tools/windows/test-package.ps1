param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [string]$MetadataPath,

    [switch]$RequireV8Runtime,

    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

function Invoke-CliSmoke {
    param(
        [string]$Executable,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Package smoke missing ${Name}: $Executable"
    }

    & $Executable --version | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Name --version failed with exit code $LASTEXITCODE"
    }

    & $Executable --help | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Name --help failed with exit code $LASTEXITCODE"
    }
}

function ConvertTo-ProcessArgumentString {
    param(
        [string[]]$Arguments
    )

    $quoted = foreach ($argument in $Arguments) {
        '"' + ($argument -replace '"', '\"') + '"'
    }
    return ($quoted -join " ")
}

function Invoke-OutsideCheckoutCompilerSmoke {
    param(
        [string]$SloppycExecutable,
        [string]$SloppyExecutable,
        [string]$StdlibRoot,
        [string]$WorkingRoot,
        [switch]$RequireV8Runtime
    )

    $sourceDir = Join-Path $WorkingRoot "source"
    $artifactDir = Join-Path $WorkingRoot "artifacts"
    New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

    $sourcePath = Join-Path $sourceDir "app.js"
    @'
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Hello from packaged Sloppy"));

export default app;
'@ | Set-Content -LiteralPath $sourcePath -Encoding ASCII

    Push-Location $sourceDir
    try {
        & $SloppycExecutable build $sourcePath --out $artifactDir | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "packaged sloppyc build failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }

    foreach ($artifact in @("app.plan.json", "app.js", "app.js.map")) {
        $artifactPath = Join-Path $artifactDir $artifact
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Package smoke missing compiled artifact: $artifact"
        }
    }

    $runStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $runStartInfo.FileName = $SloppyExecutable
    $runStartInfo.WorkingDirectory = $WorkingRoot
    $runStartInfo.RedirectStandardOutput = $true
    $runStartInfo.RedirectStandardError = $true
    $runStartInfo.UseShellExecute = $false
    $runStartInfo.Arguments = ConvertTo-ProcessArgumentString @(
        "run",
        "--artifacts",
        $artifactDir,
        "--stdlib",
        $StdlibRoot,
        "--once",
        "GET",
        "/"
    )

    $runProcess = [System.Diagnostics.Process]::new()
    $runProcess.StartInfo = $runStartInfo
    try {
        if (-not $runProcess.Start()) {
            throw "packaged sloppy run failed to start."
        }
        $stdoutTask = $runProcess.StandardOutput.ReadToEndAsync()
        $stderrTask = $runProcess.StandardError.ReadToEndAsync()
        if (-not $runProcess.WaitForExit(60000)) {
            try {
                $null = $runProcess.CloseMainWindow()
            } catch {
                # Best-effort cleanup before forcing termination below.
            }
            if (-not $runProcess.WaitForExit(5000)) {
                try {
                    $runProcess.Kill()
                } catch {
                    # The timeout failure is reported below even if kill races with exit.
                }
            }
            throw "packaged sloppy run timed out after 60s."
        }
        $runOutput = @(
            $stdoutTask.GetAwaiter().GetResult(),
            $stderrTask.GetAwaiter().GetResult()
        )
        $runExit = $runProcess.ExitCode
    } finally {
        $runProcess.Dispose()
    }
    $runText = ($runOutput | Out-String)
    $runText | Write-Host
    if ($runExit -eq 0) {
        if ($runText -notmatch "Hello from packaged Sloppy") {
            throw "packaged sloppy run succeeded but did not return the expected response."
        }
        Write-Host "Package smoke V8 artifact execution passed from extracted package layout."
        return
    }

    if ($runText -notmatch "requires V8-enabled build") {
        throw "packaged sloppy run failed for an unexpected reason: $runText"
    }
    if ($RequireV8Runtime) {
        throw "Package smoke required V8 runtime execution, but packaged sloppy reported non-V8 build."
    }
    Write-Host "Package smoke artifact execution skipped/not configured: packaged sloppy is not V8-enabled."
}

function Invoke-OutsideCheckoutArtifactSmoke {
    param(
        [string]$SloppyExecutable,
        [string]$StdlibRoot,
        [string]$RepoRoot,
        [string]$WorkingRoot,
        [object]$Fixture,
        [switch]$RequireV8Runtime
    )

    if ($null -eq $Fixture -or [string]::IsNullOrWhiteSpace([string]$Fixture.path)) {
        throw "Package smoke metadata must name prebuiltArtifactFixture.path when source compilation is forbidden."
    }

    $sourceArtifactDir = Join-Path $RepoRoot ([string]$Fixture.path)
    if (-not (Test-Path -LiteralPath $sourceArtifactDir -PathType Container)) {
        throw "Package smoke prebuilt artifact fixture does not exist: $sourceArtifactDir"
    }

    $artifactDir = Join-Path $WorkingRoot "prebuilt-artifacts"
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $sourceArtifactDir -Force) {
        Copy-Item -LiteralPath $child.FullName -Destination $artifactDir -Recurse -Force
    }

    foreach ($artifact in @("app.plan.json", "app.js", "app.js.map")) {
        $artifactPath = Join-Path $artifactDir $artifact
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Package smoke prebuilt artifact fixture is missing: $artifact"
        }
    }

    foreach ($sourceOnly in @("sloppy.json", "src", "input.ts", "input.js")) {
        if (Test-Path -LiteralPath (Join-Path $artifactDir $sourceOnly)) {
            throw "Package smoke prebuilt artifact fixture contains source-only input: $sourceOnly"
        }
    }

    if ($null -eq $Fixture.once) {
        throw "Package smoke prebuilt artifact fixture must declare once.method and once.target."
    }

    $runStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $runStartInfo.FileName = $SloppyExecutable
    $runStartInfo.WorkingDirectory = $WorkingRoot
    $runStartInfo.RedirectStandardOutput = $true
    $runStartInfo.RedirectStandardError = $true
    $runStartInfo.UseShellExecute = $false
    $runStartInfo.Arguments = ConvertTo-ProcessArgumentString @(
        "run",
        "--artifacts",
        $artifactDir,
        "--stdlib",
        $StdlibRoot,
        "--once",
        [string]$Fixture.once.method,
        [string]$Fixture.once.target
    )

    $runProcess = [System.Diagnostics.Process]::new()
    $runProcess.StartInfo = $runStartInfo
    try {
        if (-not $runProcess.Start()) {
            throw "packaged artifact smoke failed to start."
        }
        $stdoutTask = $runProcess.StandardOutput.ReadToEndAsync()
        $stderrTask = $runProcess.StandardError.ReadToEndAsync()
        if (-not $runProcess.WaitForExit(60000)) {
            try {
                $runProcess.Kill()
            } catch {
                # The timeout failure is reported below.
            }
            throw "packaged artifact smoke timed out after 60s."
        }
        $runOutput = @(
            $stdoutTask.GetAwaiter().GetResult(),
            $stderrTask.GetAwaiter().GetResult()
        )
        $runExit = $runProcess.ExitCode
    } finally {
        $runProcess.Dispose()
    }

    $runText = ($runOutput | Out-String)
    $runText | Write-Host
    if ($runExit -eq 0) {
        foreach ($needle in @($Fixture.stdoutContains)) {
            if (-not [string]::IsNullOrWhiteSpace([string]$needle) -and
                -not $runText.Contains([string]$needle))
            {
                throw "packaged artifact smoke succeeded but did not contain expected output: $needle"
            }
        }
        Write-Host "Package smoke prebuilt artifact execution passed from extracted package layout."
        return
    }

    if ($runText -notmatch "requires V8-enabled build") {
        throw "packaged artifact smoke failed for an unexpected reason: $runText"
    }
    if ($RequireV8Runtime) {
        throw "Package smoke required V8 runtime execution, but packaged artifact run reported non-V8 build."
    }
    Write-Host "Package smoke prebuilt artifact execution skipped/not configured: packaged sloppy is not V8-enabled."
}

function Assert-PackagePathMissing {
    param(
        [string]$Root,
        [string]$RelativePath
    )

    $path = Join-Path $Root $RelativePath
    if (Test-Path -LiteralPath $path) {
        throw "Package smoke found excluded path: $RelativePath"
    }
}

function Assert-PackageFileMissing {
    param(
        [string]$Root,
        [string]$RelativePath
    )

    $path = Join-Path $Root $RelativePath
    if (Test-Path -LiteralPath $path) {
        throw "Package smoke found excluded file: $RelativePath"
    }
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

$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "../..")).Path
$metadata = $null
if (-not [string]::IsNullOrWhiteSpace($MetadataPath)) {
    $resolvedMetadata = (Resolve-Path -LiteralPath $MetadataPath).Path
    $metadata = Get-Content -LiteralPath $resolvedMetadata -Raw | ConvertFrom-Json
    if ($metadata.lane -ne "package outside-checkout") {
        throw "Package smoke metadata must declare lane 'package outside-checkout'."
    }
    if ($metadata.archiveKind -ne "windows-zip") {
        throw "Package smoke metadata archiveKind must be windows-zip."
    }
    if ($metadata.mustNotCompileSource -ne $true) {
        throw "Package smoke metadata must declare mustNotCompileSource=true."
    }
    if ($metadata.mustNotCompileSource -eq $true -and $metadata.outsideCheckoutCompile -eq $true) {
        throw "Package smoke metadata forbids source compilation when mustNotCompileSource=true."
    }
    if ($metadata.mustNotCompileSource -eq $true -and $null -eq $metadata.prebuiltArtifactFixture) {
        throw "Package smoke metadata must provide prebuiltArtifactFixture when source compilation is forbidden."
    }
    if ($metadata.requiresV8Runtime -eq $true) {
        $RequireV8Runtime = $true
    }
}
if ([System.IO.Path]::GetExtension($resolvedPackage) -ne ".zip") {
    throw "Windows package smoke expects a .zip archive: $resolvedPackage"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-package-smoke-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

try {
    Expand-Archive -LiteralPath $resolvedPackage -DestinationPath $tempRoot -Force

    $roots = @(Get-ChildItem -LiteralPath $tempRoot -Directory)
    if ($roots.Count -ne 1) {
        throw "Package smoke expected exactly one archive root directory, found $($roots.Count)."
    }

    $packageRoot = $roots[0].FullName
    $manifestPath = Join-Path $packageRoot "manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Package smoke missing manifest.json"
    }

    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $expectedManifest = [ordered]@{
        name = "sloppy"
        containsStdlib = $true
        containsV8Sdk = $false
    }
    if ($null -ne $metadata -and $null -ne $metadata.expectedManifest) {
        foreach ($property in $metadata.expectedManifest.PSObject.Properties) {
            $expectedManifest[$property.Name] = $property.Value
        }
    }
    foreach ($expected in $expectedManifest.GetEnumerator()) {
        $actualProperty = $manifest.PSObject.Properties[$expected.Key]
        $actualValue = if ($null -eq $actualProperty) { $null } else { $actualProperty.Value }
        if ($actualValue -ne $expected.Value) {
            throw "Package smoke manifest field '$($expected.Key)' was '$actualValue', expected '$($expected.Value)'."
        }
    }
    if ($RequireV8Runtime -and $manifest.containsV8Runtime -ne $true) {
        throw "Package smoke required V8 runtime files, but manifest containsV8Runtime is not true."
    }
    if ((-not $RequireV8Runtime) -and $manifest.containsV8Runtime -eq $true) {
        Write-Host "Package smoke note: manifest records V8 runtime files. This run validates layout only; V8 execution still requires a V8-enabled package smoke."
    }

    Invoke-CliSmoke -Executable (Join-Path $packageRoot "bin/sloppy.exe") -Name "sloppy"
    Invoke-CliSmoke -Executable (Join-Path $packageRoot "bin/sloppyc.exe") -Name "sloppyc"

    $requiredFiles = @("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "manifest.json")
    if ($null -ne $metadata -and $null -ne $metadata.requiredFiles) {
        $requiredFiles = @($metadata.requiredFiles)
    }
    foreach ($requiredFile in $requiredFiles) {
        $requiredPath = Join-Path $packageRoot $requiredFile
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Package smoke missing required package file: $requiredFile"
        }
    }
    $requiredDirectories = @("share/sloppy/licenses", "share/sloppy/schemas")
    if ($null -ne $metadata -and $null -ne $metadata.requiredDirectories) {
        $requiredDirectories = @($metadata.requiredDirectories)
    }
    foreach ($requiredDirectory in $requiredDirectories) {
        $requiredPath = Join-Path $packageRoot $requiredDirectory
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
            throw "Package smoke missing required package directory: $requiredDirectory"
        }
    }

    $stdlibRoot = Join-Path $packageRoot "lib/sloppy/stdlib/sloppy"
    $stdlibAssets = @(
        "index.js",
        "app.js",
        "results.js",
        "schema.js",
        "data.js",
        "bootstrap.manifest.json",
        "internal/intrinsics.js"
    )
    if ($null -ne $metadata -and $null -ne $metadata.requiredStdlibAssets) {
        $stdlibAssets = @($metadata.requiredStdlibAssets)
    }
    foreach ($asset in $stdlibAssets) {
        $assetPath = Join-Path $stdlibRoot $asset
        if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
            throw "Package smoke missing stdlib asset: $asset"
        }
    }

    if ($null -ne $metadata -and $null -ne $metadata.prebuiltArtifactFixture) {
        Invoke-OutsideCheckoutArtifactSmoke `
            -SloppyExecutable (Join-Path $packageRoot "bin/sloppy.exe") `
            -StdlibRoot $stdlibRoot `
            -RepoRoot $repoRoot `
            -WorkingRoot (Join-Path $tempRoot "outside-checkout-artifact-work") `
            -Fixture $metadata.prebuiltArtifactFixture `
            -RequireV8Runtime:$RequireV8Runtime
    }

    if ($null -eq $metadata -or $metadata.outsideCheckoutCompile -eq $true) {
        Invoke-OutsideCheckoutCompilerSmoke `
            -SloppycExecutable (Join-Path $packageRoot "bin/sloppyc.exe") `
            -SloppyExecutable (Join-Path $packageRoot "bin/sloppy.exe") `
            -StdlibRoot $stdlibRoot `
            -WorkingRoot (Join-Path $tempRoot "outside-checkout-work") `
            -RequireV8Runtime:$RequireV8Runtime
    }

    $excludedPaths = @(".git", ".sdeps", "build", "compiler/target", "target", "vcpkg_installed")
    if ($null -ne $metadata -and $null -ne $metadata.excludedPaths) {
        $excludedPaths = @($metadata.excludedPaths)
    }
    foreach ($excluded in $excludedPaths) {
        Assert-PackagePathMissing -Root $packageRoot -RelativePath $excluded
    }
    $excludedFiles = @("lib/sloppy/engines/v8/include/v8.h", "lib/sloppy/engines/v8/lib/v8_monolith.lib")
    if ($null -ne $metadata -and $null -ne $metadata.excludedFiles) {
        $excludedFiles = @($metadata.excludedFiles)
    }
    foreach ($excludedSdkFile in $excludedFiles) {
        Assert-PackageFileMissing -Root $packageRoot -RelativePath $excludedSdkFile
    }

    $v8RuntimeRoot = Join-Path $packageRoot "lib/sloppy/engines/v8"
    if ($RequireV8Runtime) {
        if (-not (Test-Path -LiteralPath $v8RuntimeRoot -PathType Container)) {
            throw "Package smoke required V8 runtime files, but lib/sloppy/engines/v8 is missing."
        }
        $v8RuntimeFiles = @(
            Get-ChildItem -LiteralPath $v8RuntimeRoot -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Extension -in @(".dll", ".so", ".dylib") }
        )
        if ($v8RuntimeFiles.Count -eq 0) {
            throw "Package smoke required V8 runtime files, but no DLL/shared-library files were found."
        }
        Write-Host "Package smoke V8 runtime files:"
        $v8RuntimeFiles | ForEach-Object { Write-Host "- $($_.Name)" }
    }

    $checksumPath = Join-Path (Split-Path -Parent $resolvedPackage) "SHA256SUMS.txt"
    if (Test-Path -LiteralPath $checksumPath -PathType Leaf) {
        $actualHash = Get-Sha256Hex -Path $resolvedPackage
        $expectedName = [regex]::Escape((Split-Path -Leaf $resolvedPackage))
        $expectedLine = Get-Content -LiteralPath $checksumPath |
            Where-Object { $_ -match "\s+\*?$expectedName$" } |
            Select-Object -First 1
        if (-not $expectedLine) {
            throw "Package smoke checksum file is missing an entry for archive: $(Split-Path -Leaf $resolvedPackage)"
        }

        if ($expectedLine -notmatch "^$actualHash\s+\*?$expectedName$") {
            throw "Package smoke checksum file entry for archive does not match archive hash."
        }
    }

    Write-Host "Package smoke passed: $resolvedPackage"
    Write-Host "Extracted package root: $packageRoot"
} finally {
    if ($KeepTemp) {
        Write-Host "Keeping smoke temp directory: $tempRoot"
    } else {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
