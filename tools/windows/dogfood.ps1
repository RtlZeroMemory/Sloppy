param(
    [string]$SloppyExe = "",
    [string]$SloppycExe = "",
    [string]$PackagePath = "",
    [string]$PackageMetadataPath = "",
    [string]$ManifestPath = "",
    [switch]$RequireV8Runtime,
    [switch]$StatusOnly,
    [switch]$Json,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $Root "examples/dogfood/dogfood.json"
}

function Read-JsonFile {
    param([string]$Path)

    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in ${Path}: $($_.Exception.Message)"
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-DogfoodManifest {
    param([object]$Manifest)

    Assert-True ($Manifest.schemaVersion -eq 1) "Dogfood catalog schemaVersion must be 1."
    $allowed = @($Manifest.statusVocabulary)
    foreach ($required in @("available", "v8-gated", "package-gated", "live-provider-gated", "blocked", "unavailable", "planned")) {
        Assert-True ($allowed -contains $required) "Dogfood catalog statusVocabulary missing '$required'."
    }

    $ids = New-Object System.Collections.Generic.HashSet[string]
    foreach ($scenario in @($Manifest.scenarios)) {
        $id = [string]$scenario.id
        Assert-True (-not [string]::IsNullOrWhiteSpace($id)) "Dogfood scenario is missing id."
        Assert-True ($ids.Add($id)) "Dogfood scenario id is duplicated: $id"
        Assert-True ($allowed -contains [string]$scenario.status) "Dogfood scenario '$id' has invalid status '$($scenario.status)'."
        if ([string]$scenario.status -in @("blocked", "unavailable", "live-provider-gated")) {
            Assert-True (-not [string]::IsNullOrWhiteSpace([string]$scenario.reason)) "Dogfood scenario '$id' must explain blocked/unavailable/gated status."
        }
    }

    foreach ($requiredId in @("hello-artifact", "hello-source-input", "package-hello-artifact", "http-app", "https-app", "sqlite-app", "postgresql-app", "sqlserver-app", "framework-v2-app")) {
        Assert-True ($ids.Contains($requiredId)) "Dogfood catalog missing required scenario '$requiredId'."
    }
}

function Invoke-Script {
    param([string[]]$Arguments)

    & powershell @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "dogfood child command failed with exit code ${LASTEXITCODE}: powershell $($Arguments -join ' ')"
    }
}

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
        [string]$WorkingDirectory
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = ConvertTo-ProcessArgumentString $Arguments
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "failed to start process: $Executable"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            try {
                $process.Kill()
            } catch {
                # The timeout failure below is the durable result.
            }
            throw "process timed out after 60s: $Executable $($startInfo.Arguments)"
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

function Add-Result {
    param(
        [System.Collections.Generic.List[object]]$Results,
        [string]$Lane,
        [string]$Status,
        [string]$Reason
    )

    $Results.Add([pscustomobject]@{
        lane = $Lane
        status = $Status
        reason = $Reason
    })
}

