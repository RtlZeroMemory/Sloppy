param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$RequiredFiles = @(
    "docs/documentation-policy.md",
    "docs/testing-strategy.md",
    "docs/public/README.md",
    "docs/modules/README.md"
)

$errors = New-Object System.Collections.Generic.List[string]

function Test-HasHeading {
    param(
        [string]$Content,
        [string[]]$Headings
    )

    foreach ($heading in $Headings) {
        if ($Content -match "(?m)^##\s+$([regex]::Escape($heading))\s*$") {
            return $true
        }
    }

    return $false
}

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
        $relative = Resolve-Path -LiteralPath $readme -Relative
        if (-not (Test-HasHeading -Content $content -Headings @("Purpose"))) {
            $errors.Add("Module doc $relative is missing a purpose heading") | Out-Null
        }
        if (-not (Test-HasHeading -Content $content -Headings @("Current Status", "Status"))) {
            $errors.Add("Module doc $relative is missing a current status heading") | Out-Null
        }
        if (-not (Test-HasHeading -Content $content -Headings @("Invariants", "Ownership/Lifetime Rules"))) {
            $errors.Add("Module doc $relative is missing invariants or ownership/lifetime rules") | Out-Null
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
