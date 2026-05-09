param(
    [string]$Root = "",
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
} else {
    $Root = (Resolve-Path -LiteralPath $Root).Path
}

$RemovedAlphaCheckPattern = '(?i)\bcheck-' + 'alpha(?:-' + 'claims|-' + 'infra)?(?:\.ps1)?\b'
$RemovedAlphaCheckFixture = "Use tools/windows/check-" + "alpha-" + "claims.ps1"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Get-RelativePath {
    param([string]$Base, [string]$Path)

    $baseUri = [Uri](([System.IO.Path]::GetFullPath($Base).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar))
    $pathUri = [Uri]([System.IO.Path]::GetFullPath($Path))
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace("\", "/")
}

function Get-TrackedMarkdownFiles {
    param([string]$ScanRoot)

    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        throw "git was not found; docs freshness check cannot enumerate tracked files."
    }

    $files = & $git.Source -C $ScanRoot ls-files -- "*.md"
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while enumerating docs inputs."
    }

    return @($files | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Test-HasFencedCodeBlock {
    param([string[]]$Lines)
    return (($Lines -join "`n") -match '(?s)```[a-zA-Z0-9_-]*\r?\n.+?\r?\n```')
}

function Test-HasCommandBlock {
    param([string[]]$Lines)
    return (($Lines -join "`n") -match '(?is)```(?:powershell|pwsh|bash|sh|cmd|shell|console)?\s*\r?\n(?:\s*(?:\.\s*\\|sloppy|sloppyc|git|cmake|ctest|npm|pnpm|yarn|cargo|rustc|node|curl|Invoke-WebRequest|Set-Location|cd)\b.*\r?\n)+```')
}

function Test-HasExpectedOutputCue {
    param([string[]]$Lines)
    return (($Lines -join "`n") -match '(?im)^\s{0,3}(?:#{1,6}\s*)?(Expected (?:Output|Result)|Output|Result|Verify|Validation)\b')
}

function Get-NonHeadingNonFenceLineCount {
    param([string[]]$Lines)
    $count = 0
    foreach ($line in $Lines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -match '^\s*#') { continue }
        if ($line -match '^\s*```') { continue }
        $count += 1
    }
    return $count
}

function Get-MarkdownHeadings {
    param([string[]]$Lines)
    return @($Lines | Where-Object { $_ -match '^\s*#{1,6}\s+' })
}

function Test-ReferenceStructure {
    param([string[]]$Lines)

    $text = $Lines -join "`n"
    $hasTable = $text -match '(?m)^\|.+\|\s*$'
    $hasList = $text -match '(?m)^\s*[-*]\s+'
    $hasApiCue = $text -match '(?im)\b(API|Endpoint|Method|Methods|Route|Signature|Options?|Flags?)\b'
    return ($hasTable -or ($hasList -and $hasApiCue) -or ($hasApiCue -and $text -match '(?m)^#{1,6}\s+'))
}

function Test-InternalsRequiredSections {
    param([string[]]$Lines)

    $text = $Lines -join "`n"
    $required = @(
        "Purpose",
        "Where It Lives",
        "Main Concepts",
        "Lifecycle",
        "Invariants",
        "Failure Behavior",
        "Public API Relationship",
        "Tests And Evidence",
        "Current Limits"
    )
    foreach ($name in $required) {
        if ($text -notmatch ("(?im)^#{1,6}\s+" + [Regex]::Escape($name) + "\b")) {
            return $false
        }
    }
    return $true
}

