param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath $Root).Path
$violations = New-Object System.Collections.Generic.List[string]

function Add-Violation {
    param(
        [string]$Path,
        [int]$Line,
        [string]$Message
    )

    if ($Line -gt 0) {
        $violations.Add("${Path}:${Line}: $Message")
    } else {
        $violations.Add("${Path}: $Message")
    }
}

function Get-TrackedFiles {
    param(
        [string[]]$Paths
    )

    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        throw "git was not found; test governance check cannot enumerate tracked files."
    }

    $files = & $git.Source -C $Root ls-files --cached --others --exclude-standard -- @Paths
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while enumerating test governance inputs."
    }

    return @($files | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Read-Lines {
    param(
        [string]$RelativePath
    )

    $absolute = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
        return @()
    }

    return @(Get-Content -LiteralPath $absolute)
}

function Require-Text {
    param(
        [string]$RelativePath,
        [string]$Needle
    )

    $absolute = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
        Add-Violation -Path $RelativePath -Line 0 -Message "required governance file is missing"
        return
    }

    $text = Get-Content -LiteralPath $absolute -Raw
    if (-not $text.Contains($Needle)) {
        Add-Violation -Path $RelativePath -Line 0 -Message "missing required TEST-PLATFORM-01 text: $Needle"
    }
}

function Test-CodePatterns {
    $files = Get-TrackedFiles -Paths @("tests", "compiler/tests", "stdlib", "examples", ".github")
    $codeExtensions = @(
        ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx",
        ".js", ".mjs", ".ts", ".tsx", ".rs", ".cmake", ".ps1", ".sh", ".yml", ".yaml"
    )

    foreach ($file in $files) {
        $extension = [System.IO.Path]::GetExtension($file)
        if ($extension -notin $codeExtensions) {
            continue
        }

        $lines = Read-Lines -RelativePath $file
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            $lineNumber = $i + 1

            if ($line -match '\b(describe|it|test)\.only\s*\(' -or
                $line -match '(^|[^A-Za-z0-9_])f(describe|it|test)\s*\(') {
                Add-Violation -Path $file -Line $lineNumber -Message "focused tests such as .only/fdescribe/fit are forbidden"
            }

            if ($line -match '\b(describe|it|test)\.skip\s*\(' -and
                $line -notmatch '(#\d+|TASK-|EPIC-|CORE-|ENGINE-|TEST-PLATFORM-|ALPHA-|skip reason|reason:)') {
                Add-Violation -Path $file -Line $lineNumber -Message "skipped tests must include an issue or explicit reason"
            }

            if ($line -match '(?i)\b(TODO|FIXME)\b' -and
                $line -notmatch '(#\d+|TASK-|EPIC-|CORE-|ENGINE-|TEST-PLATFORM-|ALPHA-|HTTP-|MAIN|COMPILER-|FRAMEWORK-)') {
                Add-Violation -Path $file -Line $lineNumber -Message "TODO/FIXME in test-governed files must reference a tracked task"
            }

            if ($line -match '(?i)\b(it|test|describe)\s*\(\s*["''](it should work|should work|happy path)\b') {
                Add-Violation -Path $file -Line $lineNumber -Message "weak test names must state the contract or behavior under test"
            }
        }
    }
}

function Test-SecretPatterns {
    $files = Get-TrackedFiles -Paths @("tests", "compiler/tests", "examples", "docs", ".github")
    $secretPatterns = @(
        '-----BEGIN (RSA |DSA |EC |OPENSSH )?PRIVATE KEY-----',
        '\bAKIA[0-9A-Z]{16}\b',
        '\bgh[pousr]_[A-Za-z0-9_]{20,}\b',
        '\bsk-[A-Za-z0-9]{32,}\b',
        '\bxox[baprs]-[A-Za-z0-9-]{20,}\b'
    )

    foreach ($file in $files) {
        $lines = Read-Lines -RelativePath $file
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            foreach ($pattern in $secretPatterns) {
                if ($line -match $pattern) {
                    Add-Violation -Path $file -Line ($i + 1) -Message "high-confidence secret-looking value is forbidden in tests/docs/examples/goldens"
                }
            }
        }
    }
}

