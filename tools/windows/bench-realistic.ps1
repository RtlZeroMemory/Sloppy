[CmdletBinding()]
param(
    [ValidateSet("http", "startup", "stress", "all")]
    [string]$Suite = "http",

    [string[]]$Runtime = @("sloppy", "node", "bun", "deno"),
    [string[]]$Category = @(),
    [string[]]$Workload = @(),
    [int]$DurationSeconds = 0,
    [int]$WarmupSeconds = 0,
    [string[]]$Connections = @(),
    [int]$Iterations = 0,
    [string]$Out = "artifacts/bench/realistic",
    [string[]]$RequireRuntime = @(),
    [string]$SloppyExe = "",
    [switch]$Quick,
    [switch]$Full,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

if ($PSBoundParameters.ContainsKey("DurationSeconds") -and $DurationSeconds -lt 0) {
    throw "-DurationSeconds must be greater than or equal to 0."
}
if ($PSBoundParameters.ContainsKey("WarmupSeconds") -and $WarmupSeconds -lt 0) {
    throw "-WarmupSeconds must be greater than or equal to 0."
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..\..")
$runner = Join-Path $repoRoot "benchmarks\realistic\runner\bench-realistic.ts"

$outPath = if ([System.IO.Path]::IsPathRooted($Out)) {
    $Out
} else {
    Join-Path $repoRoot $Out
}

function Resolve-SloppyExe([string]$Explicit) {
    if ($Explicit -ne "") {
        return (Resolve-Path $Explicit).Path
    }
    $candidates = @(
        (Join-Path $repoRoot "build\windows-relwithdebinfo\sloppy.exe"),
        (Join-Path $repoRoot "build\windows-release\sloppy.exe"),
        (Join-Path $repoRoot "build\windows-debug\sloppy.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }
    $cmd = Get-Command sloppy -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    throw "Sloppy executable was not found. Build Sloppy or pass -SloppyExe."
}

$sloppyPath = Resolve-SloppyExe $SloppyExe
$sloppycPath = Join-Path $repoRoot "compiler\target\release\sloppyc.exe"
if (-not (Test-Path $sloppycPath)) {
    $sloppycPath = Join-Path $repoRoot "compiler\target\debug\sloppyc.exe"
}

$config = [ordered]@{
    suite = $Suite
    runtimes = $Runtime
    categories = $Category
    workloads = $Workload
    durationSeconds = $(if ($PSBoundParameters.ContainsKey("DurationSeconds")) { $DurationSeconds } else { $null })
    warmupSeconds = $(if ($PSBoundParameters.ContainsKey("WarmupSeconds")) { $WarmupSeconds } else { $null })
    connections = @($Connections | ForEach-Object { [int]$_ })
    iterations = $(if ($Iterations -gt 0) { $Iterations } else { $null })
    out = $outPath
    requireRuntimes = $RequireRuntime
    quick = [bool]$Quick
    full = [bool]$Full
    dryRun = [bool]$DryRun
    repoRoot = $repoRoot.Path
    sloppyExe = $sloppyPath
    sloppycExe = $(if (Test-Path $sloppycPath) { (Resolve-Path $sloppycPath).Path } else { "" })
    runtimePaths = [ordered]@{
        node = $(if (Get-Command node -ErrorAction SilentlyContinue) { (Get-Command node).Source } else { "" })
        bun = $(if (Get-Command bun -ErrorAction SilentlyContinue) { (Get-Command bun).Source } else { "" })
        deno = $(if (Get-Command deno -ErrorAction SilentlyContinue) { (Get-Command deno).Source } else { "" })
    }
}

New-Item -ItemType Directory -Force -Path $outPath | Out-Null
$configPath = Join-Path $outPath "bench-realistic.config.json"
$configJson = $config | ConvertTo-Json -Depth 8
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($configPath, $configJson, $utf8NoBom)

Push-Location $repoRoot
$oldConfig = $env:SLOPPY_BENCH_CONFIG
$oldRepo = $env:SLOPPY_BENCH_REPO
try {
    $env:SLOPPY_BENCH_CONFIG = $configPath
    $env:SLOPPY_BENCH_REPO = $repoRoot.Path
    & $sloppyPath run $runner --kind program
    exit $LASTEXITCODE
}
finally {
    if ($null -eq $oldConfig) {
        Remove-Item Env:\SLOPPY_BENCH_CONFIG -ErrorAction SilentlyContinue
    } else {
        $env:SLOPPY_BENCH_CONFIG = $oldConfig
    }
    if ($null -eq $oldRepo) {
        Remove-Item Env:\SLOPPY_BENCH_REPO -ErrorAction SilentlyContinue
    } else {
        $env:SLOPPY_BENCH_REPO = $oldRepo
    }
    Pop-Location
}
