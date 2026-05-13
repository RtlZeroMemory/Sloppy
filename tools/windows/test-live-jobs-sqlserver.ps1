param(
    [string]$Preset = "windows-relwithdebinfo",
    [string]$SloppyCli = "",
    [switch]$NoDocker,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$buildDir = Join-Path $repoRoot "build\$Preset"
$composeFile = Join-Path $repoRoot "tests\live\sqlserver\compose.yml"
$initSql = Join-Path $repoRoot "tests\live\sqlserver\init.sql"
$container = "sloppy-sqlserver-live"
$password = "Sloppy_Strong_Passw0rd!"
$driver = "ODBC Driver 18 for SQL Server"
if (-not (Get-OdbcDriver -Name $driver -ErrorAction SilentlyContinue)) {
    $driver = "ODBC Driver 17 for SQL Server"
}
if (-not (Get-OdbcDriver -Name $driver -ErrorAction SilentlyContinue)) {
    throw "UNAVAILABLE: Microsoft ODBC Driver 18 or 17 for SQL Server is required for the SQL Server jobs live lane."
}
$env:SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING = "Driver={$driver};Server=tcp:127.0.0.1,51433;Database=sloppy_test;UID=sa;PWD=$password;Encrypt=yes;TrustServerCertificate=yes;"
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING = $env:SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING
$env:Sloppy__Providers__sqlserver__main__connectionString = $env:SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING

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
    throw "FAIL: Sloppy jobs concurrency step did not emit JSON."
}

function Invoke-JobsStep {
    param(
        [string]$Cli,
        [string]$Source,
        [string[]]$StepArgs,
        [int]$ExpectedExitCode = 0
    )

    $output = & $Cli run $Source --kind program -- @StepArgs
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne $ExpectedExitCode) {
        $joined = $output -join "`n"
        throw "FAIL: jobs concurrency step '$($StepArgs -join ' ')' exited $exitCode, expected $ExpectedExitCode. Output:`n$joined"
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
        $exitCode = $job.ChildJobs[0].JobStateInfo.State
        $nativeExitCode = $job.ChildJobs[0].JobStateInfo.Reason
        if ($job.State -ne "Completed") {
            $joined = $output -join "`n"
            Remove-Job $job -Force
            throw "FAIL: concurrent jobs step failed. Output:`n$joined`nReason: $nativeExitCode"
        }
        $results += Convert-LastJsonLine -Lines $output
        Remove-Job $job -Force
        $null = $exitCode
    }
    return $results
}

function Assert-JobsSqlServerConcurrency {
    param(
        [string]$Cli,
        [string]$RepoRoot
    )

    $source = Join-Path $RepoRoot "examples\jobs-concurrency-step\main.ts"
    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "init") | Out-Null

    $duplicateRuns = @()
    for ($index = 0; $index -lt 4; $index += 1) {
        $duplicateRuns += ,@("sqlserver", "enqueue-duplicate", "sqlserver:duplicate")
    }
    $duplicates = Invoke-ConcurrentJobsStep -Cli $Cli -Source $source -StepArgsList $duplicateRuns
    $duplicateIds = @($duplicates | ForEach-Object { $_.jobId } | Sort-Object -Unique)
    if ($duplicateIds.Count -ne 1) {
        throw "FAIL: SQL Server duplicate enqueue returned more than one durable job."
    }

    $lockRuns = @(
        @("sqlserver", "acquire-lock", "sqlserver:single-owner", "owner-a", "1000"),
        @("sqlserver", "acquire-lock", "sqlserver:single-owner", "owner-b", "1000"),
        @("sqlserver", "acquire-lock", "sqlserver:single-owner", "owner-c", "1000")
    )
    $locks = Invoke-ConcurrentJobsStep -Cli $Cli -Source $source -StepArgsList $lockRuns
    $acceptedLocks = @($locks | Where-Object { $_.acquired -eq $true })
    if ($acceptedLocks.Count -ne 1) {
        throw "FAIL: SQL Server lock acquire allowed $($acceptedLocks.Count) owners."
    }

    $expired = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "acquire-lock", "sqlserver:expired", "owner-old", "1")
    if ($expired.acquired -ne $true) {
        throw "FAIL: SQL Server expired lock fixture was not acquired."
    }
    Start-Sleep -Milliseconds 50
    $takeover = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "acquire-lock", "sqlserver:expired", "owner-new", "1000")
    if ($takeover.acquired -ne $true) {
        throw "FAIL: SQL Server expired lock was not taken over."
    }
    $releaseConflict = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "release-lock", "sqlserver:expired", "owner-not-current") -ExpectedExitCode 2
    if ($releaseConflict.code -ne "SLOPPY_E_JOBS_LOCK_CONFLICT") {
        throw "FAIL: SQL Server non-owner lock release did not fail with lock conflict."
    }

    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "enqueue-claims", "0", "20") | Out-Null
    $claimRuns = @(
        @("sqlserver", "claim", "sqlserver-worker-1", "8", "5000"),
        @("sqlserver", "claim", "sqlserver-worker-2", "8", "5000"),
        @("sqlserver", "claim", "sqlserver-worker-3", "8", "5000")
    )
    $claims = Invoke-ConcurrentJobsStep -Cli $Cli -Source $source -StepArgsList $claimRuns
    $claimedIds = @($claims | ForEach-Object { $_.jobIds } | Where-Object { $_ })
    $uniqueClaimedIds = @($claimedIds | Sort-Object -Unique)
    if ($claimedIds.Count -ne $uniqueClaimedIds.Count) {
        throw "FAIL: SQL Server workers claimed the same job more than once."
    }
    if ($claimedIds.Count -eq 0) {
        throw "FAIL: SQL Server claim concurrency did not claim any jobs."
    }

    Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "enqueue-duplicate", "sqlserver:lease-reclaim", "leases") | Out-Null
    $leased = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "claim", "sqlserver-lease-worker-1", "1", "1", "leases")
    if ($leased.jobIds.Count -ne 1) {
        throw "FAIL: SQL Server initial lease claim failed."
    }
    Start-Sleep -Milliseconds 50
    $reclaimed = Invoke-JobsStep -Cli $Cli -Source $source -StepArgs @("sqlserver", "claim", "sqlserver-lease-worker-2", "1", "5000", "leases")
    if ($reclaimed.jobIds.Count -ne 1 -or $reclaimed.jobIds[0] -ne $leased.jobIds[0]) {
        throw "FAIL: SQL Server expired processing lease was not reclaimed."
    }

    Write-Host "PASS: SQL Server jobs concurrency used Sloppy Program Mode processes and data.sqlserver."
}

