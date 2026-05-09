param(
    [string]$Suite = "smoke",

    [string[]]$Size = @(),

    [string]$Out,

    [string[]]$Compare = @(),

    [string]$SloppycExe
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")
Import-SlVisualStudioEnvironment

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Node = Get-Command "node" -ErrorAction SilentlyContinue
if ($null -eq $Node) {
    throw "node was not found; compiler benchmark generation requires Node.js."
}

$Script = Join-Path $Root "tools/compiler/bench-compiler.mjs"
$Args = @($Script)

if ($Compare.Count -eq 1 -and $Suite -notin @("smoke", "scale")) {
    $Compare = @($Compare[0], $Suite)
    $Suite = "smoke"
}

if ($Compare.Count -gt 0) {
    if ($Compare.Count -ne 2) {
        throw "-Compare expects exactly two paths: before and after."
    }
    $Args += @("--compare", $Compare[0], $Compare[1])
} else {
    if ($Suite -notin @("smoke", "scale")) {
        throw "-Suite must be 'smoke' or 'scale'."
    }
    $Args += @("--suite", $Suite)
    if ($Size.Count -gt 0) {
        $Args += @("--size", ($Size -join ","))
    }
}

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $Args += @("--out", $Out)
}
if (-not [string]::IsNullOrWhiteSpace($SloppycExe)) {
    $Args += @("--sloppyc", $SloppycExe)
}

& $Node.Source @Args
if ($LASTEXITCODE -ne 0) {
    throw "compiler benchmark harness failed with exit code $LASTEXITCODE"
}
