import fs from "node:fs/promises";
import path from "node:path";

const args = new Map();
for (let i = 2; i < process.argv.length; i += 1) {
  if (!process.argv[i].startsWith("--")) {
    continue;
  }
  const key = process.argv[i].slice(2);
  const next = process.argv[i + 1];
  if (next == null || next.startsWith("--")) {
    args.set(key, "true");
  } else {
    args.set(key, next);
    i += 1;
  }
}

const inputPath = args.get("input");
if (!inputPath) {
  throw new Error("Usage: node benchmarks/competitors/report-json-local.mjs --input <json> [--baseline <json>] [--profile-input <json>] [--out <markdown>]");
}

const outPath = args.get("out") ?? path.join(path.dirname(inputPath), "report.md");
const baselinePath = args.get("baseline") ?? null;
const profileInputPath = args.get("profile-input") ?? null;
const topProfileRows = Number(args.get("top-profile-rows") ?? "40");

function formatNs(value) {
  if (value == null || !Number.isFinite(Number(value))) return "-";
  const ns = Number(value);
  if (Math.abs(ns) >= 1_000_000) return `${(ns / 1_000_000).toFixed(2)} ms`;
  return `${(ns / 1_000).toFixed(1)} us`;
}

function formatCount(value) {
  if (value == null || !Number.isFinite(Number(value))) return "-";
  return String(Number(value));
}

function formatPercent(value) {
  if (value == null || !Number.isFinite(value)) return "-";
  const sign = value > 0 ? "+" : "";
  return `${sign}${value.toFixed(1)}%`;
}

function escapeCell(value) {
  return String(value ?? "-").replaceAll("|", "\\|").replaceAll("\n", "<br>");
}

function table(headers, rows) {
  if (rows.length === 0) return "_No rows._\n";
  const header = `| ${headers.map(escapeCell).join(" | ")} |`;
  const separator = `| ${headers.map(() => "---").join(" | ")} |`;
  const body = rows.map((row) => `| ${row.map(escapeCell).join(" | ")} |`);
  return `${[header, separator, ...body].join("\n")}\n`;
}

function runtimeLabel(runtime) {
  if (runtime === "sloppy:loopback:native_json") return "Sloppy loopback native JSON";
  if (runtime === "sloppy:loopback:generic_json") return "Sloppy loopback generic JSON";
  if (runtime === "node:http") return "Node http";
  if (runtime === "node:express") return "Express";
  if (runtime === "node:fastify") return "Fastify";
  if (runtime === "bun") return "Bun";
  if (runtime === "deno") return "Deno";
  return runtime;
}

function summaryMap(report) {
  const map = new Map();
  for (const row of report.summary ?? []) {
    map.set(`${row.runtime}\0${row.scenario}`, row);
  }
  return map;
}

function resultByRuntime(report) {
  const map = new Map();
  for (const result of report.results ?? []) {
    map.set(result.runtime, result);
  }
  return map;
}

async function readJson(file) {
  const text = await fs.readFile(file, "utf8");
  return JSON.parse(text.replace(/^\uFEFF/, ""));
}

function scenarioNames(report) {
  const fromDoc = Object.keys(report.scenarios ?? {});
  if (fromDoc.length > 0) return fromDoc;
  return Array.from(new Set((report.summary ?? []).map((row) => row.scenario))).sort();
}

function selectedRuntimes(report) {
  const present = new Set((report.results ?? []).map((result) => result.runtime));
  return [
    "sloppy:loopback:native_json",
    "sloppy:loopback:generic_json",
    "node:http",
    "node:express",
    "node:fastify",
    "bun",
    "deno",
  ].filter((runtime) => present.has(runtime));
}

function timingRows(report) {
  const bySummary = summaryMap(report);
  const runtimes = selectedRuntimes(report);
  return scenarioNames(report).map((scenario) => [
    scenario,
    ...runtimes.map((runtime) =>
      formatNs(bySummary.get(`${runtime}\0${scenario}`)?.medianNsPerOp ?? null),
    ),
  ]);
}

function comparisonRows(report, baseline) {
  if (!baseline) return [];
  const current = summaryMap(report);
  const before = summaryMap(baseline);
  const rows = [];
  for (const scenario of scenarioNames(report)) {
    for (const runtime of ["sloppy:loopback:native_json", "sloppy:loopback:generic_json"]) {
      const after = current.get(`${runtime}\0${scenario}`)?.medianNsPerOp ?? null;
      const base = before.get(`${runtime}\0${scenario}`)?.medianNsPerOp ?? null;
      const delta = base != null && after != null && base !== 0 ? ((after - base) / base) * 100 : null;
      rows.push([scenario, runtimeLabel(runtime), formatNs(base), formatNs(after), formatPercent(delta)]);
    }
  }
  return rows;
}

