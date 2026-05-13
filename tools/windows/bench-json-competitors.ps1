param(
    [int]$Iterations = 100,
    [int]$Warmup = 10,
    [int]$Repeat = 1,
    [string]$SloppyBin = "",
    [switch]$HttpProfile,
    [string]$Out = "artifacts/bench/json-competitors.json"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Runner = Join-Path $Root "benchmarks/competitors/json-local.mjs"
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
node $Runner @args
