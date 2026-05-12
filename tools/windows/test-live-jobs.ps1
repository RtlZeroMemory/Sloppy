param(
    [ValidateSet("sqlite", "postgres", "sqlserver", "all")]
    [string]$Provider = "all",
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [switch]$NoDocker,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$providers = if ($Provider -eq "all") { @("sqlite", "postgres", "sqlserver") } else { @($Provider) }

foreach ($selected in $providers) {
    Write-Host "== Sloppy jobs live-provider lane: $selected =="
    switch ($selected) {
        "sqlite" {
            & (Join-Path $PSScriptRoot "test-live-jobs-sqlite.ps1") -Preset $Preset -SloppyCli $SloppyCli -NoBuild:$NoBuild
        }
        "postgres" {
            & (Join-Path $PSScriptRoot "test-live-jobs-postgres.ps1") -Preset $Preset -SloppyCli $SloppyCli -NoDocker:$NoDocker -NoBuild:$NoBuild
        }
        "sqlserver" {
            & (Join-Path $PSScriptRoot "test-live-jobs-sqlserver.ps1") -Preset $Preset -SloppyCli $SloppyCli -NoDocker:$NoDocker -NoBuild:$NoBuild
        }
    }
}
