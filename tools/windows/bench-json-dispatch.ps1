param(
    [string[]]$Configuration = @("Debug", "RelWithDebInfo"),
    [ValidateSet("native", "generic", "validate")]
    [string[]]$JsonMode = @("native", "generic", "validate"),
    [ValidateRange(1, 100)]
    [int]$Repeat = 1,
    [switch]$Smoke,
    [Alias("Out")]
    [string]$Output = "artifacts/bench/json-dispatch.json"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Dev = Join-Path $Root "tools/windows/dev.ps1"
$Rows = @()
$CommitSha = (& git -C $Root rev-parse HEAD).Trim()
$OsVersion = [System.Environment]::OSVersion.VersionString
$CpuName = "unknown"
try {
    $CpuName = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name).Trim()
} catch {
    $CpuName = "unavailable"
}

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
            for ($repeatIndex = 1; $repeatIndex -le $Repeat; $repeatIndex += 1) {
                foreach ($name in $names) {
                    $benchArgs = @("--format", "json", "--bench", $name)
                    if ($Smoke) {
                        $benchArgs += "--smoke"
                    }
                    $jsonText = & $benchExe @benchArgs
                    if ($LASTEXITCODE -ne 0) {
                        throw "sloppy_bench failed for $config $name ($mode mode, repeat $repeatIndex)"
                    }
                    $report = $jsonText | ConvertFrom-Json
                    foreach ($row in $report.benchmarks) {
                        $row | Add-Member -NotePropertyName configuration -NotePropertyValue $config -Force
                        $row | Add-Member -NotePropertyName jsonMode -NotePropertyValue $mode -Force
                        $row | Add-Member -NotePropertyName repeat -NotePropertyValue $repeatIndex -Force
                        $Rows += $row
                    }
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
    repeat = $Repeat
    commitSha = $CommitSha
    os = $OsVersion
    cpu = $CpuName
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    rows = $Rows
}

$outPath = Join-Path $Root $Output
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding ASCII

$Rows | Sort-Object configuration,jsonMode,name,repeat | Format-Table configuration,jsonMode,repeat,name,nsPerOp,bytesPerSecond,rowsPerSecond,checksum,nativeHits,genericFallbackCount,materializationCount,rejectCount -AutoSize
Write-Host "JSON report: $outPath"
