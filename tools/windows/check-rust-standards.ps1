param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$CompilerSrc = Join-Path $Root "compiler/src"

function Convert-ToRepoPath {
    param([string]$Path)

    $fullPath = (Resolve-Path -LiteralPath $Path).Path
    return $fullPath.Substring($Root.Length).TrimStart('\', '/') -replace "\\", "/"
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

function Get-RustSourceFiles {
    if (-not (Test-Path -LiteralPath $CompilerSrc)) {
        return @()
    }

    return @(Get-ChildItem -Path $CompilerSrc -Recurse -File -Filter "*.rs")
}

function Test-CliOutputAllowed {
    param(
        [string]$RelativePath
    )

    return $RelativePath -eq "compiler/src/main.rs"
}

function Test-ArtifactOrderingSensitivePath {
    param(
        [string]$RelativePath
    )

    return $RelativePath -match 'compiler/src/(emit|artifact|artifacts|plan|golden|writer|diagnostic|diagnostics)/'
}

$rules = @(
    @{ Rule = "RS001"; Pattern = '\.unwrap\s*\('; Message = "unwrap() is forbidden in production Rust without an explicit allow reason." },
    @{ Rule = "RS002"; Pattern = '\.expect\s*\('; Message = "expect() is forbidden in production Rust without an explicit allow reason." },
    @{ Rule = "RS003"; Pattern = '\btodo!\s*\('; Message = "todo!() is forbidden in production Rust." },
    @{ Rule = "RS004"; Pattern = '\bunimplemented!\s*\('; Message = "unimplemented!() is forbidden in production Rust." },
    @{ Rule = "RS005"; Pattern = '\bpanic!\s*\('; Message = "panic!() is forbidden in production Rust without an explicit allow reason." },
    @{ Rule = "RS006"; Pattern = '\bdbg!\s*\('; Message = "dbg!() is forbidden in Rust code." }
)

$violations = @()

foreach ($file in Get-RustSourceFiles) {
    $relativePath = Convert-ToRepoPath $file.FullName
    $lineNumber = 0
    $pendingCfgTest = $false
    $testModuleDepth = $null
    $braceDepth = 0

    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNumber += 1
        $trimmed = $line.Trim()

        if ($trimmed -match '^#\s*\[\s*cfg\s*\(\s*test\s*\)\s*\]') {
            $pendingCfgTest = $true
        }

        if ($pendingCfgTest -and $trimmed -match '^mod\s+tests\b') {
            $testModuleDepth = $braceDepth
            $pendingCfgTest = $false
        }

        $insideTestModule = $null -ne $testModuleDepth

        if (-not $insideTestModule) {
            foreach ($rule in $rules) {
                if ($line -match $rule.Pattern) {
                    $violations += New-Finding -File $relativePath -Line $lineNumber -Rule $rule.Rule -Message $rule.Message
                }
            }

            if (($line -match '\bprintln!\s*\(' -or $line -match '\beprintln!\s*\(') -and
                -not (Test-CliOutputAllowed $relativePath)) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Rule "RS007" `
                    -Message "println!/eprintln! are allowed only in the CLI entrypoint or with an explicit allow reason."
            }

            if ((Test-ArtifactOrderingSensitivePath $relativePath) -and
                ($line -match '\bHashMap\b' -or $line -match '\bHashSet\b')) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Rule "RS008" `
                    -Message "HashMap/HashSet usage in artifact or diagnostics paths must justify deterministic ordering."
            }
        }

        $opens = ([regex]::Matches($line, "\{")).Count
        $closes = ([regex]::Matches($line, "\}")).Count
        $braceDepth += $opens - $closes

        if ($null -ne $testModuleDepth -and $braceDepth -le $testModuleDepth) {
            $testModuleDepth = $null
        }
    }
}

$goldenRoot = Join-Path $Root "compiler/tests/golden"
if (Test-Path -LiteralPath $goldenRoot) {
    $absolutePathPattern = '([A-Za-z]:\\|/home/|/Users/|/tmp/)'
    foreach ($file in Get-ChildItem -Path $goldenRoot -Recurse -File) {
        $relativePath = Convert-ToRepoPath $file.FullName
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber += 1
            if ($line -match $absolutePathPattern) {
                $violations += New-Finding `
                    -File $relativePath `
                    -Line $lineNumber `
                    -Rule "RS009" `
                    -Message "Golden compiler outputs must not contain absolute local paths unless explicitly normalized and allowed."
            }
        }
    }
}

if ($violations.Count -gt 0) {
    Write-Host "Rust standards violations found:" -ForegroundColor Red
    foreach ($violation in $violations) {
        Write-Host ("  {0}:{1}: {2}: {3}" -f $violation.File, $violation.Line, $violation.Rule, $violation.Message) -ForegroundColor Red
    }
    exit 1
}

Write-Host "Rust standards check passed."
