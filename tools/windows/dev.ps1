param(
    [string]$Command = "help",

    [string]$Preset = "windows-dev",

    [string[]]$CMakeArgs = @(),

    [switch]$EnableV8,

    [ValidateSet("OFF", "AUTO", "REQUIRED")]
    [string]$V8Mode = "AUTO",

    [string]$V8Root,

    [string]$PackagePath = "",

    [string]$PackageMetadataPath = "",

    [switch]$FreshConfigure
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")
. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$BuildDir = Join-Path (Join-Path $Root "build") $Preset
$CompilerManifest = Join-Path $Root "compiler/Cargo.toml"
$SloppyIsCi = -not [string]::IsNullOrWhiteSpace($env:CI) -and
    $env:CI -ne "0" -and
    $env:CI -ne "false"

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

function Write-DevHelp {
    Write-Host "Usage: .\tools\windows\dev.ps1 <command> [options]"
    Write-Host ""
    Write-Host "Commands:"
    Write-Host "  doctor        Validate required and optional dependencies."
    Write-Host "  configure     Configure the selected CMake preset."
    Write-Host "  build         Build the selected CMake preset."
    Write-Host "  test          Run CTest and compiler tests."
    Write-Host "  lint          Run repository standards and hygiene checks."
    Write-Host "  format-check  Run C/C++ and Rust format checks."
    Write-Host "  package       Build an experimental local package archive."
    Write-Host "  test-package  Extract a package outside the checkout and run smoke checks."
    Write-Host "  analyze       Run the advanced static-analysis target."
    Write-Host "  clean         Remove the selected build directory."
    Write-Host "  all           Configure, build, and test."
    Write-Host ""
    Write-Host "Common options:"
    Write-Host "  -Preset <name>             CMake preset, default windows-dev."
    Write-Host "  -V8Mode OFF|AUTO|REQUIRED  Bootstrap/doctor/configure V8 mode."
    Write-Host "  -EnableV8                  Configure a V8-enabled preset."
    Write-Host "  -PackagePath <path>        Package archive for test-package."
    Write-Host "  -PackageMetadataPath <path> Fixture metadata for test-package."
}

function Stop-UnknownCommand {
    param([string]$Name)

    [Console]::Error.WriteLine("sloppy dev: unknown command '$Name'. Run .\tools\windows\dev.ps1 help for the command contract.")
    exit 2
}

function Resolve-VcpkgRoot {
    $localRoot = Join-Path $Root ".sdeps/vcpkg"
    $localToolchain = Join-Path $localRoot "scripts/buildsystems/vcpkg.cmake"
    if (Test-Path -LiteralPath $localToolchain) {
        return (Resolve-Path -LiteralPath $localRoot).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidate = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $env:VCPKG_ROOT).Path
        }
    }

    $vcpkgCommand = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $commandDir = Split-Path -Parent $vcpkgCommand.Source
        $commandToolchain = Join-Path $commandDir "scripts/buildsystems/vcpkg.cmake"
        if (Test-Path -LiteralPath $commandToolchain) {
            return (Resolve-Path -LiteralPath $commandDir).Path
        }
    }

    throw "vcpkg was not found. Set VCPKG_ROOT, install vcpkg on PATH, or bootstrap .sdeps/vcpkg."
}

function Assert-BuildDirectoryPath {
    param(
        [string]$Path
    )

    $buildRoot = [System.IO.Path]::GetFullPath((Join-Path $Root "build")).TrimEnd('\', '/')
    $target = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $buildRootPrefix = $buildRoot + [System.IO.Path]::DirectorySeparatorChar

    if ($target -eq $buildRoot) {
        throw "Refusing to clean the build root itself: $target"
    }

    if (-not $target.StartsWith($buildRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean path outside build root: $target"
    }

    return $target
}

function Remove-BuildDirectory {
    param(
        [string]$Path
    )

    $target = Assert-BuildDirectoryPath -Path $Path
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "Nothing to clean: $target"
        return
    }

    Remove-Item -LiteralPath $target -Recurse -Force
    Write-Host "Removed $target"
}

function Get-CMakeCacheValue {
    param(
        [string]$CachePath,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CachePath)) {
        return $null
    }

    $escapedName = [regex]::Escape($Name)
    $entry = Select-String -LiteralPath $CachePath -Pattern "^$escapedName(?::[^=]*)?=(.*)$" |
        Select-Object -First 1
    if ($null -eq $entry) {
        return $null
    }

    if ($entry.Line -match "^$escapedName(?::[^=]*)?=(.*)$") {
        return $matches[1]
    }

    return $null
}