function Invoke-SelfTest {
    $fixture = [pscustomobject]@{
        schemaVersion = 1
        statusVocabulary = @("available", "v8-gated", "package-gated", "live-provider-gated", "blocked", "unavailable", "planned")
        scenarios = @(
            [pscustomobject]@{ id = "hello-artifact"; status = "v8-gated"; reason = "requires V8" },
            [pscustomobject]@{ id = "hello-source-input"; status = "v8-gated"; reason = "requires V8" },
            [pscustomobject]@{ id = "package-hello-artifact"; status = "package-gated"; reason = "requires package" },
            [pscustomobject]@{ id = "http-app"; status = "blocked"; reason = "owned by HTTP track" },
            [pscustomobject]@{ id = "https-app"; status = "blocked"; reason = "owned by TLS track" },
            [pscustomobject]@{ id = "sqlite-app"; status = "v8-gated"; reason = "requires V8 lane" },
            [pscustomobject]@{ id = "postgresql-app"; status = "live-provider-gated"; reason = "requires live service" },
            [pscustomobject]@{ id = "sqlserver-app"; status = "live-provider-gated"; reason = "requires live service" },
            [pscustomobject]@{ id = "framework-v2-app"; status = "blocked"; reason = "owned by framework track" }
        )
    }
    Assert-DogfoodManifest -Manifest $fixture
    Write-Host "dogfood self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

$manifest = Read-JsonFile -Path (Resolve-Path -LiteralPath $ManifestPath).Path
Assert-DogfoodManifest -Manifest $manifest

$results = [System.Collections.Generic.List[object]]::new()
foreach ($scenario in @($manifest.scenarios)) {
    if ([string]$scenario.status -in @("blocked", "unavailable", "planned", "live-provider-gated")) {
        Add-Result -Results $results -Lane ([string]$scenario.id) -Status "UNAVAILABLE" -Reason ([string]$scenario.reason)
    } elseif ([string]$scenario.status -eq "v8-gated") {
        if (-not $StatusOnly -and [string]$scenario.id -in @("hello-artifact", "hello-source-input") -and -not [string]::IsNullOrWhiteSpace($SloppyExe)) {
            continue
        }
        Add-Result -Results $results -Lane ([string]$scenario.id) -Status "UNAVAILABLE" -Reason "Positive execution requires the V8-gated dogfood lane."
    } elseif ([string]$scenario.status -eq "package-gated" -and ($StatusOnly -or [string]::IsNullOrWhiteSpace($PackagePath))) {
        Add-Result -Results $results -Lane ([string]$scenario.id) -Status "SKIPPED" -Reason "Package-mode dogfood requires a package archive input."
    }
}

if (-not $StatusOnly) {
    if (-not [string]::IsNullOrWhiteSpace($SloppyExe) -and -not [string]::IsNullOrWhiteSpace($SloppycExe)) {
        $artifactScenario = @($manifest.scenarios | Where-Object { $_.id -eq "hello-artifact" })[0]
        $artifactDir = Join-Path $Root ([string]$artifactScenario.artifactFixture)
        $artifactRun = Invoke-CapturedProcess `
            -Executable (Resolve-Path -LiteralPath $SloppyExe).Path `
            -Arguments @(
                "run",
                "--artifacts",
                $artifactDir,
                "--once",
                [string]$artifactScenario.once.method,
                [string]$artifactScenario.once.target
            ) `
            -WorkingDirectory $Root
        $artifactText = $artifactRun.Stdout + $artifactRun.Stderr
        if ($artifactRun.ExitCode -eq 0) {
            if (-not $RequireV8Runtime) {
                throw "hello-artifact dogfood unexpectedly executed without -RequireV8Runtime."
            }
            foreach ($needle in @($artifactScenario.stdoutContains)) {
                if (-not $artifactText.Contains([string]$needle)) {
                    throw "hello-artifact dogfood output did not contain expected text '$needle'."
                }
            }
            Add-Result -Results $results -Lane "hello-artifact" -Status "PASS" -Reason "V8-gated artifact hello returned the expected response."
        } elseif ($artifactText.Contains("requires V8-enabled build")) {
            if ($RequireV8Runtime) {
                throw "hello-artifact dogfood required V8 execution, but the binary reported V8 unavailable."
            }
            Add-Result -Results $results -Lane "hello-artifact" -Status "UNAVAILABLE" -Reason "non-V8 build reported the required V8 diagnostic."
        } else {
            throw "hello-artifact dogfood failed unexpectedly: $artifactText"
        }

        $sourceArgs = @(
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            (Join-Path $PSScriptRoot "test-source-input-fixtures.ps1"),
            "-SloppyExe",
            (Resolve-Path -LiteralPath $SloppyExe).Path,
            "-SloppycExe",
            (Resolve-Path -LiteralPath $SloppycExe).Path,
            "-FixtureRoot",
            (Join-Path $Root "tests/fixtures/source-input")
        )
        if ($RequireV8Runtime) {
            $sourceArgs += "-RequireV8Runtime"
        }
        Invoke-Script -Arguments $sourceArgs
        $sourceStatus = if ($RequireV8Runtime) { "PASS" } else { "UNAVAILABLE" }
        $sourceReason = if ($RequireV8Runtime) { "source-input dogfood ran with V8 required" } else { "source-input dogfood verified V8-required diagnostics in a non-V8 lane" }
        Add-Result -Results $results -Lane "hello-source-input" -Status $sourceStatus -Reason $sourceReason
        Add-Result -Results $results -Lane "source-input" -Status $sourceStatus -Reason $sourceReason
    } else {
        Add-Result -Results $results -Lane "hello-artifact" -Status "SKIPPED" -Reason "SloppyExe was not provided."
        Add-Result -Results $results -Lane "hello-source-input" -Status "SKIPPED" -Reason "SloppyExe and SloppycExe were not provided."
        Add-Result -Results $results -Lane "source-input" -Status "SKIPPED" -Reason "SloppyExe and SloppycExe were not provided."
    }

    if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
        $packageArgs = @(
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            (Join-Path $PSScriptRoot "test-package.ps1"),
            "-PackagePath",
            (Resolve-Path -LiteralPath $PackagePath).Path
        )
        if (-not [string]::IsNullOrWhiteSpace($PackageMetadataPath)) {
            $packageArgs += @("-MetadataPath", (Resolve-Path -LiteralPath $PackageMetadataPath).Path)
        }
        if ($RequireV8Runtime) {
            $packageArgs += "-RequireV8Runtime"
        }
        Invoke-Script -Arguments $packageArgs
        Add-Result -Results $results -Lane "package-hello-artifact" -Status "PASS" -Reason "package-mode hello artifact smoke ran against supplied archive"
        Add-Result -Results $results -Lane "package outside-checkout" -Status "PASS" -Reason "package-mode dogfood ran against supplied archive"
    } else {
        Add-Result -Results $results -Lane "package outside-checkout" -Status "SKIPPED" -Reason "PackagePath was not provided."
    }
}

$summary = [pscustomobject]@{
    schemaVersion = 1
    catalog = $ManifestPath
    requireV8Runtime = [bool]$RequireV8Runtime
    statusOnly = [bool]$StatusOnly
    results = @($results)
}

if ($Json) {
    $summary | ConvertTo-Json -Depth 6
} else {
    foreach ($result in @($summary.results)) {
        Write-Host ("dogfood: {0}: {1} - {2}" -f $result.lane, $result.status, $result.reason)
    }
    Write-Host "dogfood harness completed."
}
