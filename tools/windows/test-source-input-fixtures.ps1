param(
    [Parameter(Mandatory = $true)]
    [string]$SloppyExe,

    [Parameter(Mandatory = $true)]
    [string]$SloppycExe,

    [string]$FixtureRoot = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path "tests/fixtures/source-input"),

    [string]$WorkRoot = (Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-source-input-fixtures-" + [System.Guid]::NewGuid().ToString("N"))),

    [switch]$RequireV8Runtime,

    [switch]$KeepWork
)

$ErrorActionPreference = "Stop"

function ConvertTo-ProcessArgumentString {
    param([string[]]$Arguments)

    $quoted = foreach ($argument in $Arguments) {
        '"' + ($argument -replace '"', '\"') + '"'
    }
    return ($quoted -join " ")
}

function Invoke-CapturedProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        [hashtable]$Environment = @{}
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = ConvertTo-ProcessArgumentString $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    foreach ($entry in $Environment.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "failed to start process: $Executable"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(180000)) {
            try {
                $process.Kill()
            } catch {
                # The timeout failure below is the durable result.
            }
            throw "process timed out after 180s: $Executable $($startInfo.Arguments)"
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Stdout = $stdoutTask.GetAwaiter().GetResult()
            Stderr = $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

function Read-JsonFile {
    param([string]$Path)
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Assert-ContainsAll {
    param(
        [string]$Text,
        [object[]]$Needles,
        [string]$Context
    )

    foreach ($needle in @($Needles)) {
        if ([string]::IsNullOrWhiteSpace([string]$needle)) {
            continue
        }
        if (-not $Text.Contains([string]$needle)) {
            throw "$Context did not contain expected text '$needle'."
        }
    }
}

function Assert-ContainsNone {
    param(
        [string]$Text,
        [object[]]$Needles,
        [string]$Context
    )

    foreach ($needle in @($Needles)) {
        if ([string]::IsNullOrWhiteSpace([string]$needle)) {
            continue
        }
        if ($Text.Contains([string]$needle)) {
            throw "$Context contained forbidden redaction marker '$needle'."
        }
    }
}

function Get-DiagnosticCodes {
    param([string]$Text)

    return @(
        [regex]::Matches($Text, '\b(?:SLOPPY|SLOPPYC)_E_[A-Z0-9_]+\b') |
            ForEach-Object { $_.Value } |
            Select-Object -Unique
    )
}

function Copy-FixtureTree {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach ($child in Get-ChildItem -LiteralPath $Source -Force) {
        Copy-Item -LiteralPath $child.FullName -Destination $Destination -Recurse -Force
    }
}

function Assert-PlanMatchesExpected {
    param(
        [string]$PlanPath,
        [object]$ExpectedPlan,
        [string]$CaseName
    )

    if ($ExpectedPlan.sourceMap -eq "not emitted") {
        if (Test-Path -LiteralPath $PlanPath -PathType Leaf) {
            throw "source-input fixture '$CaseName' emitted a Plan, but expected none."
        }
        return
    }

    if (-not (Test-Path -LiteralPath $PlanPath -PathType Leaf)) {
        throw "source-input fixture '$CaseName' did not emit app.plan.json."
    }

    $plan = Read-JsonFile -Path $PlanPath
    if ($ExpectedPlan.sourceMap -eq "present" -and $null -eq $plan.sourceMap) {
        throw "source-input fixture '$CaseName' expected sourceMap metadata in app.plan.json."
    }

    foreach ($expectedRoute in @($ExpectedPlan.routes)) {
        $matched = $false
        foreach ($actualRoute in @($plan.routes)) {
            if ($actualRoute.method -ne $expectedRoute.method -or
                $actualRoute.pattern -ne $expectedRoute.path)
            {
                continue
            }

            $handlerName = $actualRoute.name
            if ($null -eq $handlerName -and $null -ne $actualRoute.handlerId) {
                foreach ($handler in @($plan.handlers)) {
                    if ($handler.id -eq $actualRoute.handlerId) {
                        $handlerName = $handler.displayName
                        break
                    }
                }
            }

            if ($handlerName -eq $expectedRoute.handler) {
                $matched = $true
                break
            }
        }

        if (-not $matched) {
            throw "source-input fixture '$CaseName' Plan did not contain route $($expectedRoute.method) $($expectedRoute.path) handled by $($expectedRoute.handler)."
        }
    }
}

function Assert-DoctorMatchesExpected {
    param(
        [string]$SloppyExe,
        [string]$PlanPath,
        [object]$ExpectedDoctor,
        [string[]]$ObservedDiagnosticCodes,
        [string]$WorkingDirectory,
        [string]$CaseName
    )

    if ($ExpectedDoctor.status -eq "not-run") {
        if (Test-Path -LiteralPath $PlanPath -PathType Leaf) {
            throw "source-input fixture '$CaseName' expected doctor not-run, but a Plan was emitted."
        }
        foreach ($diagnostic in @($ExpectedDoctor.diagnostics)) {
            if ($diagnostic.code -notin $ObservedDiagnosticCodes) {
                throw "source-input fixture '$CaseName' did not observe expected diagnostic code '$($diagnostic.code)'."
            }
        }
        return ""
    }

    if (-not (Test-Path -LiteralPath $PlanPath -PathType Leaf)) {
        throw "source-input fixture '$CaseName' expected doctor output, but no Plan was emitted."
    }

    $doctor = Invoke-CapturedProcess `
        -Executable $SloppyExe `
        -Arguments @("doctor", "--plan", $PlanPath, "--format", "json") `
        -WorkingDirectory $WorkingDirectory
    if ($doctor.ExitCode -ne 0) {
        throw "source-input fixture '$CaseName' doctor failed with exit code $($doctor.ExitCode): $($doctor.Stderr)"
    }

    $doctorJson = $doctor.Stdout | ConvertFrom-Json
    if ($ExpectedDoctor.status -eq "ok") {
        $blocking = @($doctorJson.checks | Where-Object { $_.status -eq "error" })
        if ($blocking.Count -ne 0) {
            throw "source-input fixture '$CaseName' doctor reported blocking diagnostics."
        }
    }

    return $doctor.Stdout
}

$resolvedSloppy = (Resolve-Path -LiteralPath $SloppyExe).Path
$resolvedSloppyc = (Resolve-Path -LiteralPath $SloppycExe).Path
$resolvedFixtureRoot = (Resolve-Path -LiteralPath $FixtureRoot).Path
if (-not [System.IO.Path]::IsPathRooted($WorkRoot)) {
    $WorkRoot = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $WorkRoot))
}
if (Test-Path -LiteralPath $WorkRoot) {
    Remove-Item -LiteralPath $WorkRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null

try {
    $cases = @(Get-ChildItem -LiteralPath $resolvedFixtureRoot -Recurse -Filter "case.json" -File)
    if ($cases.Count -eq 0) {
        throw "No source-input fixture case.json files found under $resolvedFixtureRoot."
    }

    foreach ($caseFile in $cases) {
        $caseName = $caseFile.Directory.Name
        $metadata = Read-JsonFile -Path $caseFile.FullName
        $caseWork = Join-Path $WorkRoot $caseName
        Copy-FixtureTree -Source $caseFile.Directory.FullName -Destination $caseWork

        $arguments = @("run")
        if (-not [string]::IsNullOrWhiteSpace($metadata.source)) {
            $arguments += [string]$metadata.source
        }
        if (-not [string]::IsNullOrWhiteSpace($metadata.environment)) {
            $arguments += @("--environment", [string]$metadata.environment)
        }
        if ($null -ne $metadata.once) {
            $arguments += @("--once", [string]$metadata.once.method, [string]$metadata.once.target)
        }

        $run = Invoke-CapturedProcess `
            -Executable $resolvedSloppy `
            -Arguments $arguments `
            -WorkingDirectory $caseWork `
            -Environment @{ SLOPPY_SLOPPYC = $resolvedSloppyc }

        $v8UnavailableInThisLane = $metadata.requiresV8 -eq $true -and -not $RequireV8Runtime
        if ($v8UnavailableInThisLane) {
            if ($run.ExitCode -eq 0 -or -not $run.Stderr.Contains("requires V8-enabled build")) {
                throw "source-input fixture '$caseName' should report V8 unavailable in the non-V8 lane."
            }
        } elseif ($run.ExitCode -ne [int]$metadata.expectedExit) {
            throw "source-input fixture '$caseName' exited $($run.ExitCode), expected $($metadata.expectedExit).`nstdout:`n$($run.Stdout)`nstderr:`n$($run.Stderr)"
        }

        if (-not $v8UnavailableInThisLane) {
            Assert-ContainsAll -Text $run.Stdout -Needles @($metadata.expected.stdoutContains) -Context "$caseName stdout"
            Assert-ContainsAll -Text $run.Stderr -Needles @($metadata.expected.stderrContains) -Context "$caseName stderr"
        } else {
            Assert-ContainsAll -Text $run.Stderr -Needles @("requires V8-enabled build") -Context "$caseName non-V8 stderr"
        }

        $diagnostics = Read-JsonFile -Path (Join-Path $caseFile.Directory.FullName "expected/diagnostics.json")
        $redactionNeedles = @()
        if ($null -ne $diagnostics.redaction -and $null -ne $diagnostics.redaction.mustNotContain) {
            $redactionNeedles = @($diagnostics.redaction.mustNotContain)
        }
        Assert-ContainsNone -Text ($run.Stdout + $run.Stderr) -Needles $redactionNeedles -Context "$caseName process output"

        $observedCodes = Get-DiagnosticCodes -Text ($run.Stdout + "`n" + $run.Stderr)
        foreach ($expectedCode in @($diagnostics.diagnosticCodes)) {
            if ($expectedCode -notin $observedCodes) {
                throw "source-input fixture '$caseName' did not observe expected diagnostic code '$expectedCode'."
            }
        }

        $artifactDir = Join-Path $caseWork ([string]$metadata.expected.artifactDir)
        $planPath = Join-Path $artifactDir "app.plan.json"
        $expectedPlan = Read-JsonFile -Path (Join-Path $caseFile.Directory.FullName "expected/plan-semantic.json")
        Assert-PlanMatchesExpected -PlanPath $planPath -ExpectedPlan $expectedPlan -CaseName $caseName
        if (Test-Path -LiteralPath $planPath -PathType Leaf) {
            Assert-ContainsNone -Text (Get-Content -LiteralPath $planPath -Raw) -Needles $redactionNeedles -Context "$caseName app.plan.json"
        }

        $expectedDoctor = Read-JsonFile -Path (Join-Path $caseFile.Directory.FullName "expected/doctor.json")
        $doctorText = Assert-DoctorMatchesExpected `
            -SloppyExe $resolvedSloppy `
            -PlanPath $planPath `
            -ExpectedDoctor $expectedDoctor `
            -ObservedDiagnosticCodes $observedCodes `
            -WorkingDirectory $caseWork `
            -CaseName $caseName
        Assert-ContainsNone -Text $doctorText -Needles $redactionNeedles -Context "$caseName doctor output"

        Write-Host "source-input fixture passed: $caseName"
    }
} finally {
    if ($KeepWork) {
        Write-Host "Keeping source-input fixture work directory: $WorkRoot"
    } else {
        Remove-Item -LiteralPath $WorkRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "source-input fixture harness passed."
