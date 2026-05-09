param(
    [switch]$List,
    [switch]$Smoke,
    [switch]$Json,
    [switch]$IncludeV8,
    [string]$Benchmark,
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Configuration = "Debug",
    [string[]]$CMakeArgs = @(),
    [string[]]$Suite,
    [string[]]$Runtime,
    [string]$Out,
    [string[]]$Compare = @(),
    [int]$WarmupRequests = 10,
    [int]$Requests = 100,
    [int]$TimeoutSeconds = 20,
    [string]$SloppyExe
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

$usesLocalRuntimeEngine =
    $PSBoundParameters.ContainsKey("Suite") -or
    $PSBoundParameters.ContainsKey("Runtime") -or
    $PSBoundParameters.ContainsKey("Out") -or
    $PSBoundParameters.ContainsKey("Compare") -or
    $PSBoundParameters.ContainsKey("WarmupRequests") -or
    $PSBoundParameters.ContainsKey("Requests") -or
    $PSBoundParameters.ContainsKey("TimeoutSeconds") -or
    $PSBoundParameters.ContainsKey("SloppyExe")

if ($usesLocalRuntimeEngine) {
    if ($List -or $Smoke -or $Json -or $IncludeV8 -or
        -not [string]::IsNullOrWhiteSpace($Benchmark) -or
        $PSBoundParameters.ContainsKey("Configuration") -or
        $CMakeArgs.Count -gt 0) {
        throw "local runtime benchmark options (-Suite/-Runtime/-Out/-Compare) cannot be mixed with native sloppy_bench options (-List/-Smoke/-Json/-IncludeV8/-Benchmark/-Configuration/-CMakeArgs)"
    }

    $localBench = Join-Path $PSScriptRoot "local-bench.ps1"
    $localParams = @{}
    if ($PSBoundParameters.ContainsKey("Suite")) {
        $localParams["Suite"] = $Suite
    }
    if ($PSBoundParameters.ContainsKey("Runtime")) {
        $localParams["Runtime"] = $Runtime
    }
    if ($PSBoundParameters.ContainsKey("Out")) {
        $localParams["Out"] = $Out
    }
    if ($PSBoundParameters.ContainsKey("Compare")) {
        $localParams["Compare"] = $Compare
    }
    if ($PSBoundParameters.ContainsKey("WarmupRequests")) {
        $localParams["WarmupRequests"] = $WarmupRequests
    }
    if ($PSBoundParameters.ContainsKey("Requests")) {
        $localParams["Requests"] = $Requests
    }
    if ($PSBoundParameters.ContainsKey("TimeoutSeconds")) {
        $localParams["TimeoutSeconds"] = $TimeoutSeconds
    }
    if ($PSBoundParameters.ContainsKey("SloppyExe")) {
        $localParams["SloppyExe"] = $SloppyExe
    }

    & $localBench @localParams
    if ($LASTEXITCODE -ne 0) {
        throw "local runtime benchmark engine failed with exit code $LASTEXITCODE"
    }
    exit 0
}

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
