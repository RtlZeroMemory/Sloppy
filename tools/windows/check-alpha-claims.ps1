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

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Get-RelativePath {
    param(
        [string]$Base,
        [string]$Path
    )

    $baseUri = [Uri](([System.IO.Path]::GetFullPath($Base).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar))
    $pathUri = [Uri]([System.IO.Path]::GetFullPath($Path))
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace("/", [System.IO.Path]::DirectorySeparatorChar)
}

function Get-ClaimScanFiles {
    param([string]$ScanRoot)

    $explicitFiles = @(
        "README.md",
        "CONTRIBUTING.md",
        "RELEASE_NOTES.md",
        "CHANGELOG.md",
        ".github/PULL_REQUEST_TEMPLATE.md",
        ".github/workflows/ci.yml",
        ".github/workflows/release-artifacts.yml",
        "docs/architecture.md",
        "docs/build-and-distribution.md",
        "docs/dependencies.md",
        "docs/developer-ergonomics.md",
        "docs/documentation-policy.md",
        "docs/execution-model.md",
        "docs/quality-gates.md",
        "docs/security-permissions.md",
        "docs/testing.md",
        "docs/testing-strategy.md",
        "tools/README.md",
        "tools/windows/README.md",
        "tools/unix/README.md",
        "tools/windows/package.ps1",
        "tools/unix/package.sh"
    )

    $files = New-Object System.Collections.Generic.List[string]
    foreach ($relative in $explicitFiles) {
        $path = Join-Path $ScanRoot $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $files.Add((Resolve-Path -LiteralPath $path).Path)
        }
    }

    foreach ($relativeRoot in @("docs/public", "docs/release", "docs/modules")) {
        $docsRoot = Join-Path $ScanRoot $relativeRoot
        if (Test-Path -LiteralPath $docsRoot -PathType Container) {
            Get-ChildItem -LiteralPath $docsRoot -Recurse -File -Include "*.md" |
                ForEach-Object { $files.Add($_.FullName) }
        }
    }

    return @($files | Sort-Object -Unique)
}

function Test-GuardedClaimLine {
    param([string]$Line)

    $lower = $Line.ToLowerInvariant()
    $guardPatterns = @(
        '\bnot\b',
        '\bno\b',
        '\bdoes not\b',
        '\bdo not\b',
        '\bmust not\b',
        '\bnever\b',
        '\bwithout\b',
        '\bunsupported\b',
        '\bunavailable\b',
        '\bunverified\b',
        '\buntested\b',
        '\bskipped\b',
        '\bblocked\b',
        '\bdeferred\b',
        '\brequires?\b',
        '\bexperimental\b',
        '\bpre-alpha\b',
        '\bguardrail',
        '\bnon-claims?\b',
        '\bno-claims?\b',
        '\bclaim guardrail',
        '\bforbidden\b',
        '\brequires? evidence\b',
        '\bseparate evidence\b',
        '\bbefore any public release\b',
        '\bnot configured\b'
    )

    foreach ($pattern in $guardPatterns) {
        if ($lower -match $pattern) {
            return $true
        }
    }

    return $false
}

function Get-AlphaClaimViolations {
    param([string]$ScanRoot)

    $claimRules = @(
        @{ Name = "production readiness"; Pattern = '\bproduction[- ]ready\b|\bready for production\b' },
        @{ Name = "public alpha release"; Pattern = '\b(?:is|are|ships?|creates?|created|publishes?|published|available as)\b.*\bpublic alpha release\b|\bpublic alpha release\b.*\b(?:is|are|created|published|available)\b' },
        @{ Name = "benchmark/performance claim"; Pattern = '\bbenchmark(?:s|ed)?\b.*\b(performance|throughput|latency|fast|faster)\b|\bperformance\b.*\b(claim|guarantee|win|faster)\b|\bhigh[- ]performance\b|\blow[- ]latency\b|\bfaster than\b' },
        @{ Name = "Node/Bun/Deno compatibility"; Pattern = '\bsloppy\b.*\b(node(?:\.js)?|bun|deno)[- ]compatible\b|\bcompatible with (node(?:\.js)?|bun|deno)\b|\b(node(?:\.js)?|bun|deno|npm)[- ]compatible target\b' },
        @{ Name = "security overclaim"; Pattern = '\bfully secure\b|\bfully hardened\b|\bhardened by default\b|\bsecure by default\b' },
        @{ Name = "platform overclaim"; Pattern = '\bfully cross[- ]platform\b|\ball platforms supported\b|\bsupports all platforms\b' },
        @{ Name = "provider readiness"; Pattern = '\bproviders? (?:is|are) ready\b|\b(sqlite|postgresql|postgres|sql server) provider (?:is |are )?ready\b|\blive providers? (?:is|are) ready\b' },
        @{ Name = "package/release readiness"; Pattern = '\bpackage (?:is |are )?ready\b|\brelease (?:is |are )?ready\b|\bready to release\b|\brelease-ready\b|\bpackage-ready\b' },
        @{ Name = "V8 readiness"; Pattern = '\bv8 (?:is |execution is )?ready\b|\bv8-ready\b' }
    )

    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($file in Get-ClaimScanFiles -ScanRoot $ScanRoot) {
        $relative = (Get-RelativePath -Base $ScanRoot -Path $file) -replace "\\", "/"
        $lineNumber = 0
        $previousPreviousLine = ""
        $previousLine = ""
        foreach ($line in Get-Content -LiteralPath $file) {
            $lineNumber += 1
            foreach ($rule in $claimRules) {
                $guardContext = "$previousPreviousLine $previousLine $line"
                if ($line -match $rule.Pattern -and -not (Test-GuardedClaimLine -Line $guardContext)) {
                    $violations.Add("${relative}:${lineNumber}: $($rule.Name): $($line.Trim())")
                }
            }
            $previousPreviousLine = $previousLine
            $previousLine = $line
        }
    }

    return @($violations)
}

