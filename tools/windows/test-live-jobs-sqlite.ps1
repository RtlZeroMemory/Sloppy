param(
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\$Preset"
$caseRoot = Join-Path $buildDir "jobs-sqlite-live"
$dbPath = Join-Path $caseRoot "jobs-live.db"

function Resolve-SloppyCli {
    if ($SloppyCli.Length -gt 0) {
        return $SloppyCli
    }
    return Join-Path $buildDir "sloppy.exe"
}

function Convert-LastJsonLine {
    param([string[]]$Lines)

    for ($index = $Lines.Count - 1; $index -ge 0; $index -= 1) {
        $line = $Lines[$index]
        if ($line -match '^\s*\{') {
            return $line | ConvertFrom-Json
        }
    }
    throw "FAIL: Sloppy jobs SQLite step did not emit JSON."
}

function Invoke-JobsStep {
    param(
        [string]$Cli,
        [string]$Source,
        [string[]]$StepArgs
    )

    $output = & $Cli run $Source --kind program -- @StepArgs
    if ($LASTEXITCODE -ne 0) {
        $joined = $output -join "`n"
        throw "FAIL: SQLite jobs step '$($StepArgs -join ' ')' failed. Output:`n$joined"
    }
    return Convert-LastJsonLine -Lines $output
}

function Invoke-ConcurrentJobsStep {
    param(
        [string]$Cli,
        [string]$Source,
        [string[][]]$StepArgsList
    )

    $jobs = @()
    foreach ($stepArgs in $StepArgsList) {
        $jobs += Start-Job -ScriptBlock {
            param($CliPath, $SourcePath, [string[]]$RunArgs)
            & $CliPath run $SourcePath --kind program -- @RunArgs
            exit $LASTEXITCODE
        } -ArgumentList $Cli, $Source, $stepArgs
    }

    $results = @()
    foreach ($job in $jobs) {
        Wait-Job $job | Out-Null
        $output = Receive-Job $job
        if ($job.State -ne "Completed") {
            $joined = $output -join "`n"
            Remove-Job $job -Force
            throw "FAIL: SQLite concurrent jobs step failed. Output:`n$joined"
        }
        $results += Convert-LastJsonLine -Lines $output
        Remove-Job $job -Force
    }
    return $results
}

function Assert-JobsSqliteClaimConcurrency {
    param(
        [string]$Cli,
        [string]$RepoRoot,
        [string]$Database
    )

    $source = Join-Path $RepoRoot "examples\jobs-concurrency-step\main.ts"
    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlite", "init", $Database) | Out-Null
    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlite", "enqueue-claims", $Database, "100", "20") | Out-Null
    $claimRuns = @(
        @("sqlite", "claim", $Database, "sqlite-worker-1", "8", "5000"),
        @("sqlite", "claim", $Database, "sqlite-worker-2", "8", "5000"),
        @("sqlite", "claim", $Database, "sqlite-worker-3", "8", "5000")
    )
    $claims = Invoke-ConcurrentJobsStep -Cli $Cli -Source $source -StepArgsList $claimRuns
    $claimedIds = @($claims | ForEach-Object { $_.jobIds } | Where-Object { $_ })
    $uniqueClaimedIds = @($claimedIds | Sort-Object -Unique)
    if ($claimedIds.Count -ne $uniqueClaimedIds.Count) {
        throw "FAIL: SQLite workers claimed the same job more than once."
    }
    if ($claimedIds.Count -eq 0) {
        throw "FAIL: SQLite claim concurrency did not claim any jobs."
    }

    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlite", "enqueue-duplicate", $Database, "sqlite:lease-reclaim", "leases") | Out-Null
    $leased = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlite", "claim", $Database, "sqlite-lease-worker-1", "1", "1", "leases")
    if ($leased.jobIds.Count -ne 1) {
        throw "FAIL: SQLite initial lease claim failed."
    }
    Start-Sleep -Milliseconds 50
    $reclaimed = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlite", "claim", $Database, "sqlite-lease-worker-2", "1", "5000", "leases")
    if ($reclaimed.jobIds.Count -ne 1 -or $reclaimed.jobIds[0] -ne $leased.jobIds[0]) {
        throw "FAIL: SQLite expired processing lease was not reclaimed."
    }

    Write-Host "PASS: SQLite jobs claim concurrency used Sloppy Program Mode processes and data.sqlite."
}

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
}

$cli = Resolve-SloppyCli
if (-not (Test-Path -LiteralPath $cli)) {
    throw "UNAVAILABLE: sloppy CLI was not found at $cli"
}

Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $caseRoot | Out-Null

try {
    & $cli run (Join-Path $repoRoot "examples\jobs-basic\main.ts") --kind program -- $dbPath
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs-basic Sloppy Program Mode smoke failed."
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-recurring\main.ts") --kind program -- $dbPath
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs-recurring Sloppy Program Mode smoke failed."
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-concurrency-sqlite\main.ts") --kind program -- $dbPath
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: jobs-concurrency SQLite Sloppy Program Mode workload failed."
    }

    Assert-JobsSqliteClaimConcurrency -Cli $cli -RepoRoot $repoRoot -Database $dbPath

    Write-Host "PASS: SQLite jobs live lane used Sloppy Program Mode and data.sqlite."
}
finally {
    Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
}
