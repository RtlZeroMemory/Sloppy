param(
    [ValidateSet("pr", "extended", "torture")]
    [string]$Tier = "pr",

    [ValidateSet("all", "static", "native", "compiler", "js", "fuzz", "http2", "package", "sanitizer", "stress", "v8", "provider", "meta")]
    [string]$Area = "all",

    [int]$Seed = 12345,
    [int]$FuzzIterations = 0,
    [int]$StressSeconds = 0,
    [string]$Out = "",
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Tier = $Tier.ToLowerInvariant()
$Area = $Area.ToLowerInvariant()
$StartedAt = (Get-Date).ToUniversalTime()
$FailedLaneCount = 0

function Write-TestEngineHelp {
    Write-Host "Usage: tools/windows/test-engine.ps1 [-Tier pr|extended|torture] [-Area all|static|native|compiler|js|fuzz|http2|package|sanitizer|stress|v8|provider|meta] [-Seed N] [-FuzzIterations N] [-StressSeconds N] [-Out path]"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  tools/windows/test-engine.ps1 -Tier pr"
    Write-Host "  tools/windows/test-engine.ps1 -Tier extended"
    Write-Host "  tools/windows/test-engine.ps1 -Tier torture -FuzzIterations 120000 -StressSeconds 300"
    Write-Host "  tools/windows/test-engine.ps1 -Area fuzz -Tier pr -Seed 12345"
    Write-Host "  tools/windows/test-engine.ps1 -Out artifacts/test-engine/report.json"
}

if ($Help) {
    Write-TestEngineHelp
    exit 0
}

function Get-GitValue {
    param([string[]]$Arguments)

    try {
        $value = & git -C $Root @Arguments 2>$null | Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($value)) {
            return [string]$value
        }
    } catch {
    }

    return ""
}

function Get-HostCpuName {
    try {
        $cpu = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
        if ($null -ne $cpu -and -not [string]::IsNullOrWhiteSpace($cpu.Name)) {
            return [string]$cpu.Name
        }
    } catch {
    }

    return ""
}

$Report = [ordered]@{
    schemaVersion = 1
    tier = $Tier
    area = $Area
    seed = $Seed
    startedAt = $StartedAt.ToString("o")
    git = [ordered]@{
        branch = Get-GitValue -Arguments @("branch", "--show-current")
        commit = Get-GitValue -Arguments @("rev-parse", "HEAD")
    }
    host = [ordered]@{
        os = [System.Runtime.InteropServices.RuntimeInformation]::OSDescription
        arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        cpu = Get-HostCpuName
        logicalCores = [Environment]::ProcessorCount
    }
    lanes = @()
    summary = [ordered]@{
        pass = 0
        fail = 0
        skipped = 0
        unavailable = 0
    }
}

function Add-Lane {
    param(
        [string]$Id,
        [ValidateSet("pass", "fail", "skipped", "unavailable")]
        [string]$Status,
        [int64]$DurationMs,
        [string]$Command,
        [string]$Notes = ""
    )

    $Report.lanes += [ordered]@{
        id = $Id
        status = $Status
        durationMs = $DurationMs
        command = $Command
        notes = $Notes
    }
    $Report.summary[$Status] = [int]$Report.summary[$Status] + 1
    if ($Status -eq "fail") {
        $script:FailedLaneCount += 1
    }
    Write-Host "$Id`t$($Status.ToUpperInvariant())`t$Notes"
}

function Join-CommandText {
    param(
        [string]$File,
        [string[]]$Arguments = @()
    )

    $parts = @($File)
    foreach ($argument in $Arguments) {
        if ($argument -match '\s') {
            $parts += '"' + ($argument -replace '"', '\"') + '"'
        } else {
            $parts += $argument
        }
    }
    return ($parts -join " ")
}

function Resolve-CommandFile {
    param([string]$File)

    if (Test-Path -LiteralPath $File -PathType Leaf) {
        return (Resolve-Path -LiteralPath $File).Path
    }

    $command = Get-Command $File -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        return $null
    }

    return $command.Source
}

