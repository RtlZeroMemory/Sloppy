param(
    [string]$Preset = "windows-relwithdebinfo",
    [switch]$NoDocker
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$composeFile = Join-Path $repoRoot "tests\live\sqlserver\compose.yml"
$initSql = Join-Path $repoRoot "tests\live\sqlserver\init.sql"
$container = "sloppy-sqlserver-live"
$password = "Sloppy_Strong_Passw0rd!"
$driver = "ODBC Driver 18 for SQL Server"
if (-not (Get-OdbcDriver -Name $driver -ErrorAction SilentlyContinue)) {
    $driver = "ODBC Driver 17 for SQL Server"
}
$env:SLOPPY_SQLSERVER_TEST_CONNECTION_STRING = "Driver={$driver};Server=tcp:127.0.0.1,51433;Database=sloppy_test;UID=sa;PWD=$password;Encrypt=yes;TrustServerCertificate=yes;"

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
        Get-Content -Raw $InputFile | docker exec -i $container /bin/bash -lc $script -- -S localhost -U sa -P $password -C
    }
    else {
        docker exec $container /bin/bash -lc $script -- -S localhost -U sa -P $password -C -Q $Query | Out-Null
    }
}

if (-not $NoDocker) {
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
        throw "SQL Server container did not become ready before timeout."
    }

    Invoke-SqlServerContainerQuery -Query "" -InputFile $initSql
}

try {
    & (Join-Path $PSScriptRoot "dev.ps1") build -Preset $Preset
    ctest --test-dir (Join-Path $repoRoot "build\$Preset") --output-on-failure -R "data\.sqlserver\.live_provider|conformance\.sqlserver\.bridge_live"
}
finally {
    if (-not $NoDocker) {
        docker compose -f $composeFile down -v
    }
}
