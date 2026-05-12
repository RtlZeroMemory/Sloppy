param(
    [string[]]$Configuration = @("Debug", "RelWithDebInfo"),
    [ValidateSet("native", "generic", "validate")]
    [string[]]$JsonMode = @("native", "generic", "validate"),
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
    foreach ($mode in $JsonMode) {
        $previousMode = [Environment]::GetEnvironmentVariable("SLOPPY_JSON_DISPATCH", "Process")
        [Environment]::SetEnvironmentVariable("SLOPPY_JSON_DISPATCH", $mode, "Process")
        try {
            foreach ($name in $names) {
                $benchArgs = @("--format", "json", "--bench", $name)
                if ($Smoke) {
                    $benchArgs += "--smoke"
                }
                $jsonText = & $benchExe @benchArgs
                if ($LASTEXITCODE -ne 0) {
                    throw "sloppy_bench failed for $config $name ($mode mode)"
                }
                $report = $jsonText | ConvertFrom-Json
                foreach ($row in $report.benchmarks) {
                    $row | Add-Member -NotePropertyName configuration -NotePropertyValue $config -Force
                    $row | Add-Member -NotePropertyName jsonMode -NotePropertyValue $mode -Force
                    $Rows += $row
                }
            }
        } finally {
            [Environment]::SetEnvironmentVariable("SLOPPY_JSON_DISPATCH", $previousMode, "Process")
        }
    }
}

$payload = [ordered]@{
    schemaVersion = 1
    label = "local sloppy JSON dispatch benchmark"
    localDevMachineMeasurements = $true
    mode = $(if ($Smoke) { "smoke" } else { "measured" })
    jsonModes = @($JsonMode)
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    rows = $Rows
}

$outPath = Join-Path $Root $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding ASCII

$Rows | Sort-Object configuration,jsonMode,name | Format-Table configuration,jsonMode,name,nsPerOp,bytesPerSecond,rowsPerSecond,checksum,nativeHits,genericFallbackCount,materializationCount,rejectCount -AutoSize
Write-Host "JSON report: $outPath"
