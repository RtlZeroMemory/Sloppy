param(
    [ValidateSet("pr", "extended", "torture")]
    [string]$Tier = "pr",

    [string]$Target = "",
    [int]$Iterations = 0,
    [int]$Seed = 12345,
    [switch]$All,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Tier = $Tier.ToLowerInvariant()
$FailureRoot = Join-Path $Root "artifacts/fuzz/failures"
$Failed = 0

$NativeTargets = @{
    "plan" = @{ CTest = "fuzz\.plan_parse\.seed_replay"; Corpus = "plan"; Exe = "fuzz_plan_parse_libfuzzer.exe" }
    "route-pattern" = @{ CTest = "fuzz\.route_pattern\.seed_replay"; Corpus = "route-pattern"; Exe = "fuzz_route_pattern_libfuzzer.exe" }
    "http-request" = @{ CTest = "fuzz\.http_request\.seed_replay"; Corpus = "http-request"; Exe = "fuzz_http_request_libfuzzer.exe" }
    "http-query" = @{ CTest = "fuzz\.http_query\.seed_replay"; Corpus = "http-query"; Exe = "fuzz_http_query_libfuzzer.exe" }
    "http2-frame" = @{ CTest = "fuzz\.http2_frame\.seed_replay"; Corpus = "http2-frame"; Exe = "fuzz_http2_frame_libfuzzer.exe" }
    "http2-hpack" = @{ CTest = "fuzz\.http2_hpack\.seed_replay"; Corpus = "http2-hpack"; Exe = "fuzz_http2_hpack_libfuzzer.exe" }
    "http2-session" = @{ CTest = "fuzz\.http2_session\.seed_replay"; Corpus = "http2-session"; Exe = "fuzz_http2_session_libfuzzer.exe" }
    "diagnostics-render" = @{ CTest = "fuzz\.diagnostics_render\.seed_replay"; Corpus = "diagnostics-render"; Exe = "fuzz_diagnostics_render_libfuzzer.exe" }
    "memory-primitives" = @{ CTest = "fuzz\.memory_primitives\.seed_replay"; Corpus = "memory-primitives"; Exe = "fuzz_memory_primitives_libfuzzer.exe" }
}

$JsTargets = @(
    "config-json",
    "openapi-plan",
    "headers",
    "query-string",
    "percent-decoding",
    "logging-json",
    "package-manifest",
    "route-table",
    "required-features",
    "http-client-options",
    "results-headers",
    "worker-queue"
)

function Write-FuzzHelp {
    Write-Host "Usage: tools/windows/fuzz.ps1 [-Tier pr|extended|torture] [-Target name|-All] [-Iterations N] [-Seed N]"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  tools/windows/fuzz.ps1 -Tier pr"
    Write-Host "  tools/windows/fuzz.ps1 -Target http2-frame -Iterations 10000 -Seed 123"
    Write-Host "  tools/windows/fuzz.ps1 -All -Iterations 120000"
    Write-Host ""
    Write-Host "Native targets: $($NativeTargets.Keys -join ', ')"
    Write-Host "JS/random targets: $($JsTargets -join ', ')"
}

if ($Help) {
    Write-FuzzHelp
    exit 0
}

if ($All -and -not [string]::IsNullOrWhiteSpace($Target)) {
    [Console]::Error.WriteLine("fuzz: use either -All or -Target, not both.")
    exit 2
}

if (-not $All -and [string]::IsNullOrWhiteSpace($Target)) {
    $All = $true
}

function Get-Iterations {
    if ($Iterations -gt 0) {
        return $Iterations
    }
    switch ($Tier) {
        "pr" { return 1000 }
        "extended" { return 10000 }
        "torture" { return 120000 }
        default { return 1000 }
    }
}

function Write-Status {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Detail
    )

    Write-Host "$Name`t$Status`t$Detail"
    if ($Status -eq "FAIL") {
        $script:Failed += 1
    }
}

function Invoke-CtestFuzz {
    param(
        [string]$Name,
        [string[]]$Arguments
    )

    $buildDir = Join-Path $Root "build/windows-dev"
    if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
        Write-Status $Name "UNAVAILABLE" "build/windows-dev does not exist; configure and build first"
        return
    }
    ctest --preset windows-dev --output-on-failure @Arguments
    if ($LASTEXITCODE -eq 0) {
        Write-Status $Name "PASS" "seed replay passed"
    } else {
        Write-Status $Name "FAIL" "ctest exited with code $LASTEXITCODE"
    }
}

function Invoke-LibFuzzerTarget {
    param(
        [string]$Name,
        [hashtable]$Spec,
        [int]$Runs
    )

    $exe = Join-Path $Root ("build/windows-libfuzzer/" + $Spec.Exe)
    $corpus = Join-Path $Root ("tests/fuzz/corpus/" + $Spec.Corpus)
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        Write-Status "fuzz.$Name.mutation" "UNAVAILABLE" "windows-libfuzzer target is not built: $exe"
        return
    }
    if (-not (Test-Path -LiteralPath $corpus -PathType Container)) {
        Write-Status "fuzz.$Name.mutation" "FAIL" "corpus directory missing: $corpus"
        return
    }

    Write-Host "seed=$Seed target=$Name iterations=$Runs"
    & $exe $corpus "-runs=$Runs" "-seed=$Seed"
    if ($LASTEXITCODE -eq 0) {
        Write-Status "fuzz.$Name.mutation" "PASS" "libFuzzer completed $Runs runs"
    } else {
        Write-Status "fuzz.$Name.mutation" "FAIL" "libFuzzer exited with code $LASTEXITCODE; rerun: $exe $corpus -runs=$Runs -seed=$Seed"
    }
}

function Invoke-JsTarget {
    param([string]$Name)

    $node = Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $node) {
        Write-Status "fuzz.$Name.random" "UNAVAILABLE" "node is not available"
        return
    }
    $script = Join-Path $Root "tests/fuzz/js_fuzz_targets.mjs"
    $runs = Get-Iterations
    $repro = "node tests/fuzz/js_fuzz_targets.mjs --target $Name --iterations $runs --seed $Seed"
    Write-Host "seed=$Seed target=$Name iterations=$runs"
    & $node.Source $script "--target" $Name "--iterations" ([string]$runs) "--seed" ([string]$Seed) "--failure-root" $FailureRoot "--repro-command" $repro
    if ($LASTEXITCODE -eq 0) {
        Write-Status "fuzz.$Name.random" "PASS" "random fuzz completed $runs iterations"
    } else {
        $failurePath = Join-Path (Join-Path $FailureRoot $Name) "$Seed.bin"
        Write-Status "fuzz.$Name.random" "FAIL" "failure input: $failurePath; repro: $repro"
    }
}

$runs = Get-Iterations
Write-Host "Sloppy fuzz tier=$Tier seed=$Seed iterations=$runs"

if ($All) {
    Invoke-CtestFuzz "fuzz.seed_replay" @("-L", "fuzz")
    foreach ($jsTarget in $JsTargets) {
        Invoke-JsTarget $jsTarget
    }
} elseif ($NativeTargets.ContainsKey($Target)) {
    $spec = $NativeTargets[$Target]
    Invoke-CtestFuzz "fuzz.$Target.seed_replay" @("-R", $spec.CTest)
    Invoke-LibFuzzerTarget $Target $spec $runs
} elseif ($JsTargets -contains $Target) {
    Invoke-JsTarget $Target
} else {
    [Console]::Error.WriteLine("fuzz: unknown target '$Target'. Run tools/windows/fuzz.ps1 -Help.")
    exit 2
}

if ($Failed -gt 0) {
    exit 1
}

exit 0
