param(
    [string]$Suite = "smoke",

    [string[]]$Size = @(),

    [string]$Out,

    [string[]]$Compare = @(),

    [string]$SloppycExe,

    [ValidateSet("debug", "release")]
    [string]$CompilerProfile = "debug",

    [double]$MaxWorkingSetMB = 0
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Node = Get-Command "node" -ErrorAction SilentlyContinue
if ($null -eq $Node) {
    throw "node was not found; compiler benchmark generation requires Node.js."
}

$Script = Join-Path $Root "tools/compiler/bench-compiler.mjs"
$NodeArgs = @($Script)

if ($Compare.Count -eq 1 -and $Suite -notin @("smoke", "scale")) {
    $Compare = @($Compare[0], $Suite)
    $Suite = "smoke"
}

if ($Compare.Count -gt 0) {
    if ($Compare.Count -ne 2) {
        throw "-Compare expects exactly two paths: before and after."
    }
    $NodeArgs += @("--compare", $Compare[0], $Compare[1])
} else {
    if ($Suite -notin @("smoke", "scale")) {
        throw "-Suite must be 'smoke' or 'scale'."
    }
    $NodeArgs += @("--suite", $Suite)
    if ($Size.Count -gt 0) {
        $NodeArgs += @("--size", ($Size -join ","))
    }
}

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $NodeArgs += @("--out", $Out)
}
if (-not [string]::IsNullOrWhiteSpace($SloppycExe)) {
    if (-not (Test-Path -LiteralPath $SloppycExe -PathType Leaf)) {
        throw "-SloppycExe does not exist: $SloppycExe"
    }
    $NodeArgs += @("--sloppyc", $SloppycExe)
}
if ($Compare.Count -eq 0) {
    $NodeArgs += @("--compiler-profile", $CompilerProfile)
    if ($MaxWorkingSetMB -gt 0) {
        $NodeArgs += @("--max-working-set-mb", ([string]$MaxWorkingSetMB))
    }
}

$NeedsBuildEnv =
    $Compare.Count -eq 0 -and
    [string]::IsNullOrWhiteSpace($SloppycExe) -and
    [string]::IsNullOrWhiteSpace($env:SLOPPYC_EXE) -and
    [string]::IsNullOrWhiteSpace($env:SLOPPYC)

if ($NeedsBuildEnv) {
    . (Join-Path $PSScriptRoot "msvc-env.ps1")
    Import-SlVisualStudioEnvironment
}

& $Node.Source @NodeArgs
if ($LASTEXITCODE -ne 0) {
    throw "compiler benchmark harness failed with exit code $LASTEXITCODE"
}
