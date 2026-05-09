param(
    [switch]$SelfTest,
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath $Root).Path
$violations = New-Object System.Collections.Generic.List[string]
$RemovedAlphaCheckPattern = '(?i)\bcheck-' + 'alpha(?:-' + 'claims|-' + 'infra)?(?:\.ps1)?\b'
$RemovedAlphaCheckFixture = "Run tools/windows/check-" + "alpha-" + "claims.ps1"
$SkipReasonPattern = '(#\d+|TA' + 'SK-|EP' + 'IC-|CO' + 'RE-|EN' + 'GINE-|TEST-' + 'PLATFORM-|AL' + 'PHA-|skip reason|reason:)'
$TodoReasonPattern = '(#\d+|TA' + 'SK-|EP' + 'IC-|CO' + 'RE-|EN' + 'GINE-|TEST-' + 'PLATFORM-|AL' + 'PHA-|HT' + 'TP-|MAIN|COM' + 'PILER-|FRAMEWORK-)'
$ConstructionPhaseDocPattern = '\b(CODEX|Codex|EP' + 'IC|TA' + 'SK|EN' + 'GINE-[0-9][0-9A-Z.-]*|CO' + 'RE-[A-Z0-9][A-Z0-9.-]*)\b'

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

function Get-RelativeRepoPath {
    param([string]$Path)

    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    $prefix = $rootFull + [System.IO.Path]::DirectorySeparatorChar
    if ($pathFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($prefix.Length).Replace("\", "/")
    }

    return $pathFull.Replace("\", "/")
}

function Test-PathInsideDirectory {
    param(
        [string]$Path,
        [string]$Directory
    )

    $directoryFull = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    $prefix = $directoryFull + [System.IO.Path]::DirectorySeparatorChar
    return $pathFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
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
        Add-Violation -Path $RelativePath -Line 0 -Message "missing required governance text: $Needle"
    }
}

function Require-AnyText {
    param(
        [string]$RelativePath,
        [string[]]$Needles,
        [string]$Label
    )

    $absolute = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
        Add-Violation -Path $RelativePath -Line 0 -Message "required governance file is missing"
        return
    }

    $text = Get-Content -LiteralPath $absolute -Raw
    foreach ($needle in $Needles) {
        if ($text.Contains($needle)) {
            return
        }
    }

    Add-Violation -Path $RelativePath -Line 0 -Message "missing required governance text ($Label): $($Needles -join ' | ')"
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

        $lines = @(Read-Lines -RelativePath $file)
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            $lineNumber = $i + 1

            if ($line -match '\b(describe|it|test)\.only\s*\(' -or
                $line -match '(^|[^A-Za-z0-9_])f(describe|it|test)\s*\(') {
                Add-Violation -Path $file -Line $lineNumber -Message "focused tests such as .only/fdescribe/fit are forbidden"
            }

            if ($line -match '\b(describe|it|test)\.skip\s*\(' -and
                $line -notmatch $SkipReasonPattern) {
                Add-Violation -Path $file -Line $lineNumber -Message "skipped tests must include an issue or explicit reason"
            }

            if ($line -match '(?i)\b(TODO|FIXME)\b' -and
                $line -notmatch $TodoReasonPattern) {
                Add-Violation -Path $file -Line $lineNumber -Message "TODO/FIXME in test-governed files must reference a tracked task"
            }

            if ($line -match '(?i)\b(it|test|describe)\s*\(\s*["''](it should work|should work|happy path)\b') {
                Add-Violation -Path $file -Line $lineNumber -Message "weak test names must state the contract or behavior under test"
            }
        }
    }
}