function Test-DocsFreshness {
    param([string]$ScanRoot)

    $errors = New-Object System.Collections.Generic.List[string]
    $requiredFiles = @(
        "README.md",
        "AGENTS.md",
        "AGENTS_CONTRIBUTING.md",
        "CONTRIBUTING.md",
        "docs/README.md",
        "docs/tutorials/first-api.md",
        "docs/tutorials/sqlite-api.md",
        "docs/how-to/install-sloppy.md",
        "docs/how-to/create-a-project.md",
        "docs/how-to/build-artifacts.md",
        "docs/how-to/run-an-app.md",
        "docs/how-to/run-one-request.md",
        "docs/how-to/configure-an-app.md",
        "docs/how-to/use-sqlite.md",
        "docs/how-to/run-live-postgres-checks.md",
        "docs/how-to/run-live-sqlserver-checks.md",
        "docs/how-to/package-sloppy.md",
        "docs/how-to/troubleshoot-v8.md",
        "docs/reference/cli.md",
        "docs/reference/sloppy-json.md",
        "docs/reference/configuration.md",
        "docs/reference/framework.md",
        "docs/reference/routing.md",
        "docs/reference/request-context.md",
        "docs/reference/results.md",
        "docs/reference/validation.md",
        "docs/reference/dependency-injection.md",
        "docs/reference/data-api.md",
        "docs/reference/providers.md",
        "docs/reference/diagnostics.md",
        "docs/reference/plan-format.md",
        "docs/reference/supported-syntax.md",
        "docs/reference/platform-status.md",
        "docs/reference/stability.md",
        "docs/explanation/what-is-sloppy.md",
        "docs/explanation/source-input-and-artifacts.md",
        "docs/explanation/compiler-and-plan-model.md",
        "docs/explanation/why-no-node-modules.md",
        "docs/explanation/configuration-model.md",
        "docs/explanation/provider-runtime-model.md",
        "docs/explanation/v8-bridge-model.md",
        "docs/explanation/security-and-redaction.md",
        "docs/explanation/packaging-model.md",
        "docs/contributor/building-from-source.md",
        "docs/contributor/v8-sdk.md",
        "docs/contributor/dev-scripts.md",
        "docs/contributor/testing.md",
        "docs/contributor/quality-gates.md",
        "docs/contributor/release-artifacts.md",
        "docs/contributor/coding-standards.md",
        "docs/contributor/documentation.md",
        "docs/contributor/review-playbook.md",
        "docs/internals/architecture.md",
        "docs/internals/compiler.md",
        "docs/internals/plan.md",
        "docs/internals/runtime.md",
        "docs/internals/v8-bridge.md",
        "docs/internals/async-runtime.md",
        "docs/internals/memory-model.md",
        "docs/internals/http-runtime.md",
        "docs/internals/provider-runtime.md",
        "docs/internals/platform-boundaries.md",
        "docs/internals/security-model.md"
    )

    foreach ($relative in $requiredFiles) {
        $path = Join-Path $ScanRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $errors.Add("Missing required documentation file: $relative") | Out-Null
        }
    }

    foreach ($removedDir in @("docs/public", "docs/modules", "docs/project", "docs/exec-plans", "docs/skills")) {
        if (Test-Path -LiteralPath (Join-Path $ScanRoot $removedDir)) {
            $errors.Add("Removed construction-era documentation directory still exists: $removedDir") | Out-Null
        }
    }

    $badExamplePattern = 'Results\.json\(\s*\{\s*provider:\s*"sqlserver"\s*,\s*configured:\s*false\s*\}'
    $allowedRootDocs = @("docs/README.md", "docs/glossary.md", "docs/documentation-policy.md")
    foreach ($relative in Get-TrackedMarkdownFiles -ScanRoot $ScanRoot) {
        $normalized = $relative -replace "\\", "/"
        if ($normalized -notmatch '^(docs/|README\.md|AGENTS\.md|AGENTS_CONTRIBUTING\.md|CONTRIBUTING\.md|RELEASE_NOTES\.md|CHANGELOG\.md)') {
            continue
        }
        $path = Join-Path $ScanRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            continue
        }
        if ($normalized -match '^docs/[^/]+\.md$' -and $allowedRootDocs -notcontains $normalized) {
            $errors.Add("${normalized}: unexpected root docs page; keep root docs to docs/README.md, docs/glossary.md, docs/documentation-policy.md") | Out-Null
        }
        $lines = @(Get-Content -LiteralPath $path)
        $text = $lines -join "`n"
        if ($normalized -match '^docs/tutorials/(?!README\.md$).+\.md$') {
            if (-not (Test-HasFencedCodeBlock -Lines $lines)) {
                $errors.Add("${normalized}: tutorial pages must include at least one fenced code block") | Out-Null
            }
            if (-not (Test-HasCommandBlock -Lines $lines)) {
                $errors.Add("${normalized}: tutorial pages must include a runnable command block") | Out-Null
            }
            if (-not (Test-HasExpectedOutputCue -Lines $lines)) {
                $errors.Add("${normalized}: tutorial pages must include expected output/result or equivalent verification cues") | Out-Null
            }
            if ((Get-NonHeadingNonFenceLineCount -Lines $lines) -lt 8) {
                $errors.Add("${normalized}: tutorial page is too thin for a non-index tutorial") | Out-Null
            }
        }
        if ($normalized -match '^docs/how-to/.+\.md$') {
            $allowlist = @()
            if ($allowlist -notcontains $normalized) {
                $hasSteps = $text -match '(?m)^\s*(?:\d+\.\s+|[-*]\s+)'
                if (-not $hasSteps) {
                    $errors.Add("${normalized}: how-to pages must include explicit steps") | Out-Null
                }
                if (-not (Test-HasCommandBlock -Lines $lines)) {
                    $errors.Add("${normalized}: how-to pages must include runnable command blocks unless allowlisted") | Out-Null
                }
            }
            if ((Get-NonHeadingNonFenceLineCount -Lines $lines) -lt 6) {
                $errors.Add("${normalized}: how-to page is too thin") | Out-Null
            }
        }
        if ($normalized -match '^docs/reference/.+\.md$') {
            if (-not (Test-ReferenceStructure -Lines $lines)) {
                $errors.Add("${normalized}: reference pages must include table/list/API/options structure") | Out-Null
            }
            if ((Get-NonHeadingNonFenceLineCount -Lines $lines) -lt 6) {
                $errors.Add("${normalized}: reference page is too thin") | Out-Null
            }
        }
        if ($normalized -match '^docs/internals/.+\.md$') {
            if ((Get-NonHeadingNonFenceLineCount -Lines $lines) -lt 30 -or (Get-MarkdownHeadings -Lines $lines).Count -lt 9) {
                $errors.Add("${normalized}: internals page is too thin; requires meaningful body and section count") | Out-Null
            }
            if (-not (Test-InternalsRequiredSections -Lines $lines)) {
                $errors.Add("${normalized}: internals pages must include Purpose, Where It Lives, Main Concepts, Lifecycle, Invariants, Failure Behavior, Public API Relationship, Tests And Evidence, and Current Limits") | Out-Null
            }
        }
        if ($text -match '(?is)^\s*#{1,6}\s*Status\b.*^\s*#{1,6}\s*What Works\b.*^\s*#{1,6}\s*Partial\b.*^\s*#{1,6}\s*Unsupported\b.*^\s*#{1,6}\s*Planned\b') {
            $errors.Add("${normalized}: stale Status/What Works/Partial/Unsupported/Planned stub layout is forbidden") | Out-Null
        }
        $lineNumber = 0
        foreach ($line in $lines) {
            $lineNumber += 1
            if ($line -match '^(Type|Status):\s') {
                $errors.Add("${normalized}:${lineNumber}: visible documentation metadata is forbidden") | Out-Null
            }
            if ($line -match $RemovedAlphaCheckPattern) {
                $errors.Add("${normalized}:${lineNumber}: stale removed alpha-check references are forbidden") | Out-Null
            }
            if ($line -match $badExamplePattern) {
                $errors.Add("${normalized}:${lineNumber}: fake SQL Server provider example is forbidden") | Out-Null
            }
            if ($line -match '(?i)\b(CODEX|Codex|/goal|this prompt|implementation run)\b') {
                $errors.Add("${normalized}:${lineNumber}: prompt or agent choreography wording is forbidden in current docs") | Out-Null
            }
            if ($line -match '(?i)\b(prompt dump|prompt transcript|current-doc construction|current doc construction)\b') {
                $errors.Add("${normalized}:${lineNumber}: prompt-dump or construction-choreography wording is forbidden in current docs") | Out-Null
            }
            if ($line -match '(?i)\b(skeleton|stub|placeholder)\b' -and
                $normalized -notmatch '^docs/release/' -and
                $line -notmatch '(?i)(do not|must not|not a|not write|forbidden|reject|without|no fake|no dry)' -and
                $line -notmatch '(?i)(SQL placeholder|placeholder style|test fixture)') {
                $errors.Add("${normalized}:${lineNumber}: construction-stub wording is forbidden in current docs") | Out-Null
            }
            if ($line -cmatch '([A-Za-z]:\\|/Users/|/home/|/mnt/)' -and
                $line -notmatch '<repo>|<workspace>|<temp>|<path>|<redacted>') {
                $errors.Add("${normalized}:${lineNumber}: machine-local path is forbidden in current docs") | Out-Null
            }
        }
    }

    $exampleReadmes = Get-TrackedMarkdownFiles -ScanRoot $ScanRoot | Where-Object { ($_ -replace "\\", "/") -match '^examples/[^/]+/README\.md$' }
    foreach ($relative in $exampleReadmes) {
        $normalized = $relative -replace "\\", "/"
        $dir = Split-Path -Parent (Join-Path $ScanRoot $relative)
        $readmeText = Get-Content -LiteralPath (Join-Path $ScanRoot $relative) -Raw
        if ($readmeText -match '(?im)^\s*(?:Feature|Named Feature)\s*:\s*([a-z0-9._-]+)\s*$') {
            $namedFeature = $Matches[1]
            $codeFiles = @(Get-ChildItem -LiteralPath $dir -Recurse -File | Where-Object {
                $_.Name -ne "README.md" -and $_.Extension -in @(".js", ".ts", ".tsx", ".json", ".sql", ".ps1", ".sh")
            })
            $used = $false
            foreach ($file in $codeFiles) {
                $body = Get-Content -LiteralPath $file.FullName -Raw
                if ($body -match ("(?i)\b" + [Regex]::Escape($namedFeature) + "\b")) {
                    $used = $true
                    break
                }
            }
            if (-not $used) {
                $errors.Add("${normalized}: fake feature README; named feature '$namedFeature' is not used in example code") | Out-Null
            }
        }
    }

    return @($errors)
}

