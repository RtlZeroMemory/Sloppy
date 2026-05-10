param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

$excludedPrefixes = @(
    "build/",
    ".sdeps/",
    "compiler/target/",
    "node_modules/",
    ".git/"
)

$scanPrefixes = @(
    "stdlib/",
    "examples/",
    "compiler/tests/fixtures/",
    "tests/fixtures/",
    "tests/golden/"
)

$sourceExtensions = @(".js", ".mjs", ".ts", ".tsx", ".json", ".md")
$packageManagerFiles = @(
    "package.json",
    "package-lock.json",
    "npm-shrinkwrap.json",
    "yarn.lock",
    "pnpm-lock.yaml",
    "bun.lock",
    "bun.lockb"
)

function Convert-ToRepoPath {
    param([string]$Path)

    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    return $fullPath.Substring($Root.Length).TrimStart('\', '/') -replace "\\", "/"
}

function Test-ExcludedPath {
    param([string]$RelativePath)

    foreach ($prefix in $excludedPrefixes) {
        if ($RelativePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Test-ScannedPath {
    param([string]$RelativePath)

    foreach ($prefix in $scanPrefixes) {
        if ($RelativePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Test-SourcePath {
    param([string]$RelativePath)

    if ((Test-ExcludedPath $RelativePath) -or -not (Test-ScannedPath $RelativePath)) {
        return $false
    }

    $fileName = [System.IO.Path]::GetFileName($RelativePath)
    if ($fileName -in $packageManagerFiles) {
        return $true
    }

    return [System.IO.Path]::GetExtension($RelativePath) -in $sourceExtensions
}

function Get-TrackedSourceFiles {
    $git = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        return @()
    }

    Push-Location $Root
    try {
        $tracked = & $git.Source ls-files -- stdlib examples compiler/tests/fixtures tests/fixtures tests/golden
        if ($LASTEXITCODE -ne 0) {
            return @()
        }

        return @($tracked | Where-Object {
                (Test-SourcePath $_) -and
                (Test-Path -LiteralPath (Join-Path $Root $_))
            } | ForEach-Object { Join-Path $Root $_ })
    } finally {
        Pop-Location
    }
}

function Get-RecursiveSourceFiles {
    $roots = @("stdlib", "examples", "compiler/tests/fixtures", "tests/fixtures", "tests/golden") |
        ForEach-Object { Join-Path $Root $_ } |
        Where-Object { Test-Path -LiteralPath $_ }

    return @(Get-ChildItem -Path $roots -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            $relativePath = Convert-ToRepoPath $_.FullName
            if (Test-SourcePath $relativePath) {
                $_.FullName
            }
        })
}

function New-Finding {
    param(
        [string]$File,
        [int]$Line,
        [string]$Rule,
        [string]$Message
    )

    [pscustomobject]@{
        File = $File
        Line = $Line
        Rule = $Rule
        Message = $Message
    }
}

function Test-AllowedStdlibGlobalWrite {
    param(
        [string]$RelativePath,
        [string]$Line
    )

    if ($RelativePath -eq "stdlib/sloppy/internal/runtime-classic.js" -and
        $Line -match '^\s*globalThis\.__sloppy_runtime\s*=')
    {
        return $true
    }

    return $false
}

function Test-AllowedStdlibDynamicImport {
    param(
        [string]$RelativePath,
        [string]$Line
    )

    if ($RelativePath -eq "stdlib/sloppy/workers.js" -and
        $Line -match '^\s*const\s+moduleNamespace\s+=\s+await\s+import\s*\(\s*modulePath\s*\)\s*;')
    {
        return $true
    }

    return $false
}

function Test-AllowedNodeCompatStdlibRule {
    param(
        [string]$RelativePath,
        [string]$Rule
    )

    if (-not $RelativePath.StartsWith("stdlib/sloppy/node/", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }

    if ($Rule -in @("JS001", "JS002", "JS003", "JS008", "JS009")) {
        return $true
    }

    return $false
}

function Test-AllowedExamplePackageManagerFile {
    param([string]$RelativePath)

    return $RelativePath -in @(
        "examples/dependency-graph/package.json",
        "examples/dependency-graph/fixtures/graph-helper/package.json",
        "examples/package-zod-like/package.json",
        "examples/package-zod-like/fixtures/zod-like/package.json"
    )
}

function Test-AllowedGeneralDynamicImport {
    param([string]$RelativePath)

    return $RelativePath.StartsWith("examples/dynamic-module-include/", [System.StringComparison]::OrdinalIgnoreCase)
}

$files = Get-TrackedSourceFiles
if ($files.Count -eq 0) {
    $files = Get-RecursiveSourceFiles
}

$violations = @()

$stdlibRules = @(
    @{ Rule = "JS001"; Pattern = '(^|[^\w$.])require\s*\(\s*["'']'; Message = "CommonJS require is forbidden in bootstrap stdlib." },
    @{ Rule = "JS002"; Pattern = '\bprocess\.'; Message = "Node process global is forbidden in bootstrap stdlib." },
    @{ Rule = "JS003"; Pattern = '\bBuffer\b'; Message = "Node Buffer global is forbidden in bootstrap stdlib." },
    @{ Rule = "JS004"; Pattern = '(^|\s)import\s+.*["''](?:node:)?fs["'']|(^|[^\w$.])require\s*\(\s*["''](?:node:)?fs["'']\s*\)'; Message = "Node fs usage is forbidden in bootstrap stdlib." },
    @{ Rule = "JS005"; Pattern = '(^|\s)import\s+.*["''](?:node:)?path["'']|(^|[^\w$.])require\s*\(\s*["''](?:node:)?path["'']\s*\)'; Message = "Node path usage is forbidden in bootstrap stdlib." },
    @{ Rule = "JS006"; Pattern = '\b__dirname\b'; Message = "__dirname is forbidden in bootstrap stdlib." },
    @{ Rule = "JS007"; Pattern = '\b__filename\b'; Message = "__filename is forbidden in bootstrap stdlib." },
    @{ Rule = "JS008"; Pattern = '\bmodule\.exports\b'; Message = "CommonJS module.exports is forbidden in bootstrap stdlib." },
    @{ Rule = "JS009"; Pattern = '(^|[^\w$])exports\.'; Message = "CommonJS exports.* is forbidden in bootstrap stdlib." },
    @{ Rule = "JS010"; Pattern = '\bimport\s*\('; Message = "Dynamic import is forbidden unless a future scoped task allows it." },
    @{ Rule = "JS011"; Pattern = '\b(window|document|HTMLElement|customElements|localStorage|sessionStorage)\b'; Message = "Browser DOM APIs are forbidden in bootstrap stdlib." }
)

$generalRules = @(
    @{ Rule = "JS020"; Pattern = '\bimport\s*\('; Message = "Dynamic import is forbidden unless explicitly scoped." },
    @{ Rule = "JS021"; Pattern = '\bmodule\.exports\b'; Message = "CommonJS module.exports is forbidden in JS/TS examples and fixtures." },
    @{ Rule = "JS022"; Pattern = '(^|[^\w$])exports\.'; Message = "CommonJS exports.* is forbidden in JS/TS examples and fixtures." }
)

$topLevelStdlibSideEffects = @(
    @{ Rule = "JS040"; Pattern = '^\s*(setTimeout|setInterval|fetch)\s*\('; Message = "Obvious top-level runtime side effects are forbidden in bootstrap stdlib." },
    @{ Rule = "JS041"; Pattern = '^\s*(globalThis|window|document)\s*\.'; Message = "Top-level global mutation is forbidden in bootstrap stdlib." }
)

foreach ($file in $files) {
    $relativePath = Convert-ToRepoPath $file
    $fileName = [System.IO.Path]::GetFileName($relativePath)

    if ($relativePath.StartsWith("examples/", [System.StringComparison]::OrdinalIgnoreCase) -and
        $fileName -in $packageManagerFiles -and
        -not (Test-AllowedExamplePackageManagerFile -RelativePath $relativePath)) {
        $violations += New-Finding `
            -File $relativePath `
            -Line 1 `
            -Rule "JS050" `
            -Message "Package-manager files are forbidden under examples unless a future scoped policy allows them."
    }

    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $file) {
        $lineNumber += 1

        if ($relativePath -eq "stdlib/sloppy/bootstrap.manifest.json") {
            continue
        }

        if ($relativePath.StartsWith("stdlib/sloppy/", [System.StringComparison]::OrdinalIgnoreCase)) {
            foreach ($rule in $stdlibRules) {
                if ($line -match $rule.Pattern) {
                    if (Test-AllowedNodeCompatStdlibRule -RelativePath $relativePath -Rule $rule.Rule) {
                        continue
                    }
                    if ($rule.Rule -eq "JS010" -and
                        (Test-AllowedStdlibDynamicImport -RelativePath $relativePath -Line $line))
                    {
                        continue
                    }
                    $violations += New-Finding -File $relativePath -Line $lineNumber -Rule $rule.Rule -Message $rule.Message
                }
            }

            if ($line -notmatch '^\s*(const|let|var|function|class|export|import|//|/\*|\*|\}|$)') {
                foreach ($rule in $topLevelStdlibSideEffects) {
                    if (($line -match $rule.Pattern) -and
                        -not (Test-AllowedStdlibGlobalWrite -RelativePath $relativePath -Line $line))
                    {
                        $violations += New-Finding -File $relativePath -Line $lineNumber -Rule $rule.Rule -Message $rule.Message
                    }
                }
            }
        } else {
            foreach ($rule in $generalRules) {
                if ($line -match $rule.Pattern) {
                    if ($rule.Rule -eq "JS020" -and
                        (Test-AllowedGeneralDynamicImport -RelativePath $relativePath))
                    {
                        continue
                    }
                    $violations += New-Finding -File $relativePath -Line $lineNumber -Rule $rule.Rule -Message $rule.Message
                }
            }

            if ($line -match '(?i)\b(password|pwd|secret|token)\s*=\s*([^;,\s"''}]+)') {
                $secretValue = $Matches[2]
                if ($secretValue -notmatch '^(<secret>|<redacted>|<password>|xxxx|xxx|\*\*\*)$') {
                    $violations += New-Finding `
                        -File $relativePath `
                        -Line $lineNumber `
                        -Rule "JS030" `
                        -Message "Examples and fixtures must not contain obvious unredacted secret-looking assignments."
                }
            }
        }
    }
}

if ($violations.Count -gt 0) {
    Write-Host "JS/TS standards violations found:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Host ("  {0}:{1}: {2}: {3}" -f $violation.File, $violation.Line, $violation.Rule, $violation.Message) -ForegroundColor Red
    }
    exit 1
}

Write-Host "JS/TS standards check passed."
