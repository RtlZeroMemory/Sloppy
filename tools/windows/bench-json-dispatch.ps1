param(
    [string[]]$Configuration = @("Debug", "RelWithDebInfo"),
    [switch]$Smoke,
    [Alias("Out")]
    [string]$Output = "artifacts/bench/json-dispatch.json"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Dev = Join-Path $Root "tools/windows/dev.ps1"
$Rows = @()

function Resolve-Preset([string]$Config) {
    switch ($Config) {
        "Release" { return "windows-release" }
        "RelWithDebInfo" { return "windows-relwithdebinfo" }
        default { return "windows-dev" }
    }
}

foreach ($config in $Configuration) {
    $preset = Resolve-Preset $config
    $benchExe = Join-Path (Join-Path (Join-Path $Root "build") $preset) "sloppy_bench.exe"
    if (-not (Test-Path -LiteralPath $benchExe -PathType Leaf)) {
        & $Dev build -Preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "build failed for $preset"
        }
    }

    $listOutput = & $benchExe --list
    if ($LASTEXITCODE -ne 0) {
        throw "sloppy_bench --list failed for $config"
    }
    $names = @($listOutput | ForEach-Object {
        $parts = $_ -split "`t"
        if ($parts.Count -ge 2 -and $parts[1] -eq "json") {
            $parts[0]
        }
    })
    foreach ($name in $names) {
        $args = @("--format", "json", "--bench", $name)
        if ($Smoke) {
            $args += "--smoke"
        }
        $jsonText = & $benchExe @args
        if ($LASTEXITCODE -ne 0) {
            throw "sloppy_bench failed for $config $name"
        }
        $report = $jsonText | ConvertFrom-Json
        foreach ($row in $report.benchmarks) {
            $row | Add-Member -NotePropertyName configuration -NotePropertyValue $config -Force
            $Rows += $row
        }
    }
}

$payload = [ordered]@{
    schemaVersion = 1
    label = "local sloppy JSON dispatch benchmark"
    localDevMachineMeasurements = $true
    mode = $(if ($Smoke) { "smoke" } else { "measured" })
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    rows = $Rows
}

$outPath = Join-Path $Root $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding ASCII

$Rows | Sort-Object configuration,name | Format-Table configuration,name,nsPerOp,bytesPerSecond,rowsPerSecond,checksum,nativeHits,genericFallbackCount,materializationCount,rejectCount -AutoSize
Write-Host "JSON report: $outPath"
