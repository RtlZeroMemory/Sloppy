param(
    [Parameter(Mandatory = $true)][string]$ProjectSourceDir,
    [Parameter(Mandatory = $true)][string]$CMakeBinaryDir,
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

function Invoke-RawHttp {
    param([int]$Port, [string]$Path)

    $client = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $Port)
    try {
        $stream = $client.GetStream()
        $request = "GET $Path HTTP/1.1`r`nHost: localhost`r`nConnection: close`r`n`r`n"
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($request)
        $stream.Write($bytes, 0, $bytes.Length)
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
    param([string]$Text, [string]$Pattern, [string]$Label)
    if ($Text -notmatch $Pattern) {
        throw "$Label did not match '$Pattern':`n$Text"
    }
}

$caseRoot = Join-Path $CMakeBinaryDir "cache-package-runtime"
$projectDir = Join-Path $caseRoot "project"
$outsideDir = Join-Path $caseRoot "outside"
$stdoutPath = Join-Path $caseRoot "server.stdout.txt"
$stderrPath = Join-Path $caseRoot "server.stderr.txt"

Remove-Item -LiteralPath $caseRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $projectDir | Out-Null

@'
import { Cache, Results, Sloppy, Query } from "sloppy";

function createOutputCache() {
    return Cache.memory("default", { ttlMs: 10000 });
}

const app = Sloppy.create();
app.services.addSingleton("cache.default", () => createOutputCache());

app.get("/cached", (category: Query<string>) => Results.json({
    category: category ?? "all",
})).outputCache({
    ttlMs: 10000,
    varyByQuery: ["category"],
});

app.get("/too-large", (category: Query<string>) => Results.json({ value: "\u00e9", category: category ?? "all" })).outputCache({
    ttlMs: 10000,
    maxBodyBytes: 1,
});

app.get("/unsupported", () => {
    try {
        Cache.postgres({});
        return Results.text("unexpected");
    } catch (error) {
        return Results.text(String(error.message));
    }
});

export default app;
'@ | Set-Content -Encoding utf8 (Join-Path $projectDir "app.ts")

Push-Location $projectDir
try {
    & $SloppyCli package app.ts --out dist
    if ($LASTEXITCODE -ne 0) {
        throw "sloppy package failed for cache package runtime"
    }
}
finally {
    Pop-Location
}

New-Item -ItemType Directory -Path $outsideDir | Out-Null
Copy-Item -Recurse -LiteralPath (Join-Path $projectDir "dist/package") -Destination $outsideDir

$server = $null
try {
    $port = Get-FreePort
    New-Item -ItemType File -Path $stdoutPath -Force | Out-Null
    New-Item -ItemType File -Path $stderrPath -Force | Out-Null
    $server = Start-Process -FilePath $SloppyCli `
        -ArgumentList @("run", "package", "--host", "127.0.0.1", "--port", "$port") `
        -WorkingDirectory $outsideDir `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru

    $started = $false
    for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
        if ($server.HasExited) {
            throw "sloppy package server exited before startup: $(Get-Content -Raw -Path $stderrPath)"
        }
        try {
            $probe = [System.Net.Sockets.TcpClient]::new("127.0.0.1", $port)
            $probe.Close()
            $started = $true
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $started) {
        throw "sloppy package server did not accept connections"
    }

    $first = Invoke-RawHttp -Port $port -Path "/cached?category=books"
    $second = Invoke-RawHttp -Port $port -Path "/cached?category=books"
    $third = Invoke-RawHttp -Port $port -Path "/cached?category=music"
    $tooLarge = Invoke-RawHttp -Port $port -Path "/too-large?category=books"
    $unsupported = Invoke-RawHttp -Port $port -Path "/unsupported"

    Assert-Matches $first "HTTP/1\.1 200 OK" "first cached response"
    Assert-Matches $first "X-Sloppy-Output-Cache: MISS" "first cached response"
    Assert-Matches $first '"category":"books"' "first cached response"
    Assert-Matches $second "HTTP/1\.1 200 OK" "second cached response"
    Assert-Matches $second "X-Sloppy-Output-Cache: HIT" "second cached response"
    Assert-Matches $third "HTTP/1\.1 200 OK" "query-varied cached response"
    Assert-Matches $third "X-Sloppy-Output-Cache: MISS" "query-varied cached response"
    Assert-Matches $third '"category":"music"' "query-varied cached response"
    Assert-Matches $tooLarge "HTTP/1\.1 200 OK" "too-large cached response"
    Assert-Matches $tooLarge "X-Sloppy-Output-Cache: BYPASS" "too-large cached response"
    Assert-Matches $unsupported "requires a real postgres connection from sloppy/data" "unsupported provider diagnostic"
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        $server.Kill()
        $server.WaitForExit()
    }
}
