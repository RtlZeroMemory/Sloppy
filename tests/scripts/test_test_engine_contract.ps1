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

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments
    )

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $File @Arguments 2>&1
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

$invalidArea = Invoke-Script @("-File", $testEngine, "-Area", "invalid")
Assert-True ($invalidArea.ExitCode -ne 0) "invalid area unexpectedly succeeded"

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

$v8ReportPath = Join-Path $RepoRoot "artifacts/test-engine/meta-contract-v8-unavailable.json"
$missingV8Root = Join-Path $RepoRoot "artifacts/test-engine/missing-v8-sdk"
$v8Command = "& { `$env:SLOPPY_V8_ROOT = '$missingV8Root'; & '$testEngine' -Area v8 -Tier pr -Out '$v8ReportPath' }"
$v8 = Invoke-Script @("-Command", $v8Command)
Assert-True ($v8.ExitCode -eq 0) "unavailable V8 lane failed the command: $($v8.Output)"
Assert-True (Test-Path -LiteralPath $v8ReportPath -PathType Leaf) "unavailable V8 report was not written"
$v8Report = Get-Content -LiteralPath $v8ReportPath -Raw | ConvertFrom-Json
Assert-True ($v8Report.lanes.Count -eq 1) "unexpected V8 report lane count"
Assert-True ($v8Report.lanes[0].status -eq "unavailable") "V8 gate did not report unavailable"

$extendedWorkflow = Get-Content -LiteralPath (Join-Path $RepoRoot ".github/workflows/test-engine-extended.yml") -Raw
$tortureWorkflow = Get-Content -LiteralPath (Join-Path $RepoRoot ".github/workflows/test-engine-torture.yml") -Raw
Assert-True ($extendedWorkflow -match "workflow_dispatch:") "extended workflow is not manually runnable"
Assert-True ($extendedWorkflow -match "schedule:") "extended workflow is not scheduled"
Assert-True ($tortureWorkflow -match "workflow_dispatch:") "torture workflow is not manually runnable"
Assert-True ($tortureWorkflow -notmatch "(?m)^\s+schedule:") "torture workflow must not be scheduled"

$windowsEngineText = Get-Content -LiteralPath $testEngine -Raw
Assert-True ($windowsEngineText -match '\$Area -eq "package".*\$Tier -ne "pr"') "package lane guard no longer excludes PR all-tier"
Assert-True ($windowsEngineText -match '\$Area -eq "sanitizer".*\$Tier -ne "pr"') "sanitizer lane guard no longer excludes PR all-tier"
Assert-True ($windowsEngineText -match '\$Area -eq "provider".*\$Tier -ne "pr"') "provider lane guard no longer excludes PR all-tier"

$bashCommands = @(Get-Command bash -CommandType Application -ErrorAction SilentlyContinue)
$bash = $null
foreach ($candidate in $bashCommands) {
    $probe = Invoke-Native $candidate.Source @("--version")
    if ($probe.ExitCode -eq 0) {
        $bash = $candidate
        break
    }
}
if ($null -ne $bash) {
    $unixReportPath = Join-Path $RepoRoot "artifacts/test-engine/meta-contract-unix.json"
    Push-Location -LiteralPath $RepoRoot
    try {
        $unixHelp = Invoke-Native $bash.Source @("tools/unix/test-engine.sh", "--help")
        Assert-True ($unixHelp.ExitCode -eq 0) "unix test-engine help failed"
        Assert-True ($unixHelp.Output -match "Usage: tools/unix/test-engine.sh") "unix test-engine help did not print usage"

        $unixMeta = Invoke-Native $bash.Source @(
            "tools/unix/test-engine.sh",
            "--area",
            "meta",
            "--tier",
            "pr",
            "--out",
            "artifacts/test-engine/meta-contract-unix.json"
        )
        Assert-True ($unixMeta.ExitCode -eq 0) "unix test-engine meta failed: $($unixMeta.Output)"
    } finally {
        Pop-Location
    }

    Assert-True (Test-Path -LiteralPath $unixReportPath -PathType Leaf) "unix test-engine report was not written"
    $unixReport = Get-Content -LiteralPath $unixReportPath -Raw | ConvertFrom-Json
    Assert-True ($unixReport.schemaVersion -eq 1) "unexpected unix report schema version"
    Assert-True ($unixReport.tier -eq "pr") "unexpected unix report tier"
    Assert-True ($unixReport.area -eq "meta") "unexpected unix report area"
    foreach ($lane in $unixReport.lanes) {
        Assert-True ($allowed -contains $lane.status) "unix lane '$($lane.id)' used invalid status '$($lane.status)'"
    }
}
