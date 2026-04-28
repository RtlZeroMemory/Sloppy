param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$RequiredFiles = @(
    "docs/documentation-policy.md",
    "docs/testing-strategy.md",
    "docs/public/README.md",
    "docs/modules/README.md"
)

$RequiredModuleHeadings = @(
    "Status",
    "Purpose",
    "Scope",
    "Non-goals",
    "Public/Internal API",
    "Ownership/Lifetime Rules",
    "Invariants",
    "Diagnostics",
    "Tests",
    "Source Docs",
    "Open Questions"
)

$errors = New-Object System.Collections.Generic.List[string]

foreach ($file in $RequiredFiles) {
    $path = Join-Path $Root $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors.Add("Missing required documentation file: $file") | Out-Null
    }
}

$modulesRoot = Join-Path $Root "docs/modules"
if (Test-Path -LiteralPath $modulesRoot -PathType Container) {
    $moduleReadmes = Get-ChildItem -LiteralPath $modulesRoot -Directory |
        ForEach-Object { Join-Path $_.FullName "README.md" }

    foreach ($readme in $moduleReadmes) {
        if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) {
            $relative = Resolve-Path -LiteralPath (Split-Path -Parent $readme) -Relative
            $errors.Add("Missing module README: $relative/README.md") | Out-Null
            continue
        }

        $content = Get-Content -Raw -LiteralPath $readme
        foreach ($heading in $RequiredModuleHeadings) {
            if ($content -notmatch "(?m)^##\s+$([regex]::Escape($heading))\s*$") {
                $relative = Resolve-Path -LiteralPath $readme -Relative
                $errors.Add("Module doc $relative is missing heading: ## $heading") | Out-Null
            }
        }
    }
}

if ($errors.Count -gt 0) {
    Write-Host "docs freshness check failed:" -ForegroundColor Red
    foreach ($errorMessage in $errors) {
        Write-Host "  - $errorMessage" -ForegroundColor Red
    }
    exit 1
}

Write-Host "docs freshness check passed."
