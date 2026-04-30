param(
    [switch]$List,
    [switch]$Smoke,
    [switch]$Json,
    [switch]$IncludeV8,
    [string]$Benchmark,
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug",
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments,
        [switch]$StdoutToStderr
    )

    if ($StdoutToStderr) {
        $previousErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            & $File @Arguments 2>&1 | ForEach-Object {
                [Console]::Error.WriteLine($_)
            }
        }
        finally {
            $ErrorActionPreference = $previousErrorActionPreference
        }
    }
    else {
        & $File @Arguments
    }
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

function Resolve-Preset {
    switch ($Configuration) {
        "Release" { return "windows-release" }
        "RelWithDebInfo" { return "windows-relwithdebinfo" }
        default { return "windows-dev" }
    }
}

Import-SlVisualStudioEnvironment

$Preset = Resolve-Preset
$BuildDir = Join-Path (Join-Path $Root "build") $Preset
$BenchExe = Join-Path $BuildDir "sloppy_bench.exe"

$vcpkgRoot = Resolve-VcpkgRoot
$vcpkgToolchain = Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake"
$configureArgs = @("--preset", $Preset)
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "CMakeCache.txt"))) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
}

$hasV8Selection = @($CMakeArgs | Where-Object {
        $_ -match "^-DSLOPPY_(ENABLE_V8|ENGINE|V8_ROOT)(:[^=]+)?="
    }).Count -gt 0
if (-not $hasV8Selection) {
    $configureArgs += @("-DSLOPPY_ENGINE=none", "-DSLOPPY_ENABLE_V8=OFF")
}
if ($CMakeArgs.Count -gt 0) {
    $configureArgs += $CMakeArgs
}

Invoke-Native "cmake" $configureArgs -StdoutToStderr:$Json
Invoke-Native "cmake" @("--build", "--preset", $Preset, "--target", "sloppy_bench") -StdoutToStderr:$Json

$benchArgs = @()
if ($List) {
    $benchArgs += "--list"
}
if ($Smoke) {
    $benchArgs += "--smoke"
}
if ($Json) {
    $benchArgs += @("--format", "json")
}
if ($IncludeV8) {
    $benchArgs += "--include-v8"
}
if (-not [string]::IsNullOrWhiteSpace($Benchmark)) {
    $benchArgs += @("--bench", $Benchmark)
}

Invoke-Native $BenchExe $benchArgs
