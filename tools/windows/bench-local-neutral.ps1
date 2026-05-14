[CmdletBinding()]
param(
    [ValidateSet("auto", "oha", "wrk", "k6", "vegeta")]
    [string]$Tool = "auto",

    [string[]]$Runtime = @("all"),
    [string[]]$Workload = @(),
    [string[]]$Connections = @(),
    [string]$Duration = "",
    [string]$Warmup = "",
    [int]$Repeats = 0,
    [string]$HostName = "127.0.0.1",
    [int]$BasePort = 41000,
    [string]$SloppyExe = "",
    [string]$Out = "",

    [ValidateSet("quick", "realistic-short", "alpha", "full", "stress", "public-candidate")]
    [string]$Preset = "quick",

    [ValidateSet("local", "public-candidate")]
    [string]$ClaimMode = "local",

    [ValidateSet("same-machine", "separate-machine")]
    [string]$LoadHostKind = "same-machine",

    [int]$ResourceIntervalMs = 500,
    [switch]$NoResources,
    [switch]$CheckTools,
    [switch]$Json
)

$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptRoot "..\..")).Path
$runner = Join-Path $repoRoot "benchmarks\local-neutral\scripts\run.mjs"

$nodeArgs = @($runner, "--tool", $Tool, "--preset", $Preset, "--host", $HostName, "--base-port", [string]$BasePort, "--claim-mode", $ClaimMode, "--load-host-kind", $LoadHostKind, "--resource-interval-ms", [string]$ResourceIntervalMs)

if ($Runtime.Count -gt 0) {
    $nodeArgs += @("--runtime", ($Runtime -join ","))
}
if ($Workload.Count -gt 0) {
    $nodeArgs += @("--workload", ($Workload -join ","))
}
if ($Connections.Count -gt 0) {
    $nodeArgs += @("--connections", ($Connections -join ","))
}
if (-not [string]::IsNullOrWhiteSpace($Duration)) {
    $nodeArgs += @("--duration", $Duration)
}
if (-not [string]::IsNullOrWhiteSpace($Warmup)) {
    $nodeArgs += @("--warmup", $Warmup)
}
if ($Repeats -gt 0) {
    $nodeArgs += @("--repeats", [string]$Repeats)
}
if (-not [string]::IsNullOrWhiteSpace($SloppyExe)) {
    $sloppyExePath = if ([System.IO.Path]::IsPathRooted($SloppyExe)) { $SloppyExe } else { Join-Path $repoRoot $SloppyExe }
    $nodeArgs += @("--sloppy-bin", (Resolve-Path -LiteralPath $sloppyExePath).Path)
}
if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $repoRoot $Out }
    $nodeArgs += @("--out", $outPath)
}
if ($NoResources) {
    $nodeArgs += "--no-resources"
}
if ($CheckTools) {
    $nodeArgs += "--check-tools"
}
if ($Json) {
    $nodeArgs += "--json"
}

Push-Location $repoRoot
try {
    & node @nodeArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