function Invoke-ExternalLane {
    param(
        [string]$Id,
        [string]$File,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $Root,
        [string]$UnavailableNote = ""
    )

    $resolved = Resolve-CommandFile -File $File
    $commandText = Join-CommandText $File $Arguments
    if ($null -eq $resolved) {
        $note = if ([string]::IsNullOrWhiteSpace($UnavailableNote)) { "$File is not available" } else { $UnavailableNote }
        Add-Lane $Id "unavailable" 0 $commandText $note
        return
    }

    $previous = (Get-Location).Path
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        Set-Location -LiteralPath $WorkingDirectory
        & $resolved @Arguments
        $exitCode = if ($null -ne $LASTEXITCODE) { [int]$LASTEXITCODE } elseif ($?) { 0 } else { 1 }
    } catch {
        $exitCode = 1
        Write-Host $_.Exception.Message
    } finally {
        Set-Location -LiteralPath $previous
        $stopwatch.Stop()
    }

    if ($exitCode -eq 0) {
        Add-Lane $Id "pass" $stopwatch.ElapsedMilliseconds $commandText ""
    } else {
        Add-Lane $Id "fail" $stopwatch.ElapsedMilliseconds $commandText "exit code $exitCode"
    }
}

function Invoke-PowerShellLane {
    param(
        [string]$Id,
        [string]$Script,
        [string[]]$Arguments = @()
    )

    $powerShell = (Get-Command pwsh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($null -eq $powerShell) {
        $powerShell = Get-Command powershell -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($null -eq $powerShell) {
        Add-Lane $Id "unavailable" 0 "powershell -File $Script" "PowerShell is not available"
        return
    }

    $args = @("-NoProfile")
    if ($powerShell.Source -match "powershell(\.exe)?$") {
        $args += @("-ExecutionPolicy", "Bypass")
    }
    $args += @("-File", $Script)
    $args += $Arguments
    Invoke-ExternalLane $Id $powerShell.Source $args
}

function Invoke-CtestLane {
    param(
        [string]$Id,
        [string]$Preset,
        [string[]]$Arguments = @()
    )

    $buildDir = Join-Path (Join-Path $Root "build") $Preset
    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        Add-Lane $Id "unavailable" 0 "ctest --preset $Preset" "build preset directory does not exist: $buildDir"
        return
    }

    Invoke-ExternalLane $Id "ctest" (@("--preset", $Preset, "--output-on-failure") + $Arguments)
}

function Get-TierIterations {
    if ($FuzzIterations -gt 0) {
        return $FuzzIterations
    }
    switch ($Tier) {
        "pr" { return 1000 }
        "extended" { return 10000 }
        "torture" { return 120000 }
        default { return 1000 }
    }
}

function Get-TierStressSeconds {
    if ($StressSeconds -gt 0) {
        return $StressSeconds
    }
    switch ($Tier) {
        "pr" { return 10 }
        "extended" { return 60 }
        "torture" { return 300 }
        default { return 10 }
    }
}

function Invoke-PowerShellParseCheck {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $errors = @()
    $paths = @()
    foreach ($folder in @("tools", "tools/windows")) {
        $absolute = Join-Path $Root $folder
        if (Test-Path -LiteralPath $absolute -PathType Container) {
            $paths += Get-ChildItem -LiteralPath $absolute -Filter "*.ps1" -File
        }
    }
    foreach ($file in $paths) {
        $tokens = $null
        $parseErrors = $null
        [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
        foreach ($parseError in $parseErrors) {
            $errors += "$($file.FullName):$($parseError.Extent.StartLineNumber): $($parseError.Message)"
        }
    }
    $stopwatch.Stop()
    if ($errors.Count -eq 0) {
        Add-Lane "test-engine.static.powershell_parse" "pass" $stopwatch.ElapsedMilliseconds "PowerShell parser over tools/*.ps1 tools/windows/*.ps1" ""
    } else {
        Add-Lane "test-engine.static.powershell_parse" "fail" $stopwatch.ElapsedMilliseconds "PowerShell parser over tools/*.ps1 tools/windows/*.ps1" ($errors -join "; ")
    }
}

function Invoke-BashSyntaxCheck {
    $bash = Get-Command bash -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $bash) {
        Add-Lane "test-engine.static.bash_syntax" "unavailable" 0 "bash -n tools/unix/*.sh" "bash is not available"
        return
    }
    $scripts = @(Get-ChildItem -LiteralPath (Join-Path $Root "tools/unix") -Filter "*.sh" -File)
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    foreach ($script in $scripts) {
        $relative = [System.IO.Path]::GetRelativePath($Root, $script.FullName).Replace("\", "/")
        $previous = (Get-Location).Path
        try {
            Set-Location -LiteralPath $Root
            & $bash.Source "-n" $relative
        } finally {
            Set-Location -LiteralPath $previous
        }
        if ($LASTEXITCODE -ne 0) {
            $stopwatch.Stop()
            Add-Lane "test-engine.static.bash_syntax" "fail" $stopwatch.ElapsedMilliseconds "bash -n tools/unix/*.sh" "$($script.Name) failed syntax check"
            return
        }
    }
    $stopwatch.Stop()
    Add-Lane "test-engine.static.bash_syntax" "pass" $stopwatch.ElapsedMilliseconds "bash -n tools/unix/*.sh" ""
}

function Invoke-NodeSyntaxCheck {
    $node = Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $node) {
        Add-Lane "test-engine.static.node_check" "unavailable" 0 "node --check <tracked js/mjs>" "node is not available"
        return
    }
    $files = @(& git -C $Root ls-files -- "*.js" "*.mjs")
    if ($LASTEXITCODE -ne 0) {
        Add-Lane "test-engine.static.node_check" "fail" 0 "git ls-files -- *.js *.mjs" "git ls-files failed"
        return
    }
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    foreach ($file in $files) {
        & $node.Source "--check" (Join-Path $Root $file)
        if ($LASTEXITCODE -ne 0) {
            $stopwatch.Stop()
            Add-Lane "test-engine.static.node_check" "fail" $stopwatch.ElapsedMilliseconds "node --check <tracked js/mjs>" "$file failed syntax check"
            return
        }
    }
    $stopwatch.Stop()
    Add-Lane "test-engine.static.node_check" "pass" $stopwatch.ElapsedMilliseconds "node --check <tracked js/mjs>" ""
}

function Invoke-AddedLineGuardrails {
    $base = Get-GitValue -Arguments @("merge-base", "HEAD", "origin/main")
    if ([string]::IsNullOrWhiteSpace($base)) {
        Add-Lane "test-engine.static.guardrails" "skipped" 0 "git diff --unified=0 <merge-base>" "origin/main merge-base was not available"
        return
    }

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $diff = @(& git -C $Root diff --unified=0 $base -- docs README.md .github tools src include tests compiler stdlib examples)
    if ($LASTEXITCODE -ne 0) {
        $stopwatch.Stop()
        Add-Lane "test-engine.static.guardrails" "fail" $stopwatch.ElapsedMilliseconds "git diff --unified=0 $base -- <guarded paths>" "git diff failed"
        return
    }

    $violations = New-Object System.Collections.Generic.List[string]
    $goalPhrase = "/" + "goal"
    $releasePhrase = "auto-generated" + " release notes"
    $generatedPhrase = "generated" + " with"
    $legacyAgentPhrase = "Claude" + " Code"
    $codexToken = "CO" + "DEX"
    $blockedPhrasePattern = "(?i)(" + [regex]::Escape($goalPhrase) + "|" +
        [regex]::Escape($releasePhrase) + "|" + [regex]::Escape($generatedPhrase) +
        "|blazing fast|production ready|" + [regex]::Escape($legacyAgentPhrase) +
        "|\b" + [regex]::Escape($codexToken) + "\b)"
    $path = ""
    foreach ($line in $diff) {
        if ($line -match '^\+\+\+ b/(.+)$') {
            $path = $matches[1]
            continue
        }
        if (-not $line.StartsWith("+") -or $line.StartsWith("+++")) {
            continue
        }
        $added = $line.Substring(1)
        if ($path -notmatch '^(AGENTS\.md|AGENTS_CONTRIBUTING\.md|docs/archive/)' -and
            $added -match $blockedPhrasePattern) {
            $violations.Add("${path}: disallowed generated/prompt-leakage wording")
        }
        if ($path -match '^(src|include|tests)/(.*\.(c|h|cc|cpp|hpp))$' -and
            $added -match '\b(malloc|free|memcpy|memmove)\s*\(') {
            $violations.Add("${path}: added direct allocation/copy pattern needs review")
        }
        if ($path -match '^include/' -and $added -match '\bv8::|<v8\.h>|libuv|uv_') {
            $violations.Add("${path}: public header exposes V8/libuv detail")
        }
    }
    $stopwatch.Stop()

    if ($violations.Count -gt 0) {
        Add-Lane "test-engine.static.guardrails" "fail" $stopwatch.ElapsedMilliseconds "git diff --unified=0 $base -- <guarded paths>" (($violations | Select-Object -Unique) -join "; ")
    } else {
        Add-Lane "test-engine.static.guardrails" "pass" $stopwatch.ElapsedMilliseconds "git diff --unified=0 $base -- <guarded paths>" ""
    }
}

function Invoke-StaticArea {
    $start = $Report.lanes.Count
    Invoke-ExternalLane "test-engine.static.git_diff_check" "git" @("-C", $Root, "diff", "--check")
    Invoke-PowerShellLane "test-engine.static.c_standards_selftest" (Join-Path $Root "tools/windows/check-c-standards.ps1") @("-SelfTest")
    Invoke-PowerShellLane "test-engine.static.c_standards" (Join-Path $Root "tools/windows/check-c-standards.ps1")
    Invoke-PowerShellLane "test-engine.static.js_ts_standards" (Join-Path $Root "tools/windows/check-js-ts-standards.ps1")
    Invoke-PowerShellLane "test-engine.static.rust_standards" (Join-Path $Root "tools/windows/check-rust-standards.ps1")
    Invoke-PowerShellLane "test-engine.static.docs_freshness" (Join-Path $Root "tools/windows/check-docs-freshness.ps1")
    Invoke-PowerShellParseCheck
    Invoke-BashSyntaxCheck
    Invoke-NodeSyntaxCheck
    Invoke-AddedLineGuardrails
    Invoke-ExternalLane "test-engine.static.cargo_fmt" "cargo" @("fmt", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml"), "--", "--check") -UnavailableNote "cargo is not available"
    Invoke-ExternalLane "test-engine.static.cargo_clippy" "cargo" @("clippy", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml"), "--all-targets", "--", "-D", "warnings") -UnavailableNote "cargo is not available"
    Invoke-ExternalLane "test-engine.static.cargo_test" "cargo" @("test", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml")) -UnavailableNote "cargo is not available"

    $slice = @($Report.lanes[$start..($Report.lanes.Count - 1)])
    $status = "pass"
    if (@($slice | Where-Object { $_.status -eq "fail" }).Count -gt 0) {
        $status = "fail"
    } elseif (@($slice | Where-Object { $_.status -eq "unavailable" }).Count -gt 0) {
        $status = "unavailable"
    } elseif (@($slice | Where-Object { $_.status -eq "skipped" }).Count -gt 0) {
        $status = "skipped"
    }
    Add-Lane "test-engine.static" $status 0 "static guardrail aggregate" "aggregate over $($slice.Count) static checks"
}

function Invoke-NativeArea {
    Invoke-CtestLane "native.unit" "windows-dev" @("-R", "^(core\.|data\.|conformance\.(foundation|http|sqlite|data|capability|net|transport)|smoke\.transport)")
}

function Invoke-CompilerArea {
    Invoke-ExternalLane "compiler.cargo_tests" "cargo" @("test", "--manifest-path", (Join-Path $Root "compiler/Cargo.toml")) -UnavailableNote "cargo is not available"
    Invoke-CtestLane "compiler.ctest_fixtures" "windows-dev" @("-R", "compiler|source_input")
}

function Invoke-JsArea {
    $iterations = Get-TierIterations
    $property = Join-Path $Root "tests/bootstrap/property/run_property_tests.mjs"
    Invoke-ExternalLane "js.property" "node" @($property, "--seed", [string]$Seed, "--iterations", [string]$iterations) -UnavailableNote "node is not available"
    Invoke-CtestLane "js.bootstrap" "windows-dev" @("-R", "bootstrap\.stdlib")
}

function Invoke-FuzzArea {
    $iterations = Get-TierIterations
    Invoke-PowerShellLane "fuzz.runner" (Join-Path $Root "tools/windows/fuzz.ps1") @("-Tier", $Tier, "-All", "-Iterations", [string]$iterations, "-Seed", [string]$Seed)
}

function Invoke-Http2Area {
    Invoke-PowerShellLane "http2.conformance" (Join-Path $Root "tools/windows/test-http2.ps1") @("-Preset", "windows-dev")
}

function Invoke-PackageArea {
    $packageRoot = Join-Path $Root "artifacts/packages"
    $packages = @()
    if (Test-Path -LiteralPath $packageRoot -PathType Container) {
        $packages = @(Get-ChildItem -LiteralPath $packageRoot -Filter "sloppy-*.zip" -File | Sort-Object LastWriteTimeUtc -Descending)
    }
    if ($packages.Count -eq 0) {
        Add-Lane "package.outside_checkout" "skipped" 0 "tools/windows/dev.ps1 test-package" "no package archive found under artifacts/packages"
        return
    }
    Invoke-PowerShellLane "package.outside_checkout" (Join-Path $Root "tools/windows/dev.ps1") @("test-package", "-PackagePath", $packages[0].FullName)
}

function Invoke-SanitizerArea {
    $artifactRoot = Join-Path $Root "artifacts/test-engine/sanitizers"
    New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
    Invoke-PowerShellLane "sanitizer.windows_asan.configure" (Join-Path $Root "tools/windows/dev.ps1") @("configure", "-Preset", "windows-asan")
    Invoke-PowerShellLane "sanitizer.windows_asan.build" (Join-Path $Root "tools/windows/dev.ps1") @("build", "-Preset", "windows-asan")
    Invoke-ExternalLane "sanitizer.windows_asan.ctest" "ctest" @("--preset", "windows-asan", "--output-on-failure")
    Invoke-PowerShellLane "sanitizer.windows_libfuzzer.configure" (Join-Path $Root "tools/windows/dev.ps1") @("configure", "-Preset", "windows-libfuzzer")
    Invoke-PowerShellLane "sanitizer.windows_libfuzzer.build" (Join-Path $Root "tools/windows/dev.ps1") @("build", "-Preset", "windows-libfuzzer")
    Invoke-ExternalLane "sanitizer.windows_libfuzzer.seed_replay" "ctest" @("--preset", "windows-libfuzzer", "-L", "fuzz", "--output-on-failure")
}

function Invoke-StressArea {
    $seconds = Get-TierStressSeconds
    Invoke-CtestLane "stress.ctest_smoke" "windows-dev" @("-R", "stress\.")
    Add-Lane "stress.budget" "pass" 0 "stress budget" "tier $Tier uses ${seconds}s default stress budget for manual stress helpers"
}

function Invoke-V8Area {
    if ([string]::IsNullOrWhiteSpace($env:SLOPPY_V8_ROOT)) {
        Add-Lane "v8.gated" "unavailable" 0 "tools/windows/dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8" "SLOPPY_V8_ROOT is not set"
        return
    }
    Invoke-PowerShellLane "v8.configure" (Join-Path $Root "tools/windows/dev.ps1") @("configure", "-Preset", "windows-relwithdebinfo", "-EnableV8", "-V8Root", $env:SLOPPY_V8_ROOT)
    Invoke-PowerShellLane "v8.build" (Join-Path $Root "tools/windows/dev.ps1") @("build", "-Preset", "windows-relwithdebinfo")
    Invoke-PowerShellLane "v8.test" (Join-Path $Root "tools/windows/dev.ps1") @("test", "-Preset", "windows-relwithdebinfo")
}

function Invoke-ProviderArea {
    Invoke-CtestLane "provider.default" "windows-dev" @("-R", "data\.|conformance\.(sqlite|postgres|sqlserver)")
}

function Invoke-MetaArea {
    Invoke-PowerShellLane "test-engine.meta.help" (Join-Path $Root "tools/windows/test-engine.ps1") @("-Help")
    Invoke-PowerShellLane "test-engine.meta.fuzz_help" (Join-Path $Root "tools/windows/fuzz.ps1") @("-Help")
}

function Should-Run {
    param([string]$Name)
    return $Area -eq "all" -or $Area -eq $Name
}

if (Should-Run "meta") {
    Invoke-MetaArea
}
if (Should-Run "static") {
    Invoke-StaticArea
}
if (Should-Run "native") {
    Invoke-NativeArea
}
if (Should-Run "compiler") {
    Invoke-CompilerArea
}
if (Should-Run "js") {
    Invoke-JsArea
}
if (Should-Run "fuzz") {
    Invoke-FuzzArea
}
if (Should-Run "http2") {
    Invoke-Http2Area
}
if (Should-Run "stress") {
    Invoke-StressArea
}
if ($Area -eq "package" -or ($Area -eq "all" -and $Tier -ne "pr")) {
    Invoke-PackageArea
}
if ($Area -eq "sanitizer" -or ($Area -eq "all" -and $Tier -ne "pr")) {
    Invoke-SanitizerArea
}
if ($Area -eq "v8" -or ($Area -eq "all" -and $Tier -eq "torture")) {
    Invoke-V8Area
}
if ($Area -eq "provider" -or ($Area -eq "all" -and $Tier -ne "pr")) {
    Invoke-ProviderArea
}

$Report.finishedAt = (Get-Date).ToUniversalTime().ToString("o")

if (-not [string]::IsNullOrWhiteSpace($Out)) {
    $outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $Root $Out }
    $outDir = Split-Path -Parent $outPath
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $Report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outPath -Encoding UTF8
    Write-Host "test-engine report: $outPath"
}

if ($FailedLaneCount -gt 0) {
    exit 1
}

exit 0