function Get-ConfigureCacheRefreshReason {
    param(
        [string]$CachePath,
        [string]$ExpectedToolchain
    )

    if ($FreshConfigure) {
        return "Fresh configure requested."
    }

    if (-not (Test-Path -LiteralPath $CachePath)) {
        return $null
    }

    $cachedToolchain = Get-CMakeCacheValue -CachePath $CachePath -Name "CMAKE_TOOLCHAIN_FILE"
    if ([string]::IsNullOrWhiteSpace($cachedToolchain)) {
        return "CMake cache is missing CMAKE_TOOLCHAIN_FILE."
    }

    if ($cachedToolchain -notmatch "vcpkg[\\/]+scripts[\\/]buildsystems[\\/]vcpkg\.cmake$") {
        return "CMake cache was not configured with the vcpkg toolchain."
    }

    $expected = [System.IO.Path]::GetFullPath($ExpectedToolchain)
    $actual = [System.IO.Path]::GetFullPath($cachedToolchain)
    if (-not [string]::Equals($actual, $expected, [System.StringComparison]::OrdinalIgnoreCase)) {
        return "CMake cache uses a different vcpkg toolchain."
    }

    return $null
}

function Resolve-V8Root {
    return Resolve-SlV8SdkRoot -RepoRoot $Root -V8Root $V8Root -Require
}

function Resolve-GateTool {
    param(
        [string]$Name,
        [string]$Gate
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $message = "$Name was not found; $Gate cannot run."
    if ($SloppyIsCi) {
        throw $message
    }

    Write-Warning "$message Skipping this local gate."
    return $null
}

function Invoke-Doctor {
    $doctorScript = Join-Path $PSScriptRoot "deps-doctor.ps1"
    Invoke-Native "powershell" @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $doctorScript,
        "-V8Mode",
        $V8Mode
    )
}

function Invoke-Configure {
    if ([string]::IsNullOrWhiteSpace($Preset)) {
        throw "Preset must not be empty."
    }

    if ($EnableV8 -and $V8Mode -eq "OFF") {
        throw "-EnableV8 cannot be combined with -V8Mode OFF."
    }

    $enableV8ForConfigure = $EnableV8 -or $V8Mode -eq "REQUIRED"
    if ($enableV8ForConfigure -and $Preset -eq "windows-dev") {
        throw "The current V8 SDK is release/RelWithDebInfo. Use -Preset windows-relwithdebinfo with -EnableV8 or -V8Mode REQUIRED."
    }

    Import-SlVisualStudioEnvironment

    $vcpkgRoot = Resolve-VcpkgRoot
    $vcpkgToolchain = Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    $cachePath = Join-Path $BuildDir "CMakeCache.txt"
    $refreshReason = Get-ConfigureCacheRefreshReason -CachePath $cachePath -ExpectedToolchain $vcpkgToolchain
    if ($null -ne $refreshReason) {
        Write-Warning "$refreshReason Recreating $BuildDir with the repo Windows wrapper."
        Remove-BuildDirectory -Path $BuildDir
    }

    $cmakeConfigureArgs = @("--preset", $Preset)
    if (-not (Test-Path -LiteralPath $cachePath)) {
        $cmakeConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
    }
    $hasV8Selection = $enableV8ForConfigure -or @($CMakeArgs | Where-Object {
        $_ -match "^-DSLOPPY_(ENABLE_V8|ENGINE|V8_ROOT)="
    }).Count -gt 0
    if (-not $hasV8Selection) {
        $cmakeConfigureArgs += @("-DSLOPPY_ENGINE=none", "-DSLOPPY_ENABLE_V8=OFF")
    }
    if ($CMakeArgs.Count -gt 0) {
        $cmakeConfigureArgs += $CMakeArgs
    }
    if ($enableV8ForConfigure) {
        $resolvedV8Root = Resolve-V8Root
        $cmakeConfigureArgs += @("-DSLOPPY_ENABLE_V8=ON", "-DSLOPPY_V8_ROOT=$resolvedV8Root")
    }

    Invoke-Native "cmake" $cmakeConfigureArgs
}