function Get-SecretClaimViolations {
    param([string]$ScanRoot)

    $secretRules = @(
        @{ Name = "private key"; Pattern = '-----BEGIN (?:RSA |EC |OPENSSH |)PRIVATE KEY-----' },
        @{ Name = "AWS access key"; Pattern = '\bAKIA[0-9A-Z]{16}\b' },
        @{ Name = "GitHub token"; Pattern = '\bgh[pousr]_[A-Za-z0-9_]{20,}\b' },
        @{ Name = "inline password"; Pattern = '(?i)\b(password|passwd|pwd)\s*[:=]\s*[^<\s][^\s]+' },
        @{ Name = "inline API key"; Pattern = '(?i)\b(api[_-]?key|secret|token)\s*[:=]\s*[^<\s][^\s]+' }
    )

    $releaseFiles = @(
        "RELEASE_NOTES.md",
        "CHANGELOG.md",
        "docs/release/README.md",
        "docs/release/KNOWN_LIMITATIONS.md",
        "docs/release/LICENSES.md",
        "docs/release/NOTICE.md",
        ".github/workflows/release-artifacts.yml"
    )

    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($relative in $releaseFiles) {
        $file = Join-Path $ScanRoot $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            continue
        }

        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file) {
            $lineNumber += 1
            foreach ($rule in $secretRules) {
                if ($line -match $rule.Pattern -and -not (Test-GuardedClaimLine -Line $line)) {
                    $violations.Add("${relative}:${lineNumber}: $($rule.Name): $($line.Trim())")
                }
            }
        }
    }

    return @($violations)
}

function Invoke-SelfTest {
    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-claims-selftest-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path (Join-Path $tempRoot "docs/release") | Out-Null
    try {
        @"
# Fixture

Sloppy is production ready.
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        $bad = Get-AlphaClaimViolations -ScanRoot $tempRoot
        Assert-True ($bad.Count -eq 1) "No-claims scanner did not reject an unguarded production-ready fixture."

        @"
# Fixture

Sloppy is not production ready.
Package readiness requires outside-checkout package smoke evidence.
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "README.md") -Encoding ASCII
        $guarded = Get-AlphaClaimViolations -ScanRoot $tempRoot
        Assert-True ($guarded.Count -eq 0) "No-claims scanner rejected guarded limitation wording."

        @"
# Release Notes

token=ghp_123456789012345678901234567890
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "RELEASE_NOTES.md") -Encoding ASCII
        $secretBad = Get-SecretClaimViolations -ScanRoot $tempRoot
        Assert-True ($secretBad.Count -ge 1) "Secret scanner did not reject a token fixture."

        @"
# Release Notes

No secrets or credentials belong in release notes.
"@ | Set-Content -LiteralPath (Join-Path $tempRoot "RELEASE_NOTES.md") -Encoding ASCII
        $secretGuarded = Get-SecretClaimViolations -ScanRoot $tempRoot
        Assert-True ($secretGuarded.Count -eq 0) "Secret scanner rejected guarded no-secrets wording."
    } finally {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "alpha claim scanner self-test passed."
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

$violations = @()
$violations += Get-AlphaClaimViolations -ScanRoot $Root
$violations += Get-SecretClaimViolations -ScanRoot $Root

if ($violations.Count -gt 0) {
    throw "Alpha no-claims guardrail failed:`n$($violations -join [Environment]::NewLine)"
}

Write-Host "alpha no-claims guardrail passed."
exit 0
