param(
    [int]$Iterations = 100,
    [string]$Out = "artifacts/bench/json-competitors.json"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Runner = Join-Path $Root "benchmarks/competitors/json-local.mjs"
$outPath = Join-Path $Root $Out

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
node $Runner --iterations $Iterations --out $outPath
