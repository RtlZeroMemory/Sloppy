param(
    [ValidateSet("all", "cli", "compiler", "templates", "diagnostics", "alpha-flows", "examples")]
    [string]$Area = "all",

    [string]$RunnerPreset = "windows-relwithdebinfo",

    [string]$TargetPreset = "windows-dev",

    [switch]$Verify,

    [switch]$RequireV8
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Runner = Join-Path $Root "build/$RunnerPreset/sloppy.exe"
$Sloppy = Join-Path $Root "build/$TargetPreset/sloppy.exe"
$ReleaseSloppyc = Join-Path $Root "compiler/target/release/sloppyc.exe"
$DebugSloppyc = Join-Path $Root "compiler/target/debug/sloppyc.exe"
$Sloppyc = if (Test-Path -LiteralPath $ReleaseSloppyc -PathType Leaf) { $ReleaseSloppyc } else { $DebugSloppyc }
$Script = Join-Path $Root "tools/golden/alpha-proof.ts"
$Mode = if ($RequireV8) { "v8" } else { "default" }

function Invoke-AlphaProof {
    param(
        [string]$RunArea,
        [string[]]$Extra = @()
    )

    $suffix = (($RunArea + "-" + ($Extra -join "-")) -replace "[^A-Za-z0-9_.-]", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($suffix)) {
        $suffix = "all"
    }
    $workRoot = Join-Path $Root "artifacts/alpha-proof/update-$PID-$RunnerPreset-$TargetPreset-$Mode-$suffix"
    $arguments = @(
        "run",
        $Script,
        "--",
        "--root", $Root,
        "--sloppy", $Sloppy,
        "--sloppyc", $Sloppyc,
        "--area", $RunArea,
        "--work-root", $workRoot
    )
    $arguments += $Extra
    if (-not $Verify) {
        $arguments += "--update"
    }
    if ($RequireV8) {
        $arguments += "--require-v8"
    }

    & $Runner @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($Area -eq "all" -or $Area -eq "cli") {
    foreach ($section in @("help", "web", "program")) {
        Invoke-AlphaProof "cli" @("--cli-section", $section)
    }
}
if ($Area -eq "all" -or $Area -eq "compiler") {
    foreach ($case in @("hello-mapget", "grouped-route", "http-methods", "framework-metadata", "full-framework-app-graph", "realistic-users-api", "provider-capability", "partial-body-without-schema", "function-module", "source-map")) {
        Invoke-AlphaProof "compiler" @("--compiler-case", $case)
    }
}
if ($Area -eq "all" -or $Area -eq "templates") {
    foreach ($template in @("minimal-api", "full-api", "dogfood", "program")) {
        Invoke-AlphaProof "templates" @("--template", $template)
    }
}
if ($Area -eq "all" -or $Area -eq "diagnostics") {
    Invoke-AlphaProof "diagnostics"
}
if ($Area -eq "all" -or $Area -eq "alpha-flows") {
    foreach ($flow in @("minimal-api", "full-api", "dogfood", "program", "direct-program", "direct-web")) {
        Invoke-AlphaProof "alpha-flows" @("--flow", $flow)
    }
}
if ($Area -eq "all" -or $Area -eq "examples") {
    Invoke-AlphaProof "examples" @("--example", "classification")
    $manifest = Get-Content -LiteralPath (Join-Path $Root "tests/golden/examples/examples.manifest.json") -Raw | ConvertFrom-Json
    foreach ($example in @($manifest.examples | Where-Object { $_.prSmoke -eq $true })) {
        Invoke-AlphaProof "examples" @("--example", $example.name)
    }
}
