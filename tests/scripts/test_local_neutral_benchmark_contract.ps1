[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path -LiteralPath $RepoRoot).Path

function Invoke-Node {
    param([string[]]$Arguments)
    Push-Location $repo
    try {
        & node @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "node $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

Invoke-Node @("--check", "benchmarks/local-neutral/scripts/run.mjs")
Invoke-Node @("--check", "benchmarks/local-neutral/scripts/resources.mjs")
Invoke-Node @("--check", "benchmarks/local-neutral/scripts/render-markdown.mjs")

$resourceProbe = @'
import { processSnapshot, summarizeResourceSamples } from "./benchmarks/local-neutral/scripts/resources.mjs";
const first = processSnapshot(process.pid);
await new Promise((resolve) => setTimeout(resolve, 120));
const second = processSnapshot(process.pid);
const summary = summarizeResourceSamples([first, second].filter(Boolean));
if (summary.status !== "PASS") throw new Error(`expected resource summary PASS, got ${summary.status}`);
if (!Number.isFinite(summary.peakRssBytes)) throw new Error("expected peakRssBytes");
console.log(JSON.stringify({ status: "PASS", sampleCount: summary.sampleCount }));
'@
Push-Location $repo
try {
    $resourceOutput = $resourceProbe | node --input-type=module -
    if ($LASTEXITCODE -ne 0) {
        throw "resource sampler probe failed"
    }
    $resourceJson = $resourceOutput | ConvertFrom-Json
    if ($resourceJson.status -ne "PASS" -or $resourceJson.sampleCount -lt 1) {
        throw "resource sampler probe returned unexpected output: $resourceOutput"
    }
}
finally {
    Pop-Location
}

$reportProbe = @'
import { renderMarkdown } from "./benchmarks/local-neutral/scripts/render-markdown.mjs";
const report = {
  startedAt: "2026-01-01T00:00:00.000Z",
  tool: "k6",
  tools: { k6: { status: "AVAILABLE", version: "k6 test", path: "k6" } },
  environment: { hostname: "host", os: "win32", release: "test", cpu: "cpu", logicalCores: 8, memoryBytes: 1024 },
  git: { commit: "abc", branch: "bench", dirty: false },
  options: { claimMode: "public-candidate", loadHostKind: "separate-machine", resourceSampling: true, resourceIntervalMs: 500 },
  workloads: [{ name: "health", method: "GET", path: "/health", description: "health" }],
  results: [{
    status: "PASS",
    runtime: "sloppy",
    workload: "health",
    connections: 1,
    repeat: 1,
    rps: 1000,
    latencyMs: { p50: 1, p95: 2, p99: 3 },
    errors: 0,
    non2xx: 0,
    serverResources: { status: "PASS", peakRssBytes: 1024, peakPrivateMemoryBytes: 2048, averageCpuPercent: 12.5, peakCpuPercent: 25, peakThreads: 4, peakHandles: 5 }
  }],
  publicClaimReadiness: {
    status: "PUBLIC_CANDIDATE",
    summary: "ready",
    criteria: [{ name: "p95 and p99 latency captured", status: "PASS", details: "ok" }]
  },
  reproductionCommand: "node benchmarks/local-neutral/scripts/run.mjs --tool k6"
};
const markdown = renderMarkdown(report);
for (const text of ["Public Claim Readiness", "Resource Summary", "p95 ms", "p99 ms", "Peak RSS", "PUBLIC_CANDIDATE"]) {
  if (!markdown.includes(text)) throw new Error(`missing ${text}`);
}
console.log("PASS");
'@
Push-Location $repo
try {
    $reportOutput = $reportProbe | node --input-type=module -
    if ($LASTEXITCODE -ne 0 -or ($reportOutput | Select-Object -Last 1) -ne "PASS") {
        throw "report renderer probe failed: $reportOutput"
    }
}
finally {
    Pop-Location
}

"local-neutral benchmark contract PASS"
