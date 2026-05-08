param(
    [string]$V8Root = "",
    [string[]]$SearchRoot = @(),
    [ValidateSet("OFF", "AUTO", "REQUIRED")]
    [string]$Mode = "REQUIRED",
    [switch]$Fetch,
    [switch]$ForceFetch,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$fetchScript = Join-Path $PSScriptRoot "fetch-v8.ps1"

try {
    $resolution = Resolve-SlV8SdkRootForMode -RepoRoot $Root -V8Root $V8Root -SearchRoots $SearchRoot -Mode $Mode
} catch {
    if (-not $Fetch -or $Mode -eq "OFF") {
        Write-Host "No compatible Sloppy V8 SDK was resolved."
        Write-Host ([string]$_)
        exit 1
    }

    $resolution = $null
}

if ($Fetch -and $Mode -ne "OFF" -and
    ($ForceFetch -or $null -eq $resolution -or [string]::IsNullOrWhiteSpace([string]$resolution.Root))) {
    $fetchArgs = @(
        "-File",
        $fetchScript,
        "-Destination",
        (Join-Path $Root ".sdeps/v8")
    )
    if ($ForceFetch) {
        $fetchArgs += "-Force"
    }

    & powershell -NoProfile -ExecutionPolicy Bypass @fetchArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    try {
        $resolution = Resolve-SlV8SdkRootForMode -RepoRoot $Root -V8Root $V8Root -SearchRoots $SearchRoot -Mode $Mode
    } catch {
        Write-Host "No compatible Sloppy V8 SDK was resolved after fetch."
        Write-Host ([string]$_)
        exit 1
    }
}
$resolved = $resolution.Root

if ($Quiet) {
    Write-Output $resolved
    if ($Mode -eq "REQUIRED" -and [string]::IsNullOrWhiteSpace($resolved)) {
        exit 1
    }
    exit 0
}

if ($Mode -eq "OFF") {
    Write-Host "V8 SDK resolution is disabled because -Mode OFF was selected."
    exit 0
}

if ([string]::IsNullOrWhiteSpace($resolved)) {
    Write-Host "No compatible Sloppy V8 SDK was resolved."
    Write-Host $resolution.Detail
    if ($Mode -eq "REQUIRED") {
        exit 1
    }
    exit 0
}

Write-Host "Resolved Sloppy V8 SDK root:"
Write-Host "  $resolved"
Write-Host ""
Test-SlV8SdkLayout -Root $resolved | Out-Null
Write-Host ""
Write-Host "Use it explicitly:"
Write-Host "  .\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8 -V8Root `"$resolved`""
Write-Host ""
Write-Host "Or rely on automatic discovery from any worktree:"
Write-Host "  .\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8"