function Invoke-SelfTest {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-docs-freshness-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/tutorials") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/how-to") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/reference") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/explanation") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/contributor") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/internals") | Out-Null
    try {
        & git -C $tempRoot init | Out-Null
        foreach ($relative in @(
            "README.md",
            "AGENTS.md",
            "AGENTS_CONTRIBUTING.md",
            "CONTRIBUTING.md",
            "docs/README.md",
            "docs/tutorials/first-api.md",
            "docs/tutorials/sqlite-api.md",
            "docs/how-to/install-sloppy.md",
            "docs/how-to/create-a-project.md",
            "docs/how-to/build-artifacts.md",
            "docs/how-to/run-an-app.md",
            "docs/how-to/run-one-request.md",
            "docs/how-to/configure-an-app.md",
            "docs/how-to/use-sqlite.md",
            "docs/how-to/run-live-postgres-checks.md",
            "docs/how-to/run-live-sqlserver-checks.md",
            "docs/how-to/package-sloppy.md",
            "docs/how-to/troubleshoot-v8.md",
            "docs/reference/cli.md",
            "docs/reference/sloppy-json.md",
            "docs/reference/configuration.md",
            "docs/reference/framework.md",
            "docs/reference/routing.md",
            "docs/reference/request-context.md",
            "docs/reference/results.md",
            "docs/reference/validation.md",
            "docs/reference/dependency-injection.md",
            "docs/reference/data-api.md",
            "docs/reference/providers.md",
            "docs/reference/diagnostics.md",
            "docs/reference/plan-format.md",
            "docs/reference/supported-syntax.md",
            "docs/reference/platform-status.md",
            "docs/reference/stability.md",
            "docs/explanation/what-is-sloppy.md",
            "docs/explanation/source-input-and-artifacts.md",
            "docs/explanation/compiler-and-plan-model.md",
            "docs/explanation/why-no-node-modules.md",
            "docs/explanation/configuration-model.md",
            "docs/explanation/provider-runtime-model.md",
            "docs/explanation/v8-bridge-model.md",
            "docs/explanation/security-and-redaction.md",
            "docs/explanation/packaging-model.md",
            "docs/contributor/building-from-source.md",
            "docs/contributor/v8-sdk.md",
            "docs/contributor/dev-scripts.md",
            "docs/contributor/testing.md",
            "docs/contributor/quality-gates.md",
            "docs/contributor/release-artifacts.md",
            "docs/contributor/coding-standards.md",
            "docs/contributor/documentation.md",
            "docs/contributor/review-playbook.md",
            "docs/internals/architecture.md",
            "docs/internals/compiler.md",
            "docs/internals/plan.md",
            "docs/internals/runtime.md",
            "docs/internals/v8-bridge.md",
            "docs/internals/async-runtime.md",
            "docs/internals/memory-model.md",
            "docs/internals/http-runtime.md",
            "docs/internals/provider-runtime.md",
            "docs/internals/platform-boundaries.md",
            "docs/internals/security-model.md"
        )) {
            $path = Join-Path $tempRoot $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
            if ($relative -match '^docs/tutorials/') {
                @"
# Good Tutorial

## Steps
1. Build.

```powershell
.\tools\windows\dev.ps1 build
```

```ts
const value = 1;
```

## Expected Output
Build succeeds.
"@ | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/how-to/') {
                @"
# Good How-To

1. Configure.
2. Run.

```powershell
.\tools\windows\dev.ps1 test
```
"@ | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/reference/' -or $relative -eq "docs/README.md") {
                @"
# Good Reference

## API
- Option: Value

| Name | Description |
| --- | --- |
| test | ok |
"@ | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/explanation/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/contributor/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/internals/') {
                @"
# Internals

## Purpose
This section explains internal behavior in detail.

## Where It Lives
- src/core/example.c
- tests/unit/example.c

## Main Concepts
The subsystem owns lifecycle, ordering, and resource rules.
It keeps public APIs separate from implementation state.

## Lifecycle
Startup validates inputs, runtime owns active state, and shutdown performs cleanup.

## Invariants
- Invariant one.
- Invariant two.

## Failure Behavior
Failures stop at the owning layer and produce diagnostics.

## Public API Relationship
Public APIs expose behavior, not implementation handles.

## Tests And Evidence
Unit tests, integration tests, and docs checks cover the current behavior.

## Current Limits
Future work is tracked in issues and contributor docs.
"@ | Set-Content -LiteralPath $path -Encoding ASCII
            } else {
                "# Good`n" | Set-Content -LiteralPath $path -Encoding ASCII
            }
        }
        & git -C $tempRoot add . | Out-Null

        Assert-True -Condition (Test-HasFencedCodeBlock -Lines @('```ts', 'const x = 1;', '```')) -Message "Good tutorial code block fixture failed."
        Assert-True -Condition (Test-HasCommandBlock -Lines @('```powershell', '.\tools\windows\dev.ps1 test', '```')) -Message "Good tutorial command block fixture failed."
        Assert-True -Condition (Test-HasExpectedOutputCue -Lines @('## Expected Output', 'done')) -Message "Good tutorial expected-output fixture failed."
        Assert-True -Condition (Test-ReferenceStructure -Lines @('## API', '- Option: value')) -Message "Good reference structure fixture failed."
        Assert-True -Condition (Test-InternalsRequiredSections -Lines @(
            '## Purpose',
            'A',
            '## Where It Lives',
            'B',
            '## Main Concepts',
            'C',
            '## Lifecycle',
            'D',
            '## Invariants',
            'E',
            '## Failure Behavior',
            'F',
            '## Public API Relationship',
            'G',
            '## Tests And Evidence',
            'H',
            '## Current Limits',
            'I'
        )) -Message "Good internals sections fixture failed."

        $providersPath = Join-Path $tempRoot "docs/reference/providers.md"
        "Results.json({ provider: `"sqlserver`", configured: false })" | Add-Content -LiteralPath $providersPath -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Fake SQL Server example fixture passed."

        "# Good`n`nBody.`n" | Set-Content -LiteralPath $providersPath -Encoding ASCII
        "Type: Reference" | Add-Content -LiteralPath (Join-Path $tempRoot "docs/reference/cli.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Metadata-line fixture passed."

        @"
# Good Reference
## API
- Option: Value
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "docs/reference/cli.md") -Encoding ASCII
        $RemovedAlphaCheckFixture | Add-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "removed alpha-check fixture passed."

        "# Good`n" | Set-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        "This page contains a prompt transcript." | Add-Content -LiteralPath (Join-Path $tempRoot "docs/explanation/what-is-sloppy.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Prompt transcript fixture passed."

        "# Good`n`nBody.`n" | Set-Content -LiteralPath (Join-Path $tempRoot "docs/explanation/what-is-sloppy.md") -Encoding ASCII
        New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/project") | Out-Null
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Stale planning directory fixture passed."

        Remove-Item -LiteralPath (Join-Path $tempRoot "docs/project") -Recurse -Force
        "Bad root doc." | Set-Content -LiteralPath (Join-Path $tempRoot "docs/bad-root.md") -Encoding ASCII
        & git -C $tempRoot add . | Out-Null
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Unexpected root docs fixture passed."
        Remove-Item -LiteralPath (Join-Path $tempRoot "docs/bad-root.md") -Force

        @"
# Bad Tutorial
Only one line.
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "docs/tutorials/first-api.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Thin tutorial fixture passed."
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "docs freshness self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

$errors = Test-DocsFreshness -ScanRoot $Root
if ($errors.Count -gt 0) {
    Write-Host "docs freshness check failed:" -ForegroundColor Red
    foreach ($errorMessage in $errors) {
        Write-Host "  - $errorMessage" -ForegroundColor Red
    }
    exit 1
}

Write-Host "docs freshness check passed."
exit 0