function Invoke-Build {
    Import-SlVisualStudioEnvironment
    Invoke-Native "cmake" @("--build", "--preset", $Preset)
}

function Invoke-Test {
    Import-SlVisualStudioEnvironment
    Invoke-Native "ctest" @("--preset", $Preset, "--output-on-failure")

    $cargo = Resolve-GateTool "cargo" "cargo test"
    if ($null -ne $cargo) {
        Invoke-Native $cargo @("test", "--manifest-path", $CompilerManifest)
    }
}

function Invoke-Clean {
    Remove-BuildDirectory -Path $BuildDir
}

function Invoke-FormatCheck {
    $clangFormat = Resolve-GateTool "clang-format" "C/C++ format-check"
    if ($null -ne $clangFormat) {
        $paths = @(
            (Join-Path $Root "include"),
            (Join-Path $Root "src"),
            (Join-Path $Root "benchmarks"),
            (Join-Path $Root "tests")
        )

        $files = Get-ChildItem -Path $paths -Recurse -File |
            Where-Object { $_.Extension -in @(".c", ".h", ".cc", ".cpp", ".hpp") }

        if ($files.Count -gt 0) {
            $args = @("--dry-run", "--Werror")
            $args += $files.FullName
            Invoke-Native $clangFormat $args
        }
    }

    $cargo = Resolve-GateTool "cargo" "Rust format-check"
    $rustfmt = Resolve-GateTool "rustfmt" "Rust format-check"
    if ($null -ne $cargo -and $null -ne $rustfmt) {
        Invoke-Native $cargo @("fmt", "--manifest-path", $CompilerManifest, "--", "--check")
    }

    Write-Host "format-check completed."
}

function Invoke-ArtifactHygieneCheck {
    $git = Resolve-GateTool "git" "artifact hygiene check"
    if ($null -eq $git) {
        return
    }

    $staged = & $git diff --cached --name-only
    if ($LASTEXITCODE -ne 0) {
        throw "git diff --cached failed with exit code $LASTEXITCODE"
    }

    $artifactPatterns = @(
        "^artifacts/",
        "^build/",
        "^compiler/target/",
        "^target/",
        "^\.sdeps/",
        "^\.sloppy/",
        "\.exe$",
        "\.pdb$",
        "\.zip$",
        "\.7z$",
        "\.tar\.gz$"
    )

    $badStaged = @()
    foreach ($path in $staged) {
        $normalized = $path -replace "\\", "/"
        foreach ($pattern in $artifactPatterns) {
            if ($normalized -match $pattern) {
                $badStaged += $path
                break
            }
        }
    }

    if ($badStaged.Count -gt 0) {
        throw "Generated/ignored artifacts are staged: $($badStaged -join ', ')"
    }

    $trackedArtifacts = & $git ls-files -- artifacts build compiler/target target .sdeps .sloppy "*.exe" "*.pdb" "*.zip" "*.7z" "*.tar.gz"
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files artifact check failed with exit code $LASTEXITCODE"
    }

    if ($trackedArtifacts.Count -gt 0) {
        throw "Generated artifacts are tracked: $($trackedArtifacts -join ', ')"
    }

    Write-Host "artifact hygiene passed."
}

