param(
    [string]$V8Root = "",
    [string[]]$SearchRoot = @(),
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$resolved = Resolve-SlV8SdkRoot -RepoRoot $Root -V8Root $V8Root -SearchRoots $SearchRoot -Require

if ($Quiet) {
    Write-Output $resolved
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
