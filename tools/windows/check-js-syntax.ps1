param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

$scanRoots = @(
    "stdlib",
    "tests",
    "tools",
    "examples",
    "templates",
    "packages",
    "benchmarks"
)

$sourceExtensions = @(".js", ".mjs", ".cjs")

function Convert-ToRepoPath {
    param([string]$Path)

    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    return $fullPath.Substring($Root.Length).TrimStart('\', '/') -replace "\\", "/"
}

function Test-ExcludedPath {
    param([string]$RelativePath)

    if ($RelativePath -match '(^|/)node_modules/') {
        return $true
    }

    foreach ($prefix in @("build/", "artifacts/", "compiler/target/", "target/", ".sdeps/", ".sloppy/")) {
        if ($RelativePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Test-JavaScriptSyntaxPath {
    param([string]$RelativePath)

    if (Test-ExcludedPath $RelativePath) {
        return $false
    }

    return [System.IO.Path]::GetExtension($RelativePath) -in $sourceExtensions
}

function Get-GitJavaScriptFiles {
    $git = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        return @()
    }

    Push-Location $Root
    try {
        $paths = & $git.Source ls-files --cached --others --exclude-standard -- @scanRoots
        if ($LASTEXITCODE -ne 0) {
            return @()
        }

        return @($paths | Where-Object {
                (Test-JavaScriptSyntaxPath $_) -and
                (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf)
            } | ForEach-Object { Join-Path $Root $_ })
    } finally {
        Pop-Location
    }
}

function Get-RecursiveJavaScriptFiles {
    $roots = $scanRoots |
        ForEach-Object { Join-Path $Root $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container }

    return @(Get-ChildItem -Path $roots -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            $relativePath = Convert-ToRepoPath $_.FullName
            if (Test-JavaScriptSyntaxPath $relativePath) {
                $_.FullName
            }
        })
}

$node = Get-Command "node" -ErrorAction SilentlyContinue
if ($null -eq $node) {
    throw "node was not found; JS syntax lint cannot run."
}

$files = Get-GitJavaScriptFiles
if ($files.Count -eq 0) {
    $files = Get-RecursiveJavaScriptFiles
}

$failures = @()
foreach ($file in $files) {
    $output = & $node.Source --check $file 2>&1
    if ($LASTEXITCODE -ne 0) {
        $failures += [pscustomobject]@{
            File = Convert-ToRepoPath $file
            Output = ($output -join "`n").Trim()
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "JS syntax violations found:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host ("  {0}" -f $failure.File) -ForegroundColor Red
        if (-not [string]::IsNullOrWhiteSpace($failure.Output)) {
            Write-Host $failure.Output -ForegroundColor Red
        }
    }
    exit 1
}

Write-Host "JS syntax check passed ($($files.Count) files)."