function Invoke-PlatformBoundaryCheck {
    $script = Join-Path $PSScriptRoot "check-platform-boundaries.ps1"
    & $script
    if (-not $?) {
        throw "platform boundary check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "platform boundary check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-CStandardsCheck {
    $script = Join-Path $PSScriptRoot "check-c-standards.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "C standards scanner self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "C standards scanner self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "C standards check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "C standards check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-JsTsStandardsCheck {
    $script = Join-Path $PSScriptRoot "check-js-ts-standards.ps1"
    & $script
    if (-not $?) {
        throw "JS/TS standards check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "JS/TS standards check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-RustStandardsCheck {
    $script = Join-Path $PSScriptRoot "check-rust-standards.ps1"
    & $script
    if (-not $?) {
        throw "Rust standards check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "Rust standards check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-DocsFreshnessCheck {
    $script = Join-Path $PSScriptRoot "check-docs-freshness.ps1"
    & $script
    if (-not $?) {
        throw "docs freshness check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "docs freshness check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-CoreApiIntegrationCheck {
    $script = Join-Path $PSScriptRoot "check-core-api-integration.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "core API integration scanner self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "core API integration scanner self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "core API integration check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "core API integration check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-TestGovernanceCheck {
    $script = Join-Path $PSScriptRoot "check-test-governance.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "test governance self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "test governance self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "test governance check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "test governance check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AlphaInfraCheck {
    $script = Join-Path $PSScriptRoot "check-alpha-infra.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "alpha infra self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "alpha infra self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "alpha infra check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "alpha infra check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-AlphaClaimsCheck {
    $script = Join-Path $PSScriptRoot "check-alpha-claims.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "alpha claims scanner self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "alpha claims scanner self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "alpha claims check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "alpha claims check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-ReleaseArtifactCheck {
    $script = Join-Path $PSScriptRoot "check-release-artifacts.ps1"
    & $script -SelfTest
    if (-not $?) {
        throw "release artifact checker self-test failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "release artifact checker self-test failed with exit code $LASTEXITCODE"
    }

    & $script
    if (-not $?) {
        throw "release artifact check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "release artifact check failed with exit code $LASTEXITCODE"
    }
}

function Invoke-CComplexityWarningCheck {
    $script = Join-Path $PSScriptRoot "check-c-complexity.ps1"
    & $script
    if (-not $?) {
        Write-Warning "C complexity warning scan could not complete."
    }
}

function Invoke-Lint {
    Import-SlVisualStudioEnvironment

    Invoke-PlatformBoundaryCheck
    Invoke-CStandardsCheck
    Invoke-JsTsStandardsCheck
    Invoke-RustStandardsCheck
    Invoke-DocsFreshnessCheck
    Invoke-CoreApiIntegrationCheck
    Invoke-TestGovernanceCheck
    Invoke-AlphaInfraCheck
    Invoke-AlphaClaimsCheck
    Invoke-ReleaseArtifactCheck
    Invoke-CComplexityWarningCheck

    $clangTidy = Resolve-GateTool "clang-tidy" "C/C++ lint"
    if ($null -ne $clangTidy) {
        $compileCommands = Join-Path $BuildDir "compile_commands.json"
        if (-not (Test-Path -LiteralPath $compileCommands)) {
            $message = "compile_commands.json was not found at $compileCommands; run configure before lint."
            if ($SloppyIsCi) {
                throw $message
            }

            Write-Warning "$message Skipping local C/C++ lint."
        } else {
            $lintFiles = @(
                (Join-Path $Root "src/main.c")
            )

            $lintFiles += Get-ChildItem -Path (Join-Path $Root "src/core") -Filter "*.c" -File |
                Select-Object -ExpandProperty FullName
            $lintFiles += Get-ChildItem -Path (Join-Path $Root "benchmarks") -Filter "*.c" -File |
                Select-Object -ExpandProperty FullName

            $testCorePath = Join-Path $Root "tests/unit/core"
            if (Test-Path -LiteralPath $testCorePath) {
                $lintFiles += Get-ChildItem -Path $testCorePath -Filter "*.c" -File |
                    Select-Object -ExpandProperty FullName
            }

            $args = @()
            $args += $lintFiles
            $args += @("-p", $BuildDir, "--warnings-as-errors=*")
            Invoke-Native $clangTidy $args
        }
    }

    $cargo = Resolve-GateTool "cargo" "Rust clippy"
    $clippy = Resolve-GateTool "clippy-driver" "Rust clippy"
    if ($null -ne $cargo -and $null -ne $clippy) {
        Invoke-Native $cargo @("clippy", "--manifest-path", $CompilerManifest, "--", "-D", "warnings")
    }

    Invoke-ArtifactHygieneCheck
    Write-Host "lint completed."
}

function Invoke-Analyze {
    Import-SlVisualStudioEnvironment

    $clangTidy = Resolve-GateTool "clang-tidy" "advanced C/C++ static analysis"
    if ($null -eq $clangTidy) {
        throw "clang-tidy was not found; install clang-tidy to run the advanced analysis lane."
    }

    $compileCommands = Join-Path $BuildDir "compile_commands.json"
    if (-not (Test-Path -LiteralPath $compileCommands)) {
        throw "compile_commands.json was not found at $compileCommands; run configure before analyze."
    }

    Invoke-Native "cmake" @("--build", "--preset", $Preset, "--target", "sloppy_memory_analysis")
    Write-Host "advanced memory/core static analysis completed."
}

function Get-PackageConfiguration {
    switch ($Preset) {
        "windows-dev" { return "Debug" }
        "windows-release" { return "Release" }
        "windows-relwithdebinfo" { return "RelWithDebInfo" }
        default {
            throw "Preset '$Preset' is not supported by the package lane. Use windows-dev, windows-release, or windows-relwithdebinfo."
        }
    }
}

function Invoke-Package {
    $script = Join-Path $PSScriptRoot "package.ps1"
    $nativeArgs = @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $script,
        "-Configuration",
        (Get-PackageConfiguration)
    )
    Invoke-Native "powershell" $nativeArgs
}

function Resolve-PackagePath {
    if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
        return (Resolve-Path -LiteralPath $PackagePath).Path
    }

    $packageRoot = Join-Path $Root "artifacts/packages"
    $packages = @()
    if (Test-Path -LiteralPath $packageRoot -PathType Container) {
        $packages = @(Get-ChildItem -LiteralPath $packageRoot -Filter "sloppy-*.zip" -File |
            Sort-Object LastWriteTimeUtc -Descending)
    }

    if ($packages.Count -eq 0) {
        throw "No Windows package archive found under $packageRoot. Run .\tools\windows\dev.ps1 package first or pass -PackagePath."
    }

    return $packages[0].FullName
}

function Invoke-TestPackage {
    $script = Join-Path $PSScriptRoot "test-package.ps1"
    $resolvedPackage = Resolve-PackagePath
    $metadata = if ([string]::IsNullOrWhiteSpace($PackageMetadataPath)) {
        Join-Path $Root "tests/fixtures/package/windows-default/case.json"
    } else {
        (Resolve-Path -LiteralPath $PackageMetadataPath).Path
    }

    Invoke-Native "powershell" @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $script,
        "-PackagePath",
        $resolvedPackage,
        "-MetadataPath",
        $metadata
    )
}

switch ($Command) {
    "help" { Write-DevHelp }
    "doctor" { Invoke-Doctor }
    "configure" { Invoke-Configure }
    "build" { Invoke-Build }
    "test" { Invoke-Test }
    "clean" { Invoke-Clean }
    "format-check" { Invoke-FormatCheck }
    "lint" { Invoke-Lint }
    "package" { Invoke-Package }
    "test-package" { Invoke-TestPackage }
    "analyze" { Invoke-Analyze }
    "all" {
        Invoke-Configure
        Invoke-Build
        Invoke-Test
    }
    default { Stop-UnknownCommand -Name $Command }
}
