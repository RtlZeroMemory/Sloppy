param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$DryRun = -not $Apply
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

& (Join-Path $PSScriptRoot "validate-issue-data.ps1")
if (-not $?) {
    throw "GitHub issue metadata validation failed."
}

$data = Read-JsonFile (Join-Path $PSScriptRoot "issues.json")
$items = @($data.epics) + @($data.tasks)

if ($DryRun) {
    Write-Host "Dry run: issues that would be created for $Repo"
    foreach ($item in $items) { Write-Host "  $($item.title) [$($item.milestone)]" }
    Write-Host "Pass -Apply to create issues."
    return
}

Assert-GhReady -Repo $Repo
$existing = Invoke-GhJson @("issue", "list", "--repo", $Repo, "--state", "open", "--limit", "1000", "--json", "title,number,url")
$byTitle = @{}
foreach ($issue in $existing) { $byTitle[$issue.title] = $issue }

foreach ($item in $items) {
    if ($byTitle.ContainsKey($item.title)) {
        Write-Host "Skipping existing issue $($item.title) -> $($byTitle[$item.title].url)"
        continue
    }
    $bodyPath = Join-Path $root $item.bodyPath
    if (-not (Test-Path -LiteralPath $bodyPath)) { throw "Missing issue body file: $bodyPath" }
    $labelArgs = @()
    foreach ($label in $item.labels) { $labelArgs += @("--label", $label) }
    Write-Host "Creating issue $($item.title)"
    & gh issue create --repo $Repo --title $item.title --body-file $bodyPath --milestone $item.milestone @labelArgs
    if ($LASTEXITCODE -ne 0) { throw "Failed to create issue $($item.title)" }
}
