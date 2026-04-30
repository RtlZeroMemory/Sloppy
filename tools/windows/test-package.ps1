param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

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
        [string]$WorkingRoot
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
        $runProcess.WaitForExit()
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
    Write-Host "Package smoke artifact execution skipped/not configured: packaged sloppy is not V8-enabled."
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

$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
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
    if ($manifest.name -ne "sloppy") {
        throw "Package smoke manifest name was not 'sloppy'."
    }
    if ($manifest.containsStdlib -ne $true) {
        throw "Package smoke manifest does not record stdlib inclusion."
    }
    if ($manifest.containsV8Sdk -ne $false) {
        throw "Package smoke manifest must not record V8 SDK inclusion."
    }
    if ($RequireV8Runtime -and $manifest.containsV8Runtime -ne $true) {
        throw "Package smoke required V8 runtime files, but manifest containsV8Runtime is not true."
    }
    if ((-not $RequireV8Runtime) -and $manifest.containsV8Runtime -eq $true) {
        Write-Host "Package smoke note: manifest records V8 runtime files. This run validates layout only; V8 execution still requires a V8-enabled package smoke."
    }

    Invoke-CliSmoke -Executable (Join-Path $packageRoot "bin/sloppy.exe") -Name "sloppy"
    Invoke-CliSmoke -Executable (Join-Path $packageRoot "bin/sloppyc.exe") -Name "sloppyc"

    foreach ($requiredFile in @("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "manifest.json")) {
        $requiredPath = Join-Path $packageRoot $requiredFile
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Package smoke missing required package file: $requiredFile"
        }
    }
    foreach ($requiredDirectory in @("share/sloppy/licenses", "share/sloppy/schemas")) {
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
    foreach ($asset in $stdlibAssets) {
        $assetPath = Join-Path $stdlibRoot $asset
        if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
            throw "Package smoke missing stdlib asset: $asset"
        }
    }

    Invoke-OutsideCheckoutCompilerSmoke `
        -SloppycExecutable (Join-Path $packageRoot "bin/sloppyc.exe") `
        -SloppyExecutable (Join-Path $packageRoot "bin/sloppy.exe") `
        -StdlibRoot $stdlibRoot `
        -WorkingRoot (Join-Path $tempRoot "outside-checkout-work")

    foreach ($excluded in @(".git", ".sdeps", "build", "compiler/target", "target", "vcpkg_installed")) {
        Assert-PackagePathMissing -Root $packageRoot -RelativePath $excluded
    }
    foreach ($excludedSdkFile in @("lib/sloppy/engines/v8/include/v8.h", "lib/sloppy/engines/v8/lib/v8_monolith.lib")) {
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
        $actualHash = (Get-FileHash -LiteralPath $resolvedPackage -Algorithm SHA256).Hash.ToLowerInvariant()
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