function runtimeStatusRows(report) {
  return (report.results ?? []).map((result) => [
    runtimeLabel(result.runtime),
    result.status ?? "-",
    result.version ?? "-",
    result.reason ?? "",
  ]);
}

function labelValidationRows(report) {
  const validation = report.labelValidation;
  if (!validation) {
    return [["UNAVAILABLE", "-", "older report did not include label validation metadata"]];
  }
  return [[
    validation.status ?? "-",
    validation.checkedRows ?? 0,
    (validation.failures ?? []).length === 0 ? "native/generic row prefixes and metadata matched" : validation.failures.join("; "),
  ]];
}

function profileCounter(profile, name) {
  const value = profile.counters?.[name];
  return value == null ? null : Number(value);
}

function topPhase(profile) {
  const phases = Object.entries(profile.phases ?? {})
    .filter(([, stats]) => Number(stats?.totalNs ?? 0) > 0)
    .sort((a, b) => Number(b[1].totalNs) - Number(a[1].totalNs));
  if (phases.length === 0) return { name: "-", totalNs: null, avgNs: null };
  return {
    name: phases[0][0],
    totalNs: Number(phases[0][1].totalNs),
    avgNs: Number(phases[0][1].avgNs),
  };
}

async function readProfiles(profileReport) {
  const profile = profileReport?.httpProfile;
  if (!profile?.enabled || !profile?.outDir || !profile?.runId) {
    return { rows: [], checks: [], unavailable: ["No HTTP profile run was attached."] };
  }
  let entries = [];
  try {
    entries = await fs.readdir(profile.outDir);
  } catch (error) {
    return { rows: [], checks: [], unavailable: [`Profile directory unavailable: ${error.message}`] };
  }
  const files = entries
    .filter((entry) => entry.startsWith(`http-profile-${profile.runId}-`) && entry.endsWith(".json"))
    .sort();
  const rows = [];
  const checks = [];
  const unavailable = [];
  for (const file of files) {
    const filePath = path.join(profile.outDir, file);
    try {
      const parsed = await readJson(filePath);
      const top = topPhase(parsed);
      const scenario = parsed.scenario ?? file;
      const v8Calls = profileCounter(parsed, "v8HandlerCalls") ?? 0;
      const noJsHits = profileCounter(parsed, "noJsResponsePlanHits") ?? 0;
      const isStaticNoJs = /\.static_(json|text|status|problem)/.test(scenario);
      const isDynamicV8 =
        /\.(dynamic_|ctx_|plain_object|exception|large|route_table|small|medium)/.test(scenario);
      if (isStaticNoJs || isDynamicV8) {
        const pass = isStaticNoJs ? noJsHits > 0 && v8Calls === 0 : v8Calls > 0;
        checks.push([
          scenario,
          pass ? "PASS" : "FAIL",
          isStaticNoJs ? "static no-JS route" : "dynamic/V8 route",
          formatCount(v8Calls),
          formatCount(noJsHits),
        ]);
      }
      rows.push([
        scenario,
        formatCount(parsed.requests),
        formatCount(profileCounter(parsed, "keepAliveReused")),
        formatCount(profileCounter(parsed, "connectionsOpened")),
        formatCount(profileCounter(parsed, "v8HandlerCalls")),
        formatCount(profileCounter(parsed, "noJsResponsePlanHits")),
        top.name,
        formatNs(top.totalNs),
        formatNs(top.avgNs),
      ]);
    } catch (error) {
      unavailable.push(`${file}: ${error.message}`);
    }
  }
  return { rows: rows.slice(0, topProfileRows), checks, unavailable };
}

function commandLine({ report, baseline, profileReport }) {
  const parts = [
    `iterations=${report.iterations}`,
    `warmup=${report.warmupIterations}`,
    `repeat=${report.repeat}`,
  ];
  if (baseline) parts.push(`baseline=${path.basename(baselinePath)}`);
  if (profileReport?.httpProfile?.enabled) parts.push(`profileRunId=${profileReport.httpProfile.runId}`);
  if (Array.isArray(report.runtimeFilter) && report.runtimeFilter.length > 0) {
    parts.push(`runtime=${report.runtimeFilter.join("+")}`);
  }
  if (Array.isArray(report.selectedScenarios) && report.selectedScenarios.length > 0) {
    parts.push(`scenarios=${report.selectedScenarios.join("+")}`);
  }
  return parts.join(", ");
}

