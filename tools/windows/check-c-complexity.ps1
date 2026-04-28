param(
    [int]$FunctionLineThreshold = 120,
    [int]$ParameterThreshold = 6,
    [int]$MacroThreshold = 40
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Paths = @(
    (Join-Path $Root "include"),
    (Join-Path $Root "src")
)

$files = Get-ChildItem -Path $Paths -Recurse -File |
    Where-Object { $_.Extension -in @(".c", ".h", ".cc", ".cpp", ".hpp") }

$warnings = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
    $relative = Resolve-Path -LiteralPath $file.FullName -Relative
    $lines = Get-Content -LiteralPath $file.FullName
    $macroCount = @($lines | Where-Object { $_ -match "^\s*#\s*define\b" }).Count

    if ($macroCount -gt $MacroThreshold) {
        $warnings.Add("$relative has $macroCount macros; review for macro-heavy design.") | Out-Null
    }

    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -notmatch "^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s+)+\*?\s*(sl_[A-Za-z0-9_]+)\s*\((.*)\)\s*\{?\s*$") {
            continue
        }

        $functionName = $matches[1]
        $paramText = $matches[2].Trim()
        if ($paramText -ne "void" -and $paramText.Length -gt 0) {
            $paramCount = @($paramText -split "," | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count
            if ($paramCount -gt $ParameterThreshold) {
                $lineNumber = $index + 1
                $warnings.Add("${relative}:$lineNumber $functionName has $paramCount parameters; review API width.") | Out-Null
            }
        }

        $braceDepth = 0
        $sawOpenBrace = $false
        for ($cursor = $index; $cursor -lt $lines.Count; $cursor++) {
            $chars = $lines[$cursor].ToCharArray()
            foreach ($char in $chars) {
                if ($char -eq "{") {
                    $braceDepth++
                    $sawOpenBrace = $true
                } elseif ($char -eq "}") {
                    $braceDepth--
                }
            }

            if ($sawOpenBrace -and $braceDepth -le 0) {
                $functionLines = $cursor - $index + 1
                if ($functionLines -gt $FunctionLineThreshold) {
                    $lineNumber = $index + 1
                    $warnings.Add("${relative}:$lineNumber $functionName is $functionLines lines; review function responsibilities.") | Out-Null
                }
                break
            }
        }
    }
}

if ($warnings.Count -eq 0) {
    Write-Host "C complexity warning scan completed with no warnings."
    exit 0
}

Write-Warning "C complexity warning scan found $($warnings.Count) item(s). These are not hard failures."
foreach ($warning in $warnings) {
    Write-Warning $warning
}
