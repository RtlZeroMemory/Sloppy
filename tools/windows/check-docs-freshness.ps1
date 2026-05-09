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

$RemovedAlphaCheckPattern = '(?i)\bcheck-' + 'alpha(?:-claims|-infra)?(?:\.ps1)?\b'
$RemovedAlphaCheckFixture = "Use tools/windows/check-" + "alpha-claims.ps1"

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
    foreach ($relative in Get-TrackedMarkdownFiles -ScanRoot $ScanRoot) {
        $normalized = $relative -replace "\\", "/"
        if ($normalized -notmatch '^(docs/|README\.md|AGENTS\.md|AGENTS_CONTRIBUTING\.md|CONTRIBUTING\.md|RELEASE_NOTES\.md|CHANGELOG\.md)') {
            continue
        }
        $path = Join-Path $ScanRoot $relative
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $path) {
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
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/how-to/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/reference/' -or $relative -eq "docs/README.md") {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/explanation/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/contributor/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } elseif ($relative -match '^docs/internals/') {
                "# Good`n`nBody.`n" | Set-Content -LiteralPath $path -Encoding ASCII
            } else {
                "# Good`n" | Set-Content -LiteralPath $path -Encoding ASCII
            }
        }
        & git -C $tempRoot add . | Out-Null

        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -eq 0) "Good docs fixture failed freshness."

        $providersPath = Join-Path $tempRoot "docs/reference/providers.md"
        "Results.json({ provider: `"sqlserver`", configured: false })" | Add-Content -LiteralPath $providersPath -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Fake SQL Server example fixture passed."

        "# Good`n`nBody.`n" | Set-Content -LiteralPath $providersPath -Encoding ASCII
        "Type: Reference" | Add-Content -LiteralPath (Join-Path $tempRoot "docs/reference/cli.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Metadata-line fixture passed."

        "# Good`n`nBody.`n" | Set-Content -LiteralPath (Join-Path $tempRoot "docs/reference/cli.md") -Encoding ASCII
        $RemovedAlphaCheckFixture | Add-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "removed alpha-check fixture passed."

        "# Good`n" | Set-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        "This page contains a prompt transcript." | Add-Content -LiteralPath (Join-Path $tempRoot "docs/explanation/what-is-sloppy.md") -Encoding ASCII
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Prompt transcript fixture passed."

        "# Good`n`nBody.`n" | Set-Content -LiteralPath (Join-Path $tempRoot "docs/explanation/what-is-sloppy.md") -Encoding ASCII
        New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/project") | Out-Null
        Assert-True ((Test-DocsFreshness -ScanRoot $tempRoot).Count -gt 0) "Stale planning directory fixture passed."
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
