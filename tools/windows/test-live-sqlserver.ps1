param(
    [string]$Preset = "windows-relwithdebinfo",
    [switch]$NoDocker
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$composeFile = Join-Path $repoRoot "tests\live\sqlserver\compose.yml"
$container = "sloppy-sqlserver-live"
$password = "Sloppy_Strong_Passw0rd!"
$driver = "ODBC Driver 18 for SQL Server"
if (-not (Get-OdbcDriver -Name $driver -ErrorAction SilentlyContinue)) {
    $driver = "ODBC Driver 17 for SQL Server"
}
if (-not (Get-OdbcDriver -Name $driver -ErrorAction SilentlyContinue)) {
    throw "UNAVAILABLE: Microsoft ODBC Driver 18 or 17 for SQL Server is required for the SQL Server integration checks."
}
if ($driver -eq "ODBC Driver 17 for SQL Server" -and [string]::IsNullOrWhiteSpace($env:SLOPPY_SQLSERVER_IMAGE)) {
    $env:SLOPPY_SQLSERVER_IMAGE = "mcr.microsoft.com/mssql/server:2019-latest"
}
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING = "Driver={$driver};Server=tcp:127.0.0.1,51433;Database=sloppy_test;UID=sa;PWD=$password;Encrypt=yes;TrustServerCertificate=yes;"
$env:Sloppy__Providers__sqlserver__main__connectionString = $env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING

function Assert-DockerAvailable {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "UNAVAILABLE: Docker CLI is required for the SQL Server integration checks."
    }
    docker info | Out-Null
}

function New-SqlServerConnectionString {
    param([string]$Database)

    return "Driver={$driver};Server=tcp:127.0.0.1,51433;Database=$Database;UID=sa;PWD=$password;Encrypt=yes;TrustServerCertificate=yes;"
}

function Invoke-SqlServerHostQuery {
    param(
        [string]$Database,
        [string]$Query
    )

    $connection = [System.Data.Odbc.OdbcConnection]::new((New-SqlServerConnectionString -Database $Database))
    try {
        $connection.Open()
        $command = $connection.CreateCommand()
        try {
            $command.CommandText = $Query
            [void]$command.ExecuteNonQuery()
        }
        finally {
            $command.Dispose()
        }
    }
    finally {
        $connection.Dispose()
    }
}

function Wait-SqlServerHostConnection {
    param([string]$Database = "sloppy_test")

    for ($attempt = 0; $attempt -lt 30; $attempt += 1) {
        $connection = $null
        try {
            $connection = [System.Data.Odbc.OdbcConnection]::new((New-SqlServerConnectionString -Database $Database))
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

try {
    if (-not $NoDocker) {
        Assert-DockerAvailable
        docker compose -f $composeFile up -d

        Wait-SqlServerHostConnection -Database "master"
        Invoke-SqlServerHostQuery -Database "master" -Query "if db_id(N'sloppy_test') is null create database sloppy_test"
        Wait-SqlServerHostConnection -Database "sloppy_test"
    }

    & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
    ctest --test-dir (Join-Path $repoRoot "build\$Preset") --output-on-failure -R "data\.sqlserver\.live_provider|conformance\.sqlserver\.(native_live|bridge_live)"
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
    }
}
