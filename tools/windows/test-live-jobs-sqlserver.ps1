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

function Assert-DockerAvailable {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "UNAVAILABLE: Docker CLI is required for the SQL Server jobs live lane."
    }
    docker info | Out-Null
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
    }

    $cli = Resolve-SloppyCli
    if (-not (Test-Path -LiteralPath $cli)) {
        throw "UNAVAILABLE: sloppy CLI was not found at $cli"
    }

    & $cli run (Join-Path $repoRoot "examples\jobs-sqlserver-worker\main.ts")
    if ($LASTEXITCODE -ne 0) {
        throw "FAIL: SQL Server jobs Sloppy Program Mode smoke failed."
    }

    Write-Host "PASS: SQL Server jobs live lane used Sloppy Program Mode and data.sqlserver."
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
    }
}