function Test-GoldenNormalization {
    $files = Get-TrackedFiles -Paths @(
        "tests/golden",
        "compiler/tests/fixtures",
        "tests/fixtures/source-input",
        "tests/fixtures/package"
    )
    foreach ($file in $files) {
        $lines = Read-Lines -RelativePath $file
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            if ($line -cmatch '([A-Za-z]:\\Users\\|[A-Za-z]:/Users/|/Users/|/home/)' -and
                $line -notmatch '<repo>|<workspace>|<temp>|<path>|<redacted>') {
                Add-Violation -Path $file -Line ($i + 1) -Message "golden or compiler fixture contains an unnormalized absolute path"
            }

        }
    }
}

function Test-UnsupportedClaims {
    $files = Get-TrackedFiles -Paths @(
        "tests/golden",
        "compiler/tests/fixtures",
        "tests/fixtures/source-input",
        "tests/fixtures/package",
        "examples",
        "docs",
        ".github"
    )
    foreach ($file in $files) {
        $lines = Read-Lines -RelativePath $file
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            $context = @(
                if ($i -gt 0) { $lines[$i - 1] }
                $line
                if ($i + 1 -lt $lines.Count) { $lines[$i + 1] }
            ) -join " "
            if ($line -match '(?i)(production[- ]ready|public alpha ready|benchmark proves|performance proves|outperforms|faster than)' -and
                $context -notmatch '(?i)(not|no|never|without|deferred|blocked|does not|must not|is not|are not|before|unless|required|claim|claims)') {
                Add-Violation -Path $file -Line ($i + 1) -Message "docs/examples/goldens contain an unsupported readiness/performance claim"
            }
        }
    }
}

function Test-FixtureMetadata {
    $sourceFixtureRoot = Join-Path $Root "tests/fixtures/source-input"
    if (-not (Test-Path -LiteralPath $sourceFixtureRoot -PathType Container)) {
        Add-Violation -Path "tests/fixtures/source-input" -Line 0 -Message "source-input fixture harness is missing"
    } else {
        $metadataFiles = @(Get-ChildItem -LiteralPath $sourceFixtureRoot -Recurse -Filter "case.json" -File)
        if ($metadataFiles.Count -eq 0) {
            Add-Violation -Path "tests/fixtures/source-input" -Line 0 -Message "source-input fixture harness has no fixture metadata"
        }

        foreach ($metadataFile in $metadataFiles) {
            $relative = [System.IO.Path]::GetRelativePath($Root, $metadataFile.FullName).Replace("\", "/")
            try {
                $metadata = Get-Content -LiteralPath $metadataFile.FullName -Raw | ConvertFrom-Json
            } catch {
                Add-Violation -Path $relative -Line 0 -Message "fixture metadata is not valid JSON"
                continue
            }

            foreach ($field in @("schemaVersion", "lane", "mode", "source", "sloppyJson", "environment", "once", "expectedExit", "expected", "requiredFeatures", "requiresV8", "requiresPlatform", "requiresDependency")) {
                if ($null -eq $metadata.$field) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input metadata is missing required field '$field'"
                }
            }

            $fixtureDir = $metadataFile.Directory.FullName
            foreach ($requiredRelative in @("sloppy.json", "src/app.ts", "expected/plan-semantic.json", "expected/doctor.json", "expected/diagnostics.json")) {
                $candidate = Join-Path $fixtureDir $requiredRelative
                if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input fixture is missing $requiredRelative"
                }
            }

            if ($metadata.lane -ne "source-input") {
                Add-Violation -Path $relative -Line 0 -Message "source-input metadata must declare lane=source-input"
            }
        }
    }

    $packageFixtureRoot = Join-Path $Root "tests/fixtures/package"
    if (-not (Test-Path -LiteralPath $packageFixtureRoot -PathType Container)) {
        Add-Violation -Path "tests/fixtures/package" -Line 0 -Message "package fixture harness is missing"
    } else {
        $metadataFiles = @(Get-ChildItem -LiteralPath $packageFixtureRoot -Recurse -Filter "case.json" -File)
        if ($metadataFiles.Count -eq 0) {
            Add-Violation -Path "tests/fixtures/package" -Line 0 -Message "package fixture harness has no fixture metadata"
        }

        foreach ($metadataFile in $metadataFiles) {
            $relative = [System.IO.Path]::GetRelativePath($Root, $metadataFile.FullName).Replace("\", "/")
            try {
                $metadata = Get-Content -LiteralPath $metadataFile.FullName -Raw | ConvertFrom-Json
            } catch {
                Add-Violation -Path $relative -Line 0 -Message "fixture metadata is not valid JSON"
                continue
            }

            foreach ($field in @("schemaVersion", "archiveKind", "lane", "requiresV8Runtime", "requiredFiles", "requiredDirectories", "requiredStdlibAssets", "excludedPaths", "excludedFiles", "expectedManifest", "outsideCheckoutCompile", "expectedRun", "mustNotCompileSource")) {
                if ($null -eq $metadata.$field) {
                    Add-Violation -Path $relative -Line 0 -Message "package metadata is missing required field '$field'"
                }
            }

            if ($metadata.mustNotCompileSource -ne $true) {
                Add-Violation -Path $relative -Line 0 -Message "package fixtures must declare mustNotCompileSource=true"
            }

            if ($metadata.mustNotCompileSource -eq $true -and $metadata.outsideCheckoutCompile -eq $true) {
                Add-Violation -Path $relative -Line 0 -Message "package artifact fixtures must not run source compilation when mustNotCompileSource=true"
            }

            if ($metadata.mustNotCompileSource -eq $true -and $null -eq $metadata.prebuiltArtifactFixture) {
                Add-Violation -Path $relative -Line 0 -Message "package artifact fixtures must name a prebuiltArtifactFixture when source compilation is forbidden"
            }

            if ($metadata.lane -ne "package outside-checkout") {
                Add-Violation -Path $relative -Line 0 -Message "package metadata must declare lane=package outside-checkout"
            }
        }
    }
}

