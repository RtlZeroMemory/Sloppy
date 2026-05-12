param(
    [int]$Iterations = 100,
    [int]$Warmup = 10,
    [int]$Repeat = 1,
    [string]$SloppyBin = "",
    [string]$Out = "artifacts/bench/json-competitors.json"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Runner = Join-Path $Root "benchmarks/competitors/json-local.mjs"
$outPath = Join-Path $Root $Out

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$args = @("--iterations", $Iterations, "--warmup", $Warmup, "--repeat", $Repeat, "--out", $outPath)
if ($SloppyBin -ne "") {
    $args += @("--sloppy-bin", $SloppyBin)
}
node $Runner @args
