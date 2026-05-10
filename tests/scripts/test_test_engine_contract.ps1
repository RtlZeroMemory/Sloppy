param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"

function Get-PowerShellExecutable {
    $command = Get-Command pwsh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        $command = Get-Command powershell -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($null -eq $command) {
        throw "PowerShell executable not found"
    }
    return $command.Source
}

function Invoke-Script {
    param([string[]]$Arguments)

    $powershell = Get-PowerShellExecutable
    $baseArgs = @("-NoProfile")
    if ($powershell -match "powershell(\.exe)?$") {
        $baseArgs += @("-ExecutionPolicy", "Bypass")
    }
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $powershell @baseArgs @Arguments 2>&1
        $exitCode = [int]$LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return [ordered]@{
        ExitCode = $exitCode
        Output = ($output | Out-String)
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$testEngine = Join-Path $RepoRoot "tools/windows/test-engine.ps1"
$reportPath = Join-Path $RepoRoot "artifacts/test-engine/meta-contract.json"

$help = Invoke-Script @("-File", $testEngine, "-Help")
Assert-True ($help.ExitCode -eq 0) "test-engine help failed"
Assert-True ($help.Output -match "Usage: tools/windows/test-engine.ps1") "test-engine help did not print usage"

$invalid = Invoke-Script @("-File", $testEngine, "-Tier", "invalid")
Assert-True ($invalid.ExitCode -ne 0) "invalid tier unexpectedly succeeded"

$meta = Invoke-Script @("-File", $testEngine, "-Area", "meta", "-Tier", "pr", "-Out", $reportPath)
Assert-True ($meta.ExitCode -eq 0) "test-engine meta failed: $($meta.Output)"
Assert-True (Test-Path -LiteralPath $reportPath -PathType Leaf) "test-engine report was not written"

$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
Assert-True ($report.schemaVersion -eq 1) "unexpected report schema version"
Assert-True ($report.tier -eq "pr") "unexpected report tier"
Assert-True ($report.area -eq "meta") "unexpected report area"
Assert-True ($report.lanes.Count -ge 2) "meta report did not record help lanes"

$allowed = @("pass", "fail", "skipped", "unavailable")
foreach ($lane in $report.lanes) {
    Assert-True ($allowed -contains $lane.status) "lane '$($lane.id)' used invalid status '$($lane.status)'"
}
