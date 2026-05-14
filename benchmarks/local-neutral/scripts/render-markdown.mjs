import { comparisons, summarize } from "./summarize.mjs";

function num(value, digits = 2) {
  return Number.isFinite(value) ? Number(value).toFixed(digits) : "";
}

function table(headers, rows) {
  return [
    `| ${headers.join(" | ")} |`,
    `| ${headers.map(() => "---").join(" | ")} |`,
    ...rows.map((row) => `| ${row.join(" | ")} |`),
  ].join("\n");
}

function fmtDelta(value) {
  return Number.isFinite(value) ? `${num(value, 1)}%` : "";
}

function bytes(value) {
  if (!Number.isFinite(value)) return "";
  const units = ["B", "KiB", "MiB", "GiB"];
  let amount = Number(value);
  let unit = 0;
  while (amount >= 1024 && unit < units.length - 1) {
    amount /= 1024;
    unit += 1;
  }
  return `${amount.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

export function renderMarkdown(report) {
  const summaries = summarize(report.results);
  const deltas = comparisons(summaries);
  const lines = [];
  lines.push("# Local Neutral Runtime Benchmark Report");
  lines.push("");
  lines.push("These are local engineering measurements, not public performance claims.");
  lines.push("Single-machine benchmark results can be affected by CPU scheduling, power mode, thermal behavior, loopback networking, tool choice, and background processes.");
  lines.push("");
  lines.push("## Environment");
  lines.push("");
  lines.push(table(["Field", "Value"], [
    ["Started", report.startedAt],
    ["Host", report.environment.hostname],
    ["OS", `${report.environment.os} ${report.environment.release}`],
    ["CPU", report.environment.cpu],
    ["Logical cores", String(report.environment.logicalCores)],
    ["RAM bytes", String(report.environment.memoryBytes)],
    ["Git commit", report.git?.commit ?? report.gitCommit ?? ""],
    ["Git branch", report.git?.branch ?? ""],
    ["Git dirty", String(report.git?.dirty ?? "")],
    ["Load generator", `${report.tool} ${report.tools[report.tool]?.version ?? ""}`],
    ["Claim mode", report.options?.claimMode ?? "local"],
    ["Load host kind", report.options?.loadHostKind ?? "same-machine"],
    ["Resource sampling", report.options?.resourceSampling === false ? "disabled" : `${report.options?.resourceIntervalMs ?? 500} ms`],
  ]));
  lines.push("");
  lines.push("## Tool And Runtime Availability");
  lines.push("");
  lines.push(table(["Name", "Status", "Version", "Path"], Object.entries(report.tools).map(([name, tool]) => [
    name,
    tool.status,
    tool.version ?? "",
    tool.path ?? "",
  ])));
  lines.push("");
  lines.push("## Workload Matrix");
  lines.push("");
  lines.push(table(["Workload", "Method", "Path", "Notes"], report.workloads.map((workload) => [
    workload.name,
    workload.mixed ? "mixed" : workload.method,
    workload.mixed ? "weighted mix" : workload.path,
    workload.description ?? "",
  ])));
  lines.push("");
  lines.push("## RPS Summary");
  lines.push("");
  if (summaries.length === 0) {
    lines.push("No passing benchmark rows were produced.");
  } else {
    lines.push(table(
      ["Workload", "Conn", "Runtime", "Median RPS", "Mean RPS", "Min RPS", "Max RPS", "RPS stdev", "Errors", "Non-2xx"],
      summaries.map((row) => [
        row.workload,
        String(row.connections),
        row.runtime,
        num(row.medianRps),
        num(row.meanRps),
        num(row.minRps),
        num(row.maxRps),
        num(row.stdevRps),
        String(row.errors),
        String(row.non2xx),
      ]),
    ));
  }
  lines.push("");
  lines.push("## Latency Summary");
  lines.push("");
  lines.push(table(["Workload", "Conn", "Runtime", "p50 ms", "p95 ms", "p99 ms"], summaries.map((row) => [
    row.workload,
    String(row.connections),
    row.runtime,
    num(row.p50Ms),
    num(row.p95Ms),
    num(row.p99Ms),
  ])));
  lines.push("");
  lines.push("## Resource Summary");
  lines.push("");
  lines.push(table(["Workload", "Conn", "Runtime", "Peak RSS", "Peak private", "Avg CPU %", "Peak CPU %", "Threads", "Handles"], summaries.map((row) => [
    row.workload,
    String(row.connections),
    row.runtime,
    bytes(row.peakRssBytes ?? row.peakWorkingSetBytes),
    bytes(row.peakPrivateMemoryBytes),
    num(row.avgCpuPercent),
    num(row.peakCpuPercent),
    num(row.peakThreads, 0),
    num(row.peakHandles, 0),
  ])));
  lines.push("");
  lines.push("## Public Claim Readiness");
  lines.push("");
  if (report.publicClaimReadiness) {
    lines.push(`Status: \`${report.publicClaimReadiness.status}\``);
    lines.push("");
    lines.push(report.publicClaimReadiness.summary);
    lines.push("");
    lines.push(table(["Criterion", "Status", "Details"], report.publicClaimReadiness.criteria.map((row) => [
      row.name,
      row.status,
      row.details ?? "",
    ])));
  } else {
    lines.push("No public-claim readiness metadata was produced.");
  }
  lines.push("");
  lines.push("## Comparison Notes");
  lines.push("");
  lines.push("When one runtime has the highest median RPS in a row, this report says it had higher RPS in this run. It does not claim that runtime is generally faster.");
  lines.push("");
  lines.push(table(["Workload", "Conn", "Highest median RPS in this run", "Sloppy vs Node", "Sloppy vs Bun", "Sloppy vs Deno"], deltas.map((row) => [
    row.workload,
    String(row.connections),
    row.fastestRuntime,
    fmtDelta(row.sloppyDeltaVs.node),
    fmtDelta(row.sloppyDeltaVs.bun),
    fmtDelta(row.sloppyDeltaVs.deno),
  ])));
  const skipped = report.results.filter((row) => row.status !== "PASS");
  if (skipped.length > 0) {
    lines.push("");
    lines.push("## Non-PASS Rows");
    lines.push("");
    lines.push(table(["Runtime", "Workload", "Conn", "Repeat", "Status", "Reason"], skipped.map((row) => [
      row.runtime,
      row.workload,
      String(row.connections ?? ""),
      String(row.repeat ?? ""),
      row.status,
      row.reason ?? "",
    ])));
  }
  lines.push("");
  lines.push("## Reproduce");
  lines.push("");
  lines.push("```sh");
  lines.push(report.reproductionCommand);
  lines.push("```");
  lines.push("");
  lines.push("## Caveats");
  lines.push("");
  lines.push("- This is a same-machine loopback benchmark.");
  lines.push("- Neutral load generators must be installed separately.");
  lines.push("- Missing optional runtimes are skipped unless requested explicitly.");
  lines.push("- These results should stay with their JSON artifacts when reviewed.");
  return `${lines.join("\n")}\n`;
}
