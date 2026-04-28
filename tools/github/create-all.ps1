param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [switch]$Apply,
    [switch]$ForceLabels
)

$ErrorActionPreference = "Stop"
$applyArgs = @()
if ($Apply) { $applyArgs += "-Apply" }
$forceArgs = @()
if ($ForceLabels) { $forceArgs += "-Force" }

& (Join-Path $PSScriptRoot "validate-issue-data.ps1")
if (-not $?) {
    throw "GitHub issue metadata validation failed. No GitHub data was changed."
}

& (Join-Path $PSScriptRoot "create-labels.ps1") -Repo $Repo @applyArgs @forceArgs
& (Join-Path $PSScriptRoot "create-milestones.ps1") -Repo $Repo @applyArgs
& (Join-Path $PSScriptRoot "create-issues.ps1") -Repo $Repo @applyArgs

if (-not $Apply) { Write-Host "Dry run complete. Pass -Apply to mutate GitHub." }
