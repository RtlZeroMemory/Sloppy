param(
    [switch]$SelfTest,
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-ToRepoPath {
    param([string]$Path)

    $relative = [System.IO.Path]::GetRelativePath($Root, $Path)
    return $relative.Replace("\", "/")
}

function Get-TrackedFileSet {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($null -ne $git) {
        $files = & git -C $Root ls-files --cached --others --exclude-standard 2>$null
        if ($LASTEXITCODE -eq 0) {
            return @($files | Where-Object {
                $_ -match "\.(c|h|cc|cpp|js|mjs|ts|md|json|cmake|ps1|sh|rs)$"
            } | ForEach-Object {
                [pscustomobject]@{
                    Path = $_.Replace("\", "/")
                    Content = Get-Content -LiteralPath (Join-Path $Root $_) -Raw
                }
            })
        }
    }

    return @(Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        [pscustomobject]@{
            Path = Convert-ToRepoPath $_.FullName
            Content = Get-Content -LiteralPath $_.FullName -Raw
        }
    })
}

function Test-CoreApiIntegrationFileSet {
    param([object[]]$Files)

    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($file in $Files) {
        $path = [string]$file.Path
        $content = [string]$file.Content

        if ($path -match "^stdlib/sloppy/" -and
            $path -ne "stdlib/sloppy/codec.js" -and
            $path -ne "stdlib/sloppy/internal/runtime-classic.js" -and
            $content -match "new\s+Text(?:Encoder|Decoder)\s*\(")
        {
            $violations.Add("$path uses TextEncoder/TextDecoder directly; route text conversions through sloppy/codec Text.")
        }

        if ($path -match "^stdlib/sloppy/" -and
            $path -ne "stdlib/sloppy/internal/runtime-classic.js" -and
            $content -match "(?m)^\s*(?:export\s+)?(?:function|const|let|var)\s+(?:utf8ToBytes|bytesToHex|bytesToBase64)\b")
        {
            $violations.Add("$path defines local UTF-8/hex/base64 helpers; use sloppy/codec instead.")
        }

        if ((($path -match "^(?:src|include)/.*\.(?:c|h|cc|cpp)$") -and
             $path -ne "src/core/string.c" -and
             $content -match "static\s+bool\s+sl_[A-Za-z0-9_]*ascii[A-Za-z0-9_]*(?:equal|starts|ends)[A-Za-z0-9_]*ci") -or
            (($path -match "^src/core/http_.*\.c$" -or $path -eq "src/platform/libuv/http_transport_libuv.c") -and
             $content -match "sl_http_(?:dispatch|backend|response|transport)_(?:ascii_lower|str_i(?:equal|starts_with|ends_with))\b"))
        {
            $violations.Add("$path defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers.")
        }

        if ($path -match "^stdlib/sloppy/" -and
            $path -ne "stdlib/sloppy/internal/runtime-classic.js" -and
            $content -match "\bMath\.random\s*\(")
        {
            $violations.Add("$path uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception.")
        }
    }

    return $violations
}

if ($SelfTest) {
    $fixtures = @(
        [pscustomobject]@{
            Path = "stdlib/sloppy/fs.js"
            Content = "const bytes = new TextEncoder().encode('x');"
        },
        [pscustomobject]@{
            Path = "stdlib/sloppy/codec.js"
            Content = "const owner = new TextEncoder();"
        },
        [pscustomobject]@{
            Path = "stdlib/sloppy/internal/runtime-classic.js"
            Content = "function bytesToHex(bytes) { return bytes; } const text = new TextDecoder();"
        },
        [pscustomobject]@{
            Path = "stdlib/sloppy/net.js"
            Content = "export const bytesToHex = (bytes) => bytes;"
        },
        [pscustomobject]@{
            Path = "stdlib/sloppy/workers.js"
            Content = "const id = Math.random();"
        },
        [pscustomobject]@{
            Path = "stdlib/sloppy/crypto.js"
            Content = "const id = Math.random();"
        },
        [pscustomobject]@{
            Path = "src/core/string.c"
            Content = "static bool sl_str_ascii_equal_ci(char a, char b) { return a == b; }"
        },
        [pscustomobject]@{
            Path = "src/core/http_backend.c"
            Content = "static bool sl_http_backend_str_iequal(SlStr left, SlStr right) { return true; }"
        },
        [pscustomobject]@{
            Path = "src/core/diagnostics.c"
            Content = "static bool sl_diag_ascii_equal_ci(char actual, char expected) { return actual == expected; }"
        }
    )
    $expected = @(
        "stdlib/sloppy/fs.js uses TextEncoder/TextDecoder directly; route text conversions through sloppy/codec Text.",
        "stdlib/sloppy/net.js defines local UTF-8/hex/base64 helpers; use sloppy/codec instead.",
        "stdlib/sloppy/workers.js uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception.",
        "stdlib/sloppy/crypto.js uses Math.random for runtime API behavior; use sloppy/crypto Random or document a non-security exception.",
        "src/core/http_backend.c defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers.",
        "src/core/diagnostics.c defines a local ASCII comparison helper; use sloppy/string case-insensitive helpers."
    ) | Sort-Object
    $violations = @(Test-CoreApiIntegrationFileSet $fixtures | Sort-Object)
    $diff = @(Compare-Object -ReferenceObject $expected -DifferenceObject $violations)
    if ($diff.Count -ne 0) {
        Write-Error "core API integration scanner self-test mismatch. Expected: $($expected -join '; ') Actual: $($violations -join '; ')"
        exit 1
    }
    Write-Host "core API integration scanner self-test passed."
    exit 0
}

$violations = @(Test-CoreApiIntegrationFileSet (Get-TrackedFileSet))
if ($violations.Count -gt 0) {
    Write-Error "Core API integration check failed:`n$($violations -join "`n")"
    exit 1
}

Write-Host "core API integration check passed."