const report = await readJson(inputPath);
const baseline = baselinePath ? await readJson(baselinePath) : null;
const profileReport = profileInputPath ? await readJson(profileInputPath) : report;
const profiles = await readProfiles(profileReport);
const runtimes = selectedRuntimes(report);

const lines = [];
lines.push("# JSON Competitor Benchmark Report");
lines.push("");
lines.push("Local engineering evidence only. Do not use this report as a public performance claim.");
lines.push("");
lines.push("## Inputs");
lines.push("");
lines.push(table(["Field", "Value"], [
  ["Timing input", path.normalize(inputPath)],
  ["Baseline input", baselinePath ? path.normalize(baselinePath) : "none"],
  ["Profile input", profileInputPath ? path.normalize(profileInputPath) : "same as timing input"],
  ["Git commit", report.git?.commit ?? "-"],
  ["Git branch", report.git?.branch ?? "-"],
  ["Dirty checkout", report.git?.dirty === true ? "yes" : report.git?.dirty === false ? "no" : "-"],
  ["Sloppy executable", report.sloppyExecutable ?? "-"],
  ["Generated at", report.generatedAtUtc ?? "-"],
  ["Command shape", commandLine({ report, baseline, profileReport })],
  ["Client", report.client ?? "-"],
  ["Host", `${report.host?.platform ?? "-"} ${report.host?.release ?? ""} ${report.host?.arch ?? ""}`.trim()],
  ["CPU", report.host?.cpu ?? "-"],
]));

lines.push("## Runtime Status");
lines.push("");
lines.push(table(["Runtime", "Status", "Version", "Reason"], runtimeStatusRows(report)));

lines.push("## Label Verification");
lines.push("");
lines.push(table(["Status", "Checked rows", "Details"], labelValidationRows(report)));

lines.push("## Median ns/op By Scenario");
lines.push("");
lines.push(table(["Scenario", ...runtimes.map(runtimeLabel)], timingRows(report)));

if (baseline) {
  lines.push("## Sloppy Before/After Delta");
  lines.push("");
  lines.push("Negative delta means the current timing input is lower than the baseline timing input.");
  lines.push("");
  lines.push(table(["Scenario", "Runtime", "Baseline median", "Current median", "Delta"], comparisonRows(report, baseline)));
}

lines.push("## HTTP Profile Evidence");
lines.push("");
if (profileReport?.httpProfile?.enabled) {
  lines.push(`Profile run id: \`${profileReport.httpProfile.runId}\``);
  lines.push("");
  lines.push("Profile rows are instrumentation evidence. They are not the latency rows used for the before/after timing comparison.");
  lines.push("");
}
lines.push(table([
  "Scenario",
  "Requests",
  "Keep-alive reused",
  "Connections opened",
  "V8 calls",
  "No-JS hits",
  "Top phase",
  "Top total",
  "Top avg",
], profiles.rows));
if (profiles.checks.length > 0) {
  lines.push("");
  lines.push("Profile expectation checks:");
  lines.push("");
  lines.push(table(["Scenario", "Status", "Expected path", "V8 calls", "No-JS hits"], profiles.checks));
}
if (profiles.unavailable.length > 0) {
  lines.push("");
  lines.push("Unavailable profile details:");
  lines.push("");
  for (const item of profiles.unavailable) {
    lines.push(`- ${item}`);
  }
}

lines.push("");
lines.push("## Reading Notes");
lines.push("");
lines.push("- Compare rows only within the same scenario, client, iteration, warmup, and repeat policy.");
lines.push("- The loopback client is one awaited Node fetch request at a time, so rows include client, socket, server, and event-loop costs.");
lines.push("- Express and Fastify are optional dependency rows and may be SKIPPED on a clean checkout.");
lines.push("- Profile instrumentation writes JSON snapshots and can change timing; use non-profile JSON for timing comparisons.");
lines.push("- Missing optional runtimes are evidence about the local environment, not benchmark failures.");
lines.push("");

await fs.mkdir(path.dirname(outPath), { recursive: true });
await fs.writeFile(outPath, `${lines.join("\n")}\n`, "utf8");
console.log(outPath);
