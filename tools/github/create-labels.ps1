param(
    [string]$Repo = "RtlZeroMemory/Slop",
    [switch]$Apply,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$DryRun = -not $Apply
$labels = Read-JsonFile (Join-Path $PSScriptRoot "labels.json")

if ($DryRun) {
    Write-Host "Dry run: labels that would be ensured for $Repo"
    $labels | ForEach-Object { Write-Host "  $($_.name) #$($_.color) - $($_.description)" }
    Write-Host "Pass -Apply to create/update labels."
    return
}

Assert-GhReady -Repo $Repo
$existing = Invoke-GhJson @("label", "list", "--repo", $Repo, "--limit", "1000", "--json", "name,color,description")
$byName = @{}
foreach ($label in $existing) { $byName[$label.name] = $label }

foreach ($label in $labels) {
    if ($byName.ContainsKey($label.name)) {
        if ($Force) {
            Write-Host "Updating label $($label.name)"
            & gh label edit $label.name --repo $Repo --color $label.color --description $label.description
            if ($LASTEXITCODE -ne 0) { throw "Failed to update label $($label.name)" }
        } else {
            Write-Host "Skipping existing label $($label.name)"
        }
    } else {
        Write-Host "Creating label $($label.name)"
        & gh label create $label.name --repo $Repo --color $label.color --description $label.description
        if ($LASTEXITCODE -ne 0) { throw "Failed to create label $($label.name)" }
    }
}