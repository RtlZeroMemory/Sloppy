param(
    [int]$Iterations = 100,
    [int]$Warmup = 10,
    [int]$Repeat = 1,
    [string]$SloppyBin = "",
    [switch]$HttpProfile,
    [string]$Runtime = "",
    [string]$Scenario = "",
    [string]$Out = "artifacts/bench/json-competitors.json",
    [switch]$Report,
    [string]$ReportOut = "",
    [string]$Compare = "",
    [string]$ProfileInput = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Runner = Join-Path $Root "benchmarks/competitors/json-local.mjs"
$Reporter = Join-Path $Root "benchmarks/competitors/report-json-local.mjs"
$outPath = Join-Path $Root $Out

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$args = @("--iterations", $Iterations, "--warmup", $Warmup, "--repeat", $Repeat, "--out", $outPath)
if ($SloppyBin -ne "") {
    $sloppyBinPath = $SloppyBin
    if (-not [System.IO.Path]::IsPathRooted($sloppyBinPath)) {
        $sloppyBinPath = Join-Path $Root $sloppyBinPath
    }
    $args += @("--sloppy-bin", (Resolve-Path -LiteralPath $sloppyBinPath).Path)
}
if ($HttpProfile) {
    $args += @("--http-profile", "true", "--http-profile-out", (Join-Path $Root "artifacts/bench"))
}
if ($Runtime -ne "") {
    $args += @("--runtime", $Runtime)
}
if ($Scenario -ne "") {
    $args += @("--scenario", $Scenario)
}
node $Runner @args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Report) {
    if ($ReportOut -eq "") {
        $ReportOut = Join-Path (Split-Path -Parent $outPath) "report.md"
    }
    $reportPath = $ReportOut
    if (-not [System.IO.Path]::IsPathRooted($reportPath)) {
        $reportPath = Join-Path $Root $reportPath
    }
    $reportArgs = @("--input", $outPath, "--out", $reportPath)
    if ($Compare -ne "") {
        $comparePath = $Compare
        if (-not [System.IO.Path]::IsPathRooted($comparePath)) {
            $comparePath = Join-Path $Root $comparePath
        }
        $reportArgs += @("--baseline", (Resolve-Path -LiteralPath $comparePath).Path)
    }
    if ($ProfileInput -ne "") {
        $profileInputPath = $ProfileInput
        if (-not [System.IO.Path]::IsPathRooted($profileInputPath)) {
            $profileInputPath = Join-Path $Root $profileInputPath
        }
        $reportArgs += @("--profile-input", (Resolve-Path -LiteralPath $profileInputPath).Path)
    }
    node $Reporter @reportArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