function Test-StaleLanguageInSourceScripts {
    $files = Get-TrackedFiles -Paths @("src", "runtime", "compiler", "stdlib", "tools", "scripts", "tests", ".github")
    $extensions = @(".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".js", ".mjs", ".ts", ".tsx", ".rs", ".ps1", ".sh", ".yml", ".yaml", ".cmake", ".md")
    $allowPathPatterns = @(
        '^AGENTS\.md$',
        '^AGENTS_CONTRIBUTING\.md$',
        '^docs/',
        '^README\.md$',
        '^CONTRIBUTING\.md$',
        '^tests/(?:golden|fixtures)/',
        '^tools/windows/check-docs-freshness\.ps1$',
        '^tools/windows/check-test-governance\.ps1$'
    )

    foreach ($file in $files) {
        $normalized = $file.Replace("\", "/")
        $extension = [System.IO.Path]::GetExtension($file)
        if ($extension -notin $extensions) {
            continue
        }
        if (@($allowPathPatterns | Where-Object { $normalized -match $_ }).Count -gt 0) {
            continue
        }

        $lines = @(Read-Lines -RelativePath $file)
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            $lineNumber = $i + 1

            if ($line -match '(?i)content-type|ContentType|TS(?:Syntax|Node|Token)?Type|SyntaxKind|TypeReference|EnumMember') {
                continue
            }

            if ($line -match '(?i)^\s*(?:#|//|/\*+|\*+)\s*(Status|What Works|Partial|Unsupported|Planned)\s*:') {
                Add-Violation -Path $normalized -Line $lineNumber -Message "stale docs-era section label found in source/script content"
            }

            if ($line -match '(?i)\b(this prompt|prompt transcript|implementation run|construction phase)\b') {
                Add-Violation -Path $normalized -Line $lineNumber -Message "stale prompt/construction wording is forbidden outside docs/AGENTS contexts"
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
        $lines = @(Read-Lines -RelativePath $file)
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
        $lines = @(Read-Lines -RelativePath $file)
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            if ($line -cmatch '([A-Za-z]:\\Users\\|[A-Za-z]:/Users/|/Users/|/home/)' -and
                $line -notmatch '<repo>|<workspace>|<temp>|<path>|<redacted>') {
                Add-Violation -Path $file -Line ($i + 1) -Message "golden or compiler fixture contains an unnormalized absolute path"
            }

        }
    }
}

function Test-CurrentClaimDocPath {
    param([string]$Path)

    if ($Path -match '^(docs/archive|tests|compiler/tests)/') {
        return $false
    }

    return (
        $Path -eq "README.md" -or
        $Path -eq "CONTRIBUTING.md" -or
        $Path -eq "AGENTS.md" -or
        $Path -eq ".github/PULL_REQUEST_TEMPLATE.md" -or
        $Path -match '^docs/[^/]+\.md$' -or
        $Path -match '^docs/(tutorials|how-to|reference|explanation|contributor|internals)/' -or
        $Path -match '^examples/.+/README\.md$' -or
        $Path -eq "stdlib/sloppy/README.md" -or
        $Path -match '^src/.+/README\.md$'
    )
}

function Test-PublicProductDocPath {
    param([string]$Path)

    return (
        $Path -eq "README.md" -or
        $Path -match '^docs/(tutorials|how-to|reference|explanation)/' -or
        $Path -match '^examples/.+/README\.md$' -or
        $Path -eq "stdlib/sloppy/README.md"
    )
}

function Test-NegatedStatementContext {
    param([string]$Context)

    return $Context -match '(?i)(\b(not|no|never|without|unsupported|avoid|against|deferred|blocked|before|unless|required|requires)\b|\b(does|do|must|is|are)\s+not\b|\bdoesn''t\b|\bdon''t\b|\bnon[- ]?(goal|goals|claim|claims)\b)'
}

function Get-UnsupportedStatementViolations {
    param([object[]]$Files)

    $found = New-Object System.Collections.Generic.List[object]

    foreach ($fileObject in $Files) {
        $file = [string]$fileObject.Path
        if (-not (Test-CurrentClaimDocPath -Path $file)) {
            continue
        }

        $lines = @($fileObject.Lines)
        for ($i = 0; $i -lt $lines.Count; $i += 1) {
            $line = $lines[$i]
            $lineNumber = $i + 1
            $context = @(
                for ($j = [Math]::Max(0, $i - 8); $j -lt $i; $j += 1) { $lines[$j] }
                $line
                if ($i + 1 -lt $lines.Count) { $lines[$i + 1] }
            ) -join " "

            if ($line -match '(?i)(production[- ]ready|production readiness|ready for production|production use|release ready|GA ready|alpha[- ]ready|alpha launch|launch ready)' -and
                -not (Test-NegatedStatementContext -Context $context)) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "current/public docs contain unsupported readiness wording" }) | Out-Null
            }

            if ($line -match '(?i)(benchmark proves|performance proves|outperforms|faster than|lower latency than|higher throughput than)' -and
                -not (Test-NegatedStatementContext -Context $context)) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "current/public docs contain unsupported benchmark or performance wording" }) | Out-Null
            }

            if ($line -match '(?i)\b(Node|Bun|Deno|npm)\b.*\b(compatible|compatibility|drop-in|supported)\b' -and
                -not (Test-NegatedStatementContext -Context $context)) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "current/public docs contain unsupported compatibility wording" }) | Out-Null
            }

            if ($line -match '(?i)\b(fully supported .*\b(runtime|provider|HTTP|V8|package|release|TLS)\b|complete (runtime|provider|HTTP|V8|package|release|TLS) (support|implementation|behavior|coverage)|complete support for (runtime|provider|HTTP|V8|package|release|TLS))\b' -and
                -not (Test-NegatedStatementContext -Context $context)) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "current/public docs contain unsupported completeness wording" }) | Out-Null
            }

            if ((Test-PublicProductDocPath -Path $file) -and (
                $line -cmatch $ConstructionPhaseDocPattern -or
                $line -match '(?i)\b(slop vibes|vibe[- ]coded|vibe coding)\b')) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "public/product docs contain construction-phase or planning-transcript wording" }) | Out-Null
            }

            if ((Test-CurrentClaimDocPath -Path $file) -and
                $line -match '(?i)\b(skeleton|stub|placeholder|this prompt|/goal|implementation run)\b' -and
                -not (Test-NegatedStatementContext -Context $context) -and
                $line -notmatch '(?i)(SQL placeholder|placeholder style|test fixture)') {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "current docs contain construction-era wording" }) | Out-Null
            }

            if ((Test-CurrentClaimDocPath -Path $file) -and
                $line -match '^(Type|Status):\s') {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "visible documentation metadata lines are forbidden in current docs" }) | Out-Null
            }

            if ((Test-CurrentClaimDocPath -Path $file) -and
                $line -match $RemovedAlphaCheckPattern) {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "stale removed alpha-check references are forbidden in current docs" }) | Out-Null
            }

            if ((Test-CurrentClaimDocPath -Path $file) -and
                $line -match '(?i)\b(prompt dump|prompt transcript|current-doc construction|current doc construction)\b') {
                $found.Add([pscustomobject]@{ Path = $file; Line = $lineNumber; Message = "prompt-dump or construction-choreography wording is forbidden in current docs" }) | Out-Null
            }
        }
    }

    return $found
}

