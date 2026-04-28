param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [switch]$Apply
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$DryRun = -not $Apply
$milestones = Read-JsonFile (Join-Path $PSScriptRoot "milestones.json")

if ($DryRun) {
    Write-Host "Dry run: milestones that would be ensured for $Repo"
    $milestones | ForEach-Object { Write-Host "  $($_.title) - $($_.purpose)" }
    Write-Host "Pass -Apply to create milestones."
    return
}

Assert-GhReady -Repo $Repo
$existing = Invoke-GhJson @("api", "repos/$Repo/milestones", "--paginate")
$byTitle = @{}
foreach ($m in $existing) { $byTitle[$m.title] = $m }

foreach ($m in $milestones) {
    if ($byTitle.ContainsKey($m.title)) {
        Write-Host "Skipping existing milestone $($m.title)"
    } else {
        Write-Host "Creating milestone $($m.title)"
        & gh api "repos/$Repo/milestones" -f title="$($m.title)" -f description="$($m.purpose)" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Failed to create milestone $($m.title)" }
    }
}