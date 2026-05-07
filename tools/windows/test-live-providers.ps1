param(
    [ValidateSet("postgres", "sqlserver", "all")]
    [string]$Provider = "all",
    [string]$Preset = "windows-relwithdebinfo",
    [switch]$NoDocker
)

$ErrorActionPreference = "Stop"

$providers = if ($Provider -eq "all") { @("postgres", "sqlserver") } else { @($Provider) }

foreach ($selected in $providers) {
    Write-Host "== Sloppy live-provider lane: $selected =="
    switch ($selected) {
        "postgres" {
            & (Join-Path $PSScriptRoot "test-live-postgres.ps1") -Preset $Preset -NoDocker:$NoDocker
        }
        "sqlserver" {
            & (Join-Path $PSScriptRoot "test-live-sqlserver.ps1") -Preset $Preset -NoDocker:$NoDocker
        }
    }
}