function Test-UnsupportedStatements {
    $files = Get-TrackedFiles -Paths @(
        "tests/golden",
        "compiler/tests/fixtures",
        "tests/fixtures/source-input",
        "tests/fixtures/package",
        "examples",
        "docs",
        ".github",
        "README.md",
        "CONTRIBUTING.md",
        "AGENTS.md",
        "stdlib/sloppy/README.md",
        "src"
    )
    $fileObjects = @($files | ForEach-Object {
        [pscustomobject]@{
            Path = $_.Replace("\", "/")
            Lines = @(Read-Lines -RelativePath $_)
        }
    })

    foreach ($statement in (Get-UnsupportedStatementViolations -Files $fileObjects)) {
        Add-Violation -Path $statement.Path -Line $statement.Line -Message $statement.Message
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
            $relative = Get-RelativeRepoPath -Path $metadataFile.FullName
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
            foreach ($requiredRelative in @("sloppy.json", "expected/plan-semantic.json", "expected/doctor.json", "expected/diagnostics.json")) {
                $candidate = [System.IO.Path]::GetFullPath((Join-Path $fixtureDir $requiredRelative))
                if (-not (Test-PathInsideDirectory -Path $candidate -Directory $fixtureDir)) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input fixture path escapes fixture directory: $requiredRelative"
                } elseif (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input fixture is missing $requiredRelative"
                }
            }
            $sourceRelative = [string]$metadata.source
            if ([string]::IsNullOrWhiteSpace($sourceRelative)) {
                $sloppyJsonPath = Join-Path $fixtureDir "sloppy.json"
                if (Test-Path -LiteralPath $sloppyJsonPath -PathType Leaf) {
                    try {
                        $sloppyJson = Get-Content -LiteralPath $sloppyJsonPath -Raw | ConvertFrom-Json
                        $sourceRelative = [string]$sloppyJson.entry
                    } catch {
                        Add-Violation -Path $relative -Line 0 -Message "source-input fixture sloppy.json is not valid JSON"
                    }
                }
            }
            if ([string]::IsNullOrWhiteSpace($sourceRelative)) {
                Add-Violation -Path $relative -Line 0 -Message "source-input fixture must declare a source input or sloppy.json entry"
            } else {
                $sourceCandidate = [System.IO.Path]::GetFullPath((Join-Path $fixtureDir $sourceRelative))
                if (-not (Test-PathInsideDirectory -Path $sourceCandidate -Directory $fixtureDir)) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input fixture source escapes fixture directory: $sourceRelative"
                } elseif (-not (Test-Path -LiteralPath $sourceCandidate -PathType Leaf)) {
                    Add-Violation -Path $relative -Line 0 -Message "source-input fixture is missing $sourceRelative"
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
            $relative = Get-RelativeRepoPath -Path $metadataFile.FullName
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
        "advanced static analysis",
        "fuzz/property",
        "stress/torture",
        "sanitizer/memory-safety",
        "benchmark"
    )

    foreach ($lane in $laneNames) {
        Require-Text -RelativePath "docs/contributor/testing.md" -Needle $lane
        Require-Text -RelativePath "docs/contributor/quality-gates.md" -Needle $lane
        Require-Text -RelativePath ".github/PULL_REQUEST_TEMPLATE.md" -Needle $lane
    }

    foreach ($required in @(
        "Implementation Contract for Reviewers",
        "Evidence Lane Report",
        "Skipped or unavailable lanes",
        "Report skipped optional gates as skipped, unavailable, deferred, or not run.",
        "Goldens changed"
    )) {
        Require-Text -RelativePath ".github/PULL_REQUEST_TEMPLATE.md" -Needle $required
    }

    Require-Text -RelativePath "AGENTS.md" -Needle "Implementation Contract for Reviewers"
    Require-AnyText -RelativePath "CONTRIBUTING.md" -Needles @("Evidence Lane Report", "Evidence Reporting") -Label "evidence-section"
    Require-Text -RelativePath ".github/PULL_REQUEST_TEMPLATE.md" -Needle "Allowed statuses are exactly: PASS, FAIL, SKIPPED, UNAVAILABLE, DEFERRED, NOT RUN."
    Require-Text -RelativePath "docs/contributor/quality-gates.md" -Needle 'Use these statuses in PR reports: `PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`,'
    Require-Text -RelativePath "docs/contributor/quality-gates.md" -Needle '`DEFERRED`, `NOT RUN`.'
    Require-Text -RelativePath "docs/contributor/testing.md" -Needle 'PASS`, `FAIL`, `SKIPPED`, `UNAVAILABLE`, `DEFERRED`, or `NOT RUN`'
    Require-Text -RelativePath "tests/conformance/cross-api/README.md" -Needle "cross-API conformance scenarios"
    Require-Text -RelativePath "tests/conformance/v8/bridge-test-template.md" -Needle "no raw native handle"
    Require-Text -RelativePath "tests/fuzz/README.md" -Needle "seed replay"
}

function Invoke-SelfTest {
    $fixtures = @(
        [pscustomobject]@{
            Path = "README.md"
            Lines = @("Sloppy is production ready.")
        },
        [pscustomobject]@{
            Path = "docs/tutorials/first-api.md"
            Lines = @("This public page references EP" + "IC-01.")
        },
        [pscustomobject]@{
            Path = "docs/archive/example.md"
            Lines = @("Historical EP" + "IC-01 planning record.")
        },
        [pscustomobject]@{
            Path = "examples/hello/README.md"
            Lines = @("This is not production ready and is not Node compatible.")
        },
        [pscustomobject]@{
            Path = "README.md"
            Lines = @("Sloppy presents production readiness.")
        },
        [pscustomobject]@{
            Path = "README.md"
            Lines = @("Sloppy presents Node compatibility.")
        },
        [pscustomobject]@{
            Path = "docs/reference/cli.md"
            Lines = @("This page is a skeleton.")
        },
        [pscustomobject]@{
            Path = "docs/contributor/documentation.md"
            Lines = @("Type: Guide")
        },
        [pscustomobject]@{
            Path = "docs/contributor/quality-gates.md"
            Lines = @($RemovedAlphaCheckFixture)
        },
        [pscustomobject]@{
            Path = "docs/contributor/documentation.md"
            Lines = @("This page includes a prompt transcript.")
        }
    )

    $statements = @(Get-UnsupportedStatementViolations -Files $fixtures)
    if ($statements.Count -ne 8) {
        Write-Host "test governance self-test failed: expected 8 statement violations, got $($statements.Count)." -ForegroundColor Red
        foreach ($statement in $statements) {
            Write-Host "  $($statement.Path):$($statement.Line): $($statement.Message)" -ForegroundColor Red
        }
        exit 1
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-test-governance-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "src") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "tools") | Out-Null
    try {
        & git -C $tempRoot init | Out-Null
        @"
// Status: legacy
const x = 1;
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "src\bad.js") -Encoding ASCII
        @"
const header = "Content-Type";
enum SyntaxKind { TypeReference = 1 }
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "src\ok.ts") -Encoding ASCII
        & git -C $tempRoot add . | Out-Null

        $script:Root = $tempRoot
        $script:violations = New-Object System.Collections.Generic.List[string]
        Test-StaleLanguageInSourceScripts
        if ($script:violations.Count -eq 0) {
            Write-Host "test governance self-test failed: expected stale-language violation." -ForegroundColor Red
            exit 1
        }
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "test governance self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

Test-CodePatterns
Test-SecretPatterns
Test-GoldenNormalization
Test-UnsupportedStatements
Test-StaleLanguageInSourceScripts
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