function Assert-DockerAvailable {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "UNAVAILABLE: Docker CLI is required for the SQL Server jobs live lane."
    }
    docker info | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "UNAVAILABLE: Docker daemon is not reachable."
    }
}

function Invoke-SqlServerContainerQuery {
    param(
        [string]$Query,
        [string]$InputFile = ""
    )

    $script = @'
set -e
if [ -x /opt/mssql-tools18/bin/sqlcmd ]; then
  exec /opt/mssql-tools18/bin/sqlcmd "$@"
fi
exec /opt/mssql-tools/bin/sqlcmd "$@"
'@

    if ($InputFile.Length -gt 0) {
        docker cp $InputFile "$($container):/tmp/sloppy-live-init.sql"
        if ($LASTEXITCODE -ne 0) {
            throw "FAIL: SQL Server live init copy failed."
        }
        docker exec $container /bin/bash -lc $script -- -S localhost -U sa -P $password -C -b -i /tmp/sloppy-live-init.sql | Out-Null
    }
    else {
        docker exec $container /bin/bash -lc $script -- -S localhost -U sa -P $password -C -b -Q $Query | Out-Null
    }
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: SQL Server live query failed."
    }
}

function Wait-SqlServerHostConnection {
    for ($attempt = 0; $attempt -lt 30; $attempt += 1) {
        $connection = $null
        try {
            $connection = [System.Data.Odbc.OdbcConnection]::new($env:SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING)
            $connection.Open()
            return
        }
        catch {
            Start-Sleep -Seconds 1
        }
        finally {
            if ($null -ne $connection) {
                $connection.Dispose()
            }
        }
    }

    throw "UNAVAILABLE: SQL Server host ODBC connection did not become ready before timeout."
}

if (-not $NoDocker) {
    Assert-DockerAvailable
    docker compose -f $composeFile up -d
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: SQL Server docker compose up failed."
    }

    $ready = $false
    for ($attempt = 0; $attempt -lt 90; $attempt += 1) {
        try {
            Invoke-SqlServerContainerQuery -Query "select 1"
            $ready = $true
            break
        }
        catch {
            Start-Sleep -Seconds 2
        }
    }

    if (-not $ready) {
        throw "UNAVAILABLE: SQL Server container did not become ready before timeout."
    }

    Invoke-SqlServerContainerQuery -Query "" -InputFile $initSql
    Wait-SqlServerHostConnection
}

try {
    if (-not $NoBuild) {
        & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "FAIL: build step failed for preset $Preset."
        }
    }

    $cli = Resolve-SloppyCli
    if (-not (Test-Path -LiteralPath $cli)) {
        throw "UNAVAILABLE: sloppy CLI was not found at $cli"
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-sqlserver-worker\main.ts") --kind program
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: SQL Server jobs Sloppy Program Mode smoke failed."
    }

    Assert-JobsSqlServerConcurrency -Cli $cli -RepoRoot $repoRoot

    Write-Host "PASS: SQL Server jobs live lane used Sloppy Program Mode and data.sqlserver."
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "FAIL: SQL Server docker compose down failed."
        }
    }
}
