import fs from "node:fs/promises";
import path from "node:path";

function fmtNumber(value, digits = 2) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "";
  }
  return Number(value).toFixed(digits);
}

function fmtInt(value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "";
  }
  return String(Math.round(Number(value)));
}

function fmtBytes(value) {
  if (value === null || value === undefined || Number.isNaN(value)) {
    return "";
  }
  const units = ["B", "KB", "MB", "GB"];
  let current = Number(value);
  let unit = 0;
  while (current >= 1024 && unit < units.length - 1) {
    current /= 1024;
    unit += 1;
  }
  return `${current.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function median(values) {
  const sorted = values.filter((value) => Number.isFinite(value)).sort((a, b) => a - b);
  if (sorted.length === 0) {
    return null;
  }
  return sorted[Math.floor(sorted.length / 2)];
}

function groupKey(run) {
  return [run.runtime, run.workload, run.variant, run.connections].join("\u0001");
}

function summarizeRuns(runs) {
  const groups = new Map();
  for (const run of runs) {
    if (run.status !== "PASS") {
      continue;
    }
    const key = groupKey(run);
    if (!groups.has(key)) {
      groups.set(key, []);
    }
    groups.get(key).push(run);
  }
  return Array.from(groups.values()).map((items) => {
    const first = items[0];
    return {
      runtime: first.runtime,
      workload: first.workload,
      variant: first.variant,
      connections: first.connections,
      iterations: items.length,
      requestsPerSecond: median(items.map((item) => item.requestsPerSecond)),
      p50Ms: median(items.map((item) => item.latency.p50Ms)),
      p95Ms: median(items.map((item) => item.latency.p95Ms)),
      p99Ms: median(items.map((item) => item.latency.p99Ms)),
      errors: items.reduce((sum, item) => sum + item.transfer.errors, 0),
      non2xx: items.reduce((sum, item) => sum + item.transfer.non2xx, 0),
      rssBytes: median(items.map((item) => item.process.peakWorkingSetBytes)),
    };
  }).sort((a, b) =>
    a.workload.localeCompare(b.workload) ||
    a.variant.localeCompare(b.variant) ||
    a.connections - b.connections ||
    a.runtime.localeCompare(b.runtime));
}

function markdownTable(headers, rows) {
  const lines = [];
  lines.push(`| ${headers.join(" | ")} |`);
  lines.push(`| ${headers.map(() => "---").join(" | ")} |`);
  for (const row of rows) {
    lines.push(`| ${row.join(" | ")} |`);
  }
  return lines.join("\n");
}

function deltaRows(summaries, competitor) {
  const sloppy = summaries.filter((item) => item.runtime === "sloppy");
  const byCompetitor = new Map(
    summaries
      .filter((item) => item.runtime === competitor)
      .map((item) => [[item.workload, item.variant, item.connections].join("\u0001"), item]),
  );
  const rows = [];
  for (const item of sloppy) {
    const other = byCompetitor.get([item.workload, item.variant, item.connections].join("\u0001"));
    if (!other || !Number.isFinite(other.requestsPerSecond) || other.requestsPerSecond === 0) {
      continue;
    }
    const delta = ((item.requestsPerSecond - other.requestsPerSecond) / other.requestsPerSecond) * 100;
    rows.push([
      item.workload,
      item.variant,
      String(item.connections),
      fmtNumber(item.requestsPerSecond),
      fmtNumber(other.requestsPerSecond),
      `${fmtNumber(delta, 1)}%`,
    ]);
  }
  return rows;
}

export async function writeReports(result, outDir) {
  await fs.mkdir(outDir, { recursive: true });
  await fs.writeFile(path.join(outDir, "results.json"), `${JSON.stringify(result, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "summary.md"), renderMarkdown(result), "utf8");
}

export function renderMarkdown(result) {
  const summaries = summarizeRuns(result.runs);
  const lines = [];
  lines.push("# Realistic Local Benchmark Summary");
  lines.push("");
  lines.push("## Caveat");
  lines.push("");
  lines.push("These benchmarks are local engineering measurements from one machine. They are not official performance claims. They exist to track regressions and identify bottlenecks while Sloppy is pre-alpha.");
  lines.push("");
  lines.push("Do not use these numbers as marketing copy, do not cherry-pick them into public claims, and do not compare debug Sloppy builds to release-like Node, Bun, or Deno runs.");
  lines.push("");
  lines.push("## Environment");
  lines.push("");
  lines.push(`- Started: ${result.startedAt}`);
  lines.push(`- OS: ${result.host.os} ${result.host.release}`);
  lines.push(`- Arch: ${result.host.arch}`);
  lines.push(`- CPU: ${result.host.cpu}`);
  lines.push(`- Logical cores: ${result.host.logicalCores}`);
  lines.push(`- Memory: ${fmtBytes(result.host.memoryBytes)}`);
  lines.push(`- Git commit: ${result.host.gitCommit ?? ""}`);
  lines.push("");
  lines.push("## Runtime Versions");
  lines.push("");
  lines.push(markdownTable(["Runtime", "Status", "Version", "Path"], Object.entries(result.tools)
    .filter(([name]) => name !== "loadGenerator")
    .map(([name, tool]) => [name, tool.status ?? "", tool.version ?? "", tool.path ?? ""])));
  lines.push("");
  lines.push(`Load generator: ${result.tools.loadGenerator.name} ${result.tools.loadGenerator.version}`);
  lines.push("");
  lines.push("## Workloads");
  lines.push("");
  lines.push(markdownTable(["Workload", "Definition"], result.workloadDefinitions.map((workload) => [
    workload.name,
    workload.description,
  ])));
  lines.push("");
  lines.push("## Results");
  lines.push("");
  if (summaries.length === 0) {
    lines.push("No passing measurement rows were produced.");
  } else {
    lines.push(markdownTable(
      ["Workload", "Variant", "Runtime", "Conn", "RPS", "p50 ms", "p95 ms", "p99 ms", "Errors", "Non-2xx", "Peak RSS"],
      summaries.map((item) => [
        item.workload,
        item.variant,
        item.runtime,
        String(item.connections),
        fmtNumber(item.requestsPerSecond),
        fmtNumber(item.p50Ms),
        fmtNumber(item.p95Ms),
        fmtNumber(item.p99Ms),
        fmtInt(item.errors),
        fmtInt(item.non2xx),
        fmtBytes(item.rssBytes),
      ]),
    ));
  }
  lines.push("");
  lines.push("## Relative Deltas");
  lines.push("");
  for (const competitor of ["node", "bun", "deno"]) {
    const rows = deltaRows(summaries, competitor);
    lines.push(`### Sloppy relative to ${competitor}`);
    lines.push("");
    if (rows.length === 0) {
      lines.push("No comparable passing local rows.");
    } else {
      lines.push(markdownTable(
        ["Workload", "Variant", "Conn", "Sloppy RPS", `${competitor} RPS`, "Delta"],
        rows,
      ));
    }
    lines.push("");
  }
  const failed = result.runs.filter((run) => run.status !== "PASS");
  if (failed.length > 0) {
    lines.push("## Non-PASS Rows");
    lines.push("");
    lines.push(markdownTable(["Runtime", "Variant", "Workload", "Status", "Reason"], failed.map((run) => [
      run.runtime,
      run.variant,
      run.workload,
      run.status,
      run.reason ?? "",
    ])));
    lines.push("");
  }
  return `${lines.join("\n")}\n`;
}
