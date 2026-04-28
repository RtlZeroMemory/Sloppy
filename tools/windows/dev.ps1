param(
    [ValidateSet("configure", "build", "test", "clean", "format-check", "lint", "all")]
    [string]$Command = "all",

    [string]$Preset = "windows-dev",

    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

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

function Invoke-Configure {
    Import-SlVisualStudioEnvironment

    $vcpkgRoot = Resolve-VcpkgRoot
    $vcpkgToolchain = Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    $args = @("--preset", $Preset)
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
        $args += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
    }
    if ($CMakeArgs.Count -gt 0) {
        $args += $CMakeArgs
    }

    Invoke-Native "cmake" $args
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
    $buildRoot = Join-Path $Root "build"
    $buildDir = Join-Path $buildRoot $Preset

    if (-not (Test-Path -LiteralPath $buildDir)) {
        Write-Host "Nothing to clean: $buildDir"
        return
    }

    $resolvedBuildRoot = (Resolve-Path -LiteralPath $buildRoot).Path
    $resolvedBuildDir = (Resolve-Path -LiteralPath $buildDir).Path

    if (-not $resolvedBuildDir.StartsWith($resolvedBuildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean path outside build root: $resolvedBuildDir"
    }

    Remove-Item -LiteralPath $resolvedBuildDir -Recurse -Force
    Write-Host "Removed $resolvedBuildDir"
}

function Invoke-FormatCheck {
    $clangFormat = Resolve-GateTool "clang-format" "C/C++ format-check"
    if ($null -ne $clangFormat) {
        $paths = @(
            (Join-Path $Root "include"),
            (Join-Path $Root "src"),
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

    $trackedArtifacts = & $git ls-files -- build compiler/target target .sdeps .sloppy "*.exe" "*.pdb" "*.zip" "*.7z" "*.tar.gz"
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
    & $script
    if (-not $?) {
        throw "C standards check failed"
    }

    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "C standards check failed with exit code $LASTEXITCODE"
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
    Invoke-DocsFreshnessCheck
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

switch ($Command) {
    "configure" { Invoke-Configure }
    "build" { Invoke-Build }
    "test" { Invoke-Test }
    "clean" { Invoke-Clean }
    "format-check" { Invoke-FormatCheck }
    "lint" { Invoke-Lint }
    "all" {
        Invoke-Configure
        Invoke-Build
        Invoke-Test
    }
}
