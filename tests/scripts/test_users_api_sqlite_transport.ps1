param(
    [Parameter(Mandatory = $true)][string]$ProjectSourceDir,
    [Parameter(Mandatory = $true)][string]$CMakeBinaryDir,
    [Parameter(Mandatory = $true)][string]$CargoExecutable,
    [Parameter(Mandatory = $true)][string]$SloppyCli
)

$ErrorActionPreference = "Stop"

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
    $listener.Start()
    try {
        return $listener.LocalEndpoint.Port
    }
    finally {
        $listener.Stop()
    }
}

function Start-SloppyServer {
    param(
        [string]$Artifacts,
        [int]$Port,
        [string]$StdoutPath,
        [string]$StderrPath
    )

    New-Item -ItemType File -Path $StdoutPath -Force | Out-Null
    New-Item -ItemType File -Path $StderrPath -Force | Out-Null
    $process = Start-Process -FilePath $SloppyCli `
        -ArgumentList @("run", "--artifacts", $Artifacts, "--host", "127.0.0.1", "--port", "$Port") `
        -WorkingDirectory $ProjectSourceDir `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -PassThru

    for ($attempt = 0; $attempt -lt 40; $attempt += 1) {
        if ($process.HasExited) {
            throw "sloppy server exited before accepting connections: $(Get-Content -Raw -Path $StderrPath)"
        }
        Start-Sleep -Milliseconds 50
    }

    return [pscustomobject]@{
        Process = $process
    }
}

function Stop-SloppyServer {
    param(
        [pscustomobject]$Server,
        [string]$StdoutPath,
        [string]$StderrPath
    )

    if ($null -eq $Server -or $null -eq $Server.Process) {
        return
    }

    if (-not $Server.Process.HasExited) {
        $Server.Process.Kill()
    }
    $Server.Process.WaitForExit()
    [void](Get-Content -Raw -Path $StdoutPath -ErrorAction SilentlyContinue)
    [void](Get-Content -Raw -Path $StderrPath -ErrorAction SilentlyContinue)
}

function Invoke-RawHttp {
    param(
        [int]$Port,
        [string]$Request
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    $client.Connect("127.0.0.1", $Port)
    try {
        $stream = $client.GetStream()
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($Request)
        $stream.Write($bytes, 0, $bytes.Length)
        $client.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)

        $buffer = New-Object byte[] 8192
        $output = [System.Text.StringBuilder]::new()
        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            [void]$output.Append([System.Text.Encoding]::UTF8.GetString($buffer, 0, $read))
        }
        return $output.ToString()
    }
    finally {
        $client.Close()
    }
}

function Assert-Matches {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Label
    )

    if ($Text -notmatch $Pattern) {
        throw "response for $Label did not match '$Pattern':`n$Text"
    }
}

$caseRoot = Join-Path $CMakeBinaryDir "users-api-sqlite-transport"
$artifacts = Join-Path $caseRoot "artifacts"
$stdoutPath = Join-Path $caseRoot "server.stdout.txt"
$stderrPath = Join-Path $caseRoot "server.stderr.txt"
$dbPath = Join-Path $ProjectSourceDir "users-api-sqlite-runtime.db"
$source = Join-Path $ProjectSourceDir "examples/users-api-sqlite/app.js"

Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $artifacts | Out-Null
Remove-Item -LiteralPath $dbPath -Force -ErrorAction SilentlyContinue

& $CargoExecutable run --manifest-path (Join-Path $ProjectSourceDir "compiler/Cargo.toml") -- build $source --out $artifacts
if ($LASTEXITCODE -ne 0) {
    throw "sloppyc build failed for users-api-sqlite"
}

$server = $null
try {
    $port = Get-FreePort
    $server = Start-SloppyServer -Artifacts $artifacts -Port $port -StdoutPath $stdoutPath -StderrPath $stderrPath

    $users = Invoke-RawHttp $port "GET /users HTTP/1.1`r`nHost: localhost`r`n`r`n"
    Assert-Matches $users "HTTP/1\.1 200 OK" "GET /users"
    Assert-Matches $users "Ada Lovelace" "GET /users"
    Assert-Matches $users "Grace Hopper" "GET /users"

    $one = Invoke-RawHttp $port "GET /users/1 HTTP/1.1`r`nHost: localhost`r`n`r`n"
    Assert-Matches $one "HTTP/1\.1 200 OK" "GET /users/1"
    Assert-Matches $one "ada@example\.test" "GET /users/1"

    $missing = Invoke-RawHttp $port "GET /users/999 HTTP/1.1`r`nHost: localhost`r`n`r`n"
    Assert-Matches $missing "HTTP/1\.1 404 Not Found" "GET /users/999"
    Assert-Matches $missing "user_not_found" "GET /users/999"

    $body = '{"name":"Lin Runtime","email":"lin@example.test"}'
    $created = Invoke-RawHttp $port "POST /users HTTP/1.1`r`nHost: localhost`r`nContent-Type: application/json`r`nContent-Length: $($body.Length)`r`n`r`n$body"
    Assert-Matches $created "HTTP/1\.1 201 Created" "POST /users"
    Assert-Matches $created "lin@example\.test" "POST /users"

    $afterPost = Invoke-RawHttp $port "GET /users HTTP/1.1`r`nHost: localhost`r`n`r`n"
    Assert-Matches $afterPost "Lin Runtime" "GET /users after POST"

    $invalid = "{"
    $invalidResponse = Invoke-RawHttp $port "POST /users HTTP/1.1`r`nHost: localhost`r`nContent-Type: application/json`r`nContent-Length: $($invalid.Length)`r`n`r`n$invalid"
    Assert-Matches $invalidResponse "HTTP/1\.1 400 Bad Request" "POST /users invalid JSON"
    Assert-Matches $invalidResponse "Malformed JSON" "POST /users invalid JSON"

    $invalidUser = '{"name":"","email":""}'
    $invalidUserResponse = Invoke-RawHttp $port "POST /users HTTP/1.1`r`nHost: localhost`r`nContent-Type: application/json`r`nContent-Length: $($invalidUser.Length)`r`n`r`n$invalidUser"
    Assert-Matches $invalidUserResponse "HTTP/1\.1 400 Bad Request" "POST /users invalid payload"
    Assert-Matches $invalidUserResponse "application/problem\+json" "POST /users invalid payload"
    Assert-Matches $invalidUserResponse "Invalid user payload" "POST /users invalid payload"
}
finally {
    Stop-SloppyServer $server $stdoutPath $stderrPath
    Remove-Item -LiteralPath $dbPath -Force -ErrorAction SilentlyContinue
}

$deniedPort = Get-FreePort
$deniedServer = $null
try {
    $deniedArtifacts = Join-Path $ProjectSourceDir "tests/integration/execution/sqlite_denied_capability"
    $deniedServer = Start-SloppyServer -Artifacts $deniedArtifacts -Port $deniedPort -StdoutPath $stdoutPath -StderrPath $stderrPath
    $denied = Invoke-RawHttp $deniedPort "GET /sqlite-denied HTTP/1.1`r`nHost: localhost`r`n`r`n"
    Assert-Matches $denied "HTTP/1\.1 500 Internal Server Error" "denied SQLite capability over TCP"
    Assert-Matches $denied "Sloppy handler failed" "denied SQLite capability over TCP"
}
finally {
    Stop-SloppyServer $deniedServer $stdoutPath $stderrPath
}