function Test-RequiredGovernanceText {
    $laneNames = @(
        "default non-V8",
        "compiler/Plan",
        "V8-gated",
        "source-input",
        "package outside-checkout",
        "platform-specific",
        "dependency-backed",
        "live-network/live-provider",
        "fuzz/property",
        "stress/torture",
        "sanitizer/memory-safety",
        "benchmark"
    )

    foreach ($lane in $laneNames) {
        Require-Text -RelativePath "docs/testing-strategy.md" -Needle $lane
        Require-Text -RelativePath "docs/quality-gates.md" -Needle $lane
        Require-Text -RelativePath ".github/PULL_REQUEST_TEMPLATE.md" -Needle $lane
    }

    foreach ($required in @(
        "Implementation Contract for Reviewers",
        "Evidence lanes",
        "Skipped or unavailable lanes",
        "Skipped optional gates are not pass claims.",
        "Goldens changed"
    )) {
        Require-Text -RelativePath ".github/PULL_REQUEST_TEMPLATE.md" -Needle $required
    }

    Require-Text -RelativePath "AGENTS.md" -Needle "Future PR test evidence"
    Require-Text -RelativePath "CONTRIBUTING.md" -Needle "Evidence Lane Report"
    Require-Text -RelativePath "tests/conformance/cross-api/README.md" -Needle "#652"
    Require-Text -RelativePath "tests/conformance/v8/bridge-test-template.md" -Needle "no raw native handle"
    Require-Text -RelativePath "tests/fuzz/README.md" -Needle "seed replay"
}

Test-CodePatterns
Test-SecretPatterns
Test-GoldenNormalization
Test-UnsupportedClaims
Test-FixtureMetadata
Test-RequiredGovernanceText

if ($violations.Count -gt 0) {
    Write-Host "test governance check failed:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Host "  $violation" -ForegroundColor Red
    }
    exit 1
}

Write-Host "test governance check passed."
