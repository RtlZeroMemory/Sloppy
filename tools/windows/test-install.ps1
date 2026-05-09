param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [switch]$RequireV8Runtime,

    [switch]$KeepTemp
)

$ErrorActionPreference = "Stop"

function Invoke-Captured {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [int[]]$AllowedExitCodes = @(0)
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $File
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "failed to start $File"
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if (-not ($AllowedExitCodes -contains $process.ExitCode)) {
        throw "$File $($Arguments -join ' ') failed with exit code $($process.ExitCode)`nstdout:`n$stdout`nstderr:`n$stderr"
    }

    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
}

$resolvedPackage = (Resolve-Path -LiteralPath $PackagePath).Path
if ([System.IO.Path]::GetExtension($resolvedPackage) -ne ".zip") {
    throw "Windows install smoke expects a .zip archive: $resolvedPackage"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-install-smoke-" + [System.Guid]::NewGuid().ToString("N"))
$extractRoot = Join-Path $tempRoot "extract"
$workRoot = Join-Path $tempRoot "work"
New-Item -ItemType Directory -Force -Path $extractRoot, $workRoot | Out-Null

try {
    Expand-Archive -LiteralPath $resolvedPackage -DestinationPath $extractRoot -Force
    $roots = @(Get-ChildItem -LiteralPath $extractRoot -Directory)
    if ($roots.Count -ne 1) {
        throw "Expected one package root in archive, found $($roots.Count)."
    }

    $packageRoot = $roots[0].FullName
    $binRoot = Join-Path $packageRoot "bin"
    $sloppy = Join-Path $binRoot "sloppy.exe"
    $sloppyc = Join-Path $binRoot "sloppyc.exe"
    foreach ($required in @($sloppy, $sloppyc, (Join-Path $packageRoot "templates/minimal-api/sloppy.json"))) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Install smoke missing packaged file: $required"
        }
    }

    $oldPath = $env:PATH
    $oldSloppyc = $env:SLOPPY_SLOPPYC
    try {
        $env:PATH = "$binRoot;$oldPath"
        $env:SLOPPY_SLOPPYC = $sloppyc

        Invoke-Captured -File $sloppy -Arguments @("--version") -WorkingDirectory $workRoot | Out-Null
        Invoke-Captured -File $sloppy -Arguments @("doctor") -WorkingDirectory $workRoot | Out-Null
        Invoke-Captured -File $sloppyc -Arguments @("--version") -WorkingDirectory $workRoot | Out-Null
        $create = Invoke-Captured -File $sloppy -Arguments @("create", "install-app", "--template", "minimal-api", "--format", "json") -WorkingDirectory $workRoot
        if ($create.Stdout -notmatch '"created":true') {
            throw "sloppy create did not report JSON success: $($create.Stdout)"
        }

        $appRoot = Join-Path $workRoot "install-app"
        Invoke-Captured -File $sloppy -Arguments @("build") -WorkingDirectory $appRoot | Out-Null
        Invoke-Captured -File $sloppy -Arguments @("package", "--format", "json") -WorkingDirectory $appRoot | Out-Null

        $run = Invoke-Captured -File $sloppy -Arguments @("run", "--once", "GET", "/health") -WorkingDirectory $appRoot -AllowedExitCodes @(0, 1)
        if ($RequireV8Runtime) {
            if ($run.ExitCode -ne 0 -or $run.Stdout -notmatch "ok") {
                throw "V8-required install smoke did not return /health ok.`nstdout:`n$($run.Stdout)`nstderr:`n$($run.Stderr)"
            }
        } elseif ($run.ExitCode -eq 0) {
            if ($run.Stdout -notmatch "ok") {
                throw "Install smoke returned success without /health ok.`nstdout:`n$($run.Stdout)`nstderr:`n$($run.Stderr)"
            }
        } elseif ($run.ExitCode -ne 0 -and $run.Stderr -notmatch "requires V8-enabled build") {
            throw "Non-V8 install smoke failed for an unexpected reason.`nstdout:`n$($run.Stdout)`nstderr:`n$($run.Stderr)"
        }
    } finally {
        $env:PATH = $oldPath
        $env:SLOPPY_SLOPPYC = $oldSloppyc
    }

    Write-Host "Windows install smoke passed: sloppy create/build/package/run from extracted archive."
} finally {
    if ($KeepTemp) {
        Write-Host "Keeping install smoke temp directory: $tempRoot"
    } else {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
