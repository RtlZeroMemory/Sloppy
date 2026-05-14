#!/usr/bin/env node
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { detectTools, hostMetadata, runtimeAvailable, selectLoadTool } from "./tools.mjs";
import { startServer, stopServer } from "./server-process.mjs";
import { runOha } from "./load-oha.mjs";
import { runWrk } from "./load-wrk.mjs";
import { runK6 } from "./load-k6.mjs";
import { runVegeta } from "./load-vegeta.mjs";
import { renderMarkdown } from "./render-markdown.mjs";
import { comparisons, summarize } from "./summarize.mjs";
import { startResourceSampler } from "./resources.mjs";
import { validateWorkload } from "./validate-workload.mjs";

const benchRoot = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const repoRoot = path.resolve(benchRoot, "..", "..");

function split(value) {
  return String(value).split(",").map((item) => item.trim()).filter(Boolean);
}

function seconds(text) {
  const match = String(text).match(/^(\d+)(ms|s|m)?$/);
  if (!match) throw new Error(`Invalid duration: ${text}`);
  const value = Number(match[1]);
  const unit = match[2] ?? "s";
  if (unit === "ms") return value / 1000;
  if (unit === "m") return value * 60;
  return value;
}

function safeToken(value) {
  return path.basename(String(value)).replace(/[^A-Za-z0-9._-]/g, "_") || "0";
}

function parseArgs(argv) {
  const options = {
    tool: "auto",
    runtimes: ["all"],
    workloads: null,
    connections: null,
    duration: null,
    warmup: null,
    repeats: null,
    host: "127.0.0.1",
    basePort: 41000,
    sloppyBin: null,
    sloppycBin: null,
    mode: "artifacts",
    out: null,
    noOpen: false,
    json: false,
    preset: "quick",
    checkTools: false,
    resourceSampling: true,
    resourceIntervalMs: 500,
    claimMode: "local",
    loadHostKind: "same-machine",
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    const value = () => {
      index += 1;
      if (index >= argv.length) throw new Error(`Missing value for ${arg}`);
      return argv[index];
    };
    if (arg === "--tool") options.tool = value();
    else if (arg === "--runtime") options.runtimes = split(value());
    else if (arg === "--workload") options.workloads = split(value());
    else if (arg === "--connections") options.connections = split(value()).map(Number);
    else if (arg === "--duration") options.duration = value();
    else if (arg === "--warmup") options.warmup = value();
    else if (arg === "--repeats") options.repeats = Number(value());
    else if (arg === "--host") options.host = value();
    else if (arg === "--base-port") options.basePort = Number(value());
    else if (arg === "--sloppy-bin") options.sloppyBin = path.resolve(value());
    else if (arg === "--sloppyc-bin") options.sloppycBin = path.resolve(value());
    else if (arg === "--mode") options.mode = value();
    else if (arg === "--out") options.out = path.resolve(value());
    else if (arg === "--preset") options.preset = value();
    else if (arg === "--resource-interval-ms") options.resourceIntervalMs = Number(value());
    else if (arg === "--no-resources") options.resourceSampling = false;
    else if (arg === "--claim-mode") options.claimMode = value();
    else if (arg === "--load-host-kind") options.loadHostKind = value();
    else if (arg === "--no-open") options.noOpen = true;
    else if (arg === "--json") options.json = true;
    else if (arg === "--check-tools") options.checkTools = true;
    else if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else throw new Error(`Unknown option: ${arg}`);
  }
  return options;
}

function printHelp() {
  console.log(`Usage: node benchmarks/local-neutral/scripts/run.mjs [options]

Options:
  --tool oha|wrk|k6|vegeta|auto
  --runtime sloppy|node|bun|deno|all
  --workload health,json-small,route-param,...
  --connections 1,16,64
  --duration 15s
  --warmup 5s
  --repeats 3
  --host 127.0.0.1
  --base-port 41000
  --sloppy-bin <path>
  --sloppyc-bin <path>
  --mode artifacts|package
  --out artifacts/benchmarks/local-neutral/<name>
  --preset quick|realistic-short|alpha|full|stress|public-candidate
  --claim-mode local|public-candidate
  --load-host-kind same-machine|separate-machine
  --resource-interval-ms 500
  --no-resources
  --check-tools
  --json`);
}

async function readJson(file) {
  return JSON.parse(await fs.readFile(file, "utf8"));
}

async function writeJson(file, value) {
  const temp = `${file}.tmp`;
  await fs.writeFile(temp, `${JSON.stringify(value, null, 2)}\n`, "utf8");
  await fs.rename(temp, file);
}

async function loadMatrix(options) {
  const matrix = await readJson(path.join(benchRoot, "matrix.json"));
  const preset = matrix.presets[options.preset];
  if (!preset) throw new Error(`Unknown preset: ${options.preset}`);
  return {
    workloads: options.workloads ?? preset.workloads,
    connections: options.connections ?? preset.connections,
    repeats: options.repeats ?? preset.repeats,
    duration: options.duration ?? preset.duration,
    warmup: options.warmup ?? preset.warmup,
  };
}

async function loadWorkloads(names) {
  const workloads = [];
  for (const name of names) {
    workloads.push(await readJson(path.join(benchRoot, "workloads", `${name}.json`)));
  }
  return workloads;
}

function gitCommit() {
  const result = spawnSync("git", ["rev-parse", "HEAD"], { cwd: repoRoot, encoding: "utf8" });
  return result.status === 0 ? result.stdout.trim() : null;
}

function gitInfo() {
  const commit = gitCommit();
  const branch = spawnSync("git", ["branch", "--show-current"], { cwd: repoRoot, encoding: "utf8" });
  const dirty = spawnSync("git", ["status", "--short", "--untracked-files=no"], { cwd: repoRoot, encoding: "utf8" });
  return {
    commit,
    branch: branch.status === 0 ? branch.stdout.trim() : "",
    dirty: dirty.status === 0 ? dirty.stdout.trim().length !== 0 : null,
  };
}

function defaultOut() {
  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  return path.join(repoRoot, "artifacts", "benchmarks", "local-neutral", stamp);
}

function resolveSloppy(tools, explicit) {
  if (explicit) return explicit;
  const candidates = [
    path.join(repoRoot, "build", "windows-relwithdebinfo", "sloppy.exe"),
    path.join(repoRoot, "build", "windows-release", "sloppy.exe"),
    path.join(repoRoot, "build", "unix-relwithdebinfo", "sloppy"),
    path.join(repoRoot, "build", "unix-release", "sloppy"),
    tools.sloppy?.path,
  ].filter(Boolean);
  return candidates.find((candidate) => {
    try { return spawnSync(candidate, ["--version"], { encoding: "utf8" }).status === 0; } catch { return false; }
  }) ?? "";
}

function publicClaimReadiness({ report, options }) {
  const summary = report.summary ?? summarize(report.results);
  const durationSeconds = seconds(report.matrix.duration);
  const requestedPublic = options.claimMode === "public-candidate";
  const criteria = [];
  const add = (name, status, details) => criteria.push({ name, status, details });
  const passRows = report.results.filter((row) => row.status === "PASS");
  const nonPassRows = report.results.filter((row) => row.status !== "PASS");
  const hasPercentiles = summary.length > 0 && summary.every((row) =>
    Number.isFinite(row.p95Ms) && Number.isFinite(row.p99Ms));
  const hasResources = !options.resourceSampling
    ? false
    : passRows.every((row) => row.serverResources?.status === "PASS");
  const selectedRuntimes = options.runtimes.includes("all") ? ["sloppy", "node", "bun", "deno"] : options.runtimes;
  const allCoreRuntimes = ["sloppy", "node", "bun", "deno"].every((runtime) => selectedRuntimes.includes(runtime));
  const hasStressShape = report.matrix.connections.some((value) => value >= 128) &&
    durationSeconds >= 60 &&
    report.matrix.repeats >= 5;

  add("neutral external load generator", report.tool ? "PASS" : "FAIL", report.tool ?? "none");
  add("p95 and p99 latency captured", hasPercentiles ? "PASS" : "FAIL", hasPercentiles ? "all passing summary rows include p95/p99" : "one or more rows are missing p95/p99");
  add("server CPU and memory sampled", hasResources ? "PASS" : "FAIL", hasResources ? "all passing rows include server resource samples" : "one or more passing rows lack resource samples");
  add("all comparator runtimes selected", allCoreRuntimes ? "PASS" : "FAIL", selectedRuntimes.join(","));
  add("no skipped/unavailable/failed rows", nonPassRows.length === 0 ? "PASS" : "FAIL", `${nonPassRows.length} non-PASS rows`);
  add("stress-sized matrix", hasStressShape ? "PASS" : "FAIL", `duration=${report.matrix.duration}, repeats=${report.matrix.repeats}, connections=${report.matrix.connections.join(",")}`);
  add("clean git checkout", report.git?.dirty === false ? "PASS" : "FAIL", report.git?.dirty === false ? "clean" : "dirty or unknown");
  add("load topology", options.loadHostKind === "separate-machine" ? "PASS" : "DEFERRED", options.loadHostKind);

  const blocking = criteria.filter((item) => item.status === "FAIL");
  const deferred = criteria.filter((item) => item.status === "DEFERRED");
  return {
    mode: options.claimMode,
    status: !requestedPublic
      ? "LOCAL_ENGINEERING_ONLY"
      : blocking.length === 0 && deferred.length === 0
        ? "PUBLIC_CANDIDATE"
        : "NOT_PUBLIC_READY",
    criteria,
    summary: requestedPublic
      ? "Public-candidate mode requires complete comparator rows, p95/p99 latency, process resources, stress-sized repeats, a clean checkout, and a separate load-generator topology."
      : "Local mode is useful for engineering comparison, but it is not a public performance claim.",
  };
}

function progressState({ results, matrix, selectedRuntimes, workloads, current = null }) {
  const expectedRepeats =
    selectedRuntimes.length * workloads.length * matrix.connections.length * matrix.repeats;
  const completedRepeats = results.filter((row) => row.repeat > 0 || row.status === "FAIL").length;
  return {
    updatedAt: new Date().toISOString(),
    partial: true,
    current,
    expectedRepeats,
    completedRepeats,
    resultRows: results.length,
    passRows: results.filter((row) => row.status === "PASS").length,
    nonPassRows: results.filter((row) => row.status !== "PASS").length,
  };
}

function makeReport({ baseReport, results, options, partial }) {
  const summary = summarize(results);
  const report = {
    ...baseReport,
    ...(partial ? { partial: true, updatedAt: new Date().toISOString() } : {}),
    results,
    summary,
    comparisons: comparisons(summary),
  };
  report.publicClaimReadiness = publicClaimReadiness({ report, options });
  return report;
}

function serverCommand(runtime, tools, options, port, workDir) {
  const env = { BENCH_HOST: options.host, BENCH_PORT: String(port) };
  if (runtime === "node") {
    return { command: process.execPath, args: [path.join(benchRoot, "servers", "node", "server.mjs")], cwd: repoRoot, env };
  }
  if (runtime === "bun") {
    return { command: tools.bun.path, args: [path.join(benchRoot, "servers", "bun", "server.ts")], cwd: repoRoot, env };
  }
  if (runtime === "deno") {
    return { command: tools.deno.path, args: ["run", `--allow-net=${env.BENCH_HOST}`, "--allow-env", path.join(benchRoot, "servers", "deno", "server.ts")], cwd: repoRoot, env };
  }
  const sloppy = resolveSloppy(tools, options.sloppyBin);
  const source = path.join(benchRoot, "servers", "sloppy", "src", "main.ts");
  const artifacts = path.join(workDir, "sloppy-artifacts");
  const build = spawnSync(sloppy, [
    "build",
    source,
    "--out",
    artifacts,
    "--kind",
    "web",
    "--host",
    options.host,
    "--port",
    String(port),
  ], {
    cwd: path.join(benchRoot, "servers", "sloppy"),
    encoding: "utf8",
  });
  if (build.status !== 0) throw new Error(`sloppy build failed:\n${build.stdout}\n${build.stderr}`);
  return {
    command: sloppy,
    args: ["run", artifacts, "--host", options.host, "--port", String(port)],
    cwd: path.join(benchRoot, "servers", "sloppy"),
    env: { ...env, Auth__apiKey: "benchmark-secret", Auth__ApiKey: "benchmark-secret" },
  };
}

async function runAdapter(tool, params) {
  if (tool === "oha") return runOha(params);
  if (tool === "wrk") return runWrk(params);
  if (tool === "k6") return runK6(params);
  if (tool === "vegeta") return runVegeta(params);
  throw new Error(`Unsupported tool: ${tool}`);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (!["local", "public-candidate"].includes(options.claimMode)) {
    throw new Error("--claim-mode must be local or public-candidate");
  }
  if (!["same-machine", "separate-machine"].includes(options.loadHostKind)) {
    throw new Error("--load-host-kind must be same-machine or separate-machine");
  }
  if (!Number.isFinite(options.resourceIntervalMs) || options.resourceIntervalMs < 100) {
    throw new Error("--resource-interval-ms must be at least 100");
  }
  const overrides = { sloppy: options.sloppyBin, sloppyc: options.sloppycBin };
  const tools = detectTools(overrides);
  tools.sloppy.path = resolveSloppy(tools, options.sloppyBin);
  tools.sloppy.status = tools.sloppy.path ? "AVAILABLE" : "UNAVAILABLE";
  const tool = options.checkTools ? null : selectLoadTool(tools, options.tool);
  if (options.checkTools) {
    console.log(JSON.stringify({ status: "PASS", tools }, null, 2));
    return;
  }
  const matrix = await loadMatrix(options);
  const workloads = await loadWorkloads(matrix.workloads);
  const outDir = options.out ?? defaultOut();
  const rawDir = path.join(outDir, "raw");
  const tempDir = path.join(outDir, "tmp");
  await fs.mkdir(rawDir, { recursive: true });
  await fs.mkdir(tempDir, { recursive: true });
  await fs.writeFile(path.join(outDir, "matrix.json"), `${JSON.stringify(matrix, null, 2)}\n`, "utf8");
  const selectedRuntimes = options.runtimes.includes("all") ? ["sloppy", "node", "bun", "deno"] : options.runtimes;
  const results = [];
  const startedAt = new Date().toISOString();
  const baseReport = {
    schemaVersion: 1,
    startedAt,
    tool,
    tools,
    environment: hostMetadata(),
    git: gitInfo(),
    gitCommit: gitCommit(),
    workloads,
    matrix,
    options: {
      claimMode: options.claimMode,
      loadHostKind: options.loadHostKind,
      resourceSampling: options.resourceSampling,
      resourceIntervalMs: options.resourceIntervalMs,
    },
    reproductionCommand: `node benchmarks/local-neutral/scripts/run.mjs ${process.argv.slice(2).join(" ")}`,
  };
  await fs.writeFile(path.join(outDir, "environment.json"), `${JSON.stringify({ ...baseReport.environment, tools, git: baseReport.git, gitCommit: baseReport.gitCommit, startedAt: baseReport.startedAt, options: baseReport.options }, null, 2)}\n`, "utf8");
  let currentProgress = null;
  const writeProgress = async () => {
    const progress = progressState({ results, matrix, selectedRuntimes, workloads, current: currentProgress });
    const partial = makeReport({ baseReport, results, options, partial: true });
    await writeJson(path.join(outDir, "progress.json"), progress);
    await writeJson(path.join(outDir, "results.partial.json"), results);
    await writeJson(path.join(outDir, "summary.partial.json"), {
      partial: true,
      updatedAt: partial.updatedAt,
      summary: partial.summary,
      comparisons: partial.comparisons,
      publicClaimReadiness: partial.publicClaimReadiness,
      progress,
    });
    await fs.writeFile(path.join(outDir, "report.partial.md"), renderMarkdown(partial), "utf8");
  };
  await writeProgress();
  const stopHandlers = [];
  process.once("SIGINT", async () => {
    await Promise.all(stopHandlers.map((fn) => fn()));
    process.exit(130);
  });
  for (const runtime of selectedRuntimes) {
    if (!runtimeAvailable(tools, runtime)) {
      for (const workload of workloads) {
        for (const connections of matrix.connections) {
          results.push({ status: "UNAVAILABLE", runtime, workload: workload.name, connections, reason: `${runtime} not available` });
          currentProgress = { runtime, workload: workload.name, connections, status: "UNAVAILABLE" };
          await writeProgress();
        }
      }
      continue;
    }
    for (const workload of workloads) {
      for (const connections of matrix.connections) {
        if (runtime === "sloppy" && workload.name === "auth-api-key") {
          results.push({
            status: "SKIPPED",
            runtime,
            workload: workload.name,
            connections,
            reason: "Sloppy Auth.apiKey source-input config handoff is not reliable in this local-neutral fixture yet; skipped instead of faking auth behavior.",
          });
          currentProgress = { runtime, workload: workload.name, connections, status: "SKIPPED" };
          await writeProgress();
          continue;
        }
        const port = options.basePort + selectedRuntimes.indexOf(runtime);
        const logDir = path.join(rawDir, `${runtime}-${workload.name}-${connections}`);
        let server = null;
        try {
          const workDir = path.join(tempDir, `${runtime}-${workload.name}-${connections}`);
          await fs.mkdir(workDir, { recursive: true });
          const command = serverCommand(runtime, tools, options, port, workDir);
          server = await startServer({ ...command, logDir, host: options.host, port });
          stopHandlers.push(() => stopServer(server.child));
          const contractValidation = await validateWorkload({
            workload,
            baseUrl: `http://${options.host}:${port}`,
          });
          await runAdapter(tool, {
            toolPath: tools[tool].path,
            runtime,
            workload,
            url: `http://${options.host}:${port}${workload.path ?? ""}`,
            connections,
            duration: matrix.warmup,
            repeat: 0,
            tempDir,
            runLabel: [runtime, workload.name, connections, "warmup"].map(safeToken).join("-"),
          });
          for (let repeat = 1; repeat <= matrix.repeats; repeat += 1) {
            currentProgress = { runtime, workload: workload.name, connections, repeat, status: "RUNNING" };
            await writeProgress();
            const sampler = options.resourceSampling
              ? startResourceSampler(server.pid, { intervalMs: options.resourceIntervalMs })
              : null;
            const row = await runAdapter(tool, {
              toolPath: tools[tool].path,
              runtime,
              workload,
              url: `http://${options.host}:${port}${workload.path ?? ""}`,
              connections,
              duration: matrix.duration,
              repeat,
              tempDir,
              runLabel: [runtime, workload.name, connections, repeat].map(safeToken).join("-"),
            });
            let resourceRun = null;
            if (sampler != null) {
              resourceRun = sampler.stop();
              const samplePath = path.join(logDir, `resources-repeat-${repeat}.json`);
              await fs.writeFile(samplePath, `${JSON.stringify(resourceRun.samples, null, 2)}\n`, "utf8");
              row.serverResources = resourceRun.summary;
              row.serverResourceSamplesPath = samplePath;
            }
            results.push({
              ...row,
              runtime,
              workload: workload.name,
              connections,
              durationSeconds: seconds(matrix.duration),
              contractValidation,
              server: { pid: server.pid, command: server.command, startupMs: server.startupMs, stdoutPath: server.stdoutPath, stderrPath: server.stderrPath },
            });
            currentProgress = { runtime, workload: workload.name, connections, repeat, status: row.status };
            await writeProgress();
          }
        } catch (error) {
          results.push({ status: "FAIL", runtime, workload: workload.name, connections, reason: error.message });
          currentProgress = { runtime, workload: workload.name, connections, status: "FAIL" };
          await writeProgress();
        } finally {
          if (server) await stopServer(server.child);
        }
      }
    }
  }
  currentProgress = { status: "COMPLETE" };
  await writeProgress();
  const report = makeReport({ baseReport, results, options, partial: false });
  await fs.writeFile(path.join(outDir, "results.json"), `${JSON.stringify(results, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "summary.json"), `${JSON.stringify({ summary: report.summary, comparisons: report.comparisons }, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "report.md"), renderMarkdown(report), "utf8");
  const csv = ["workload,connections,runtime,medianRps,meanRps,p50Ms,p95Ms,p99Ms,errors,non2xx,peakRssBytes,peakPrivateMemoryBytes,avgCpuPercent"]
    .concat(report.summary.map((row) => [row.workload, row.connections, row.runtime, row.medianRps, row.meanRps, row.p50Ms, row.p95Ms, row.p99Ms, row.errors, row.non2xx, row.peakRssBytes, row.peakPrivateMemoryBytes, row.avgCpuPercent].join(",")))
    .join("\n");
  await fs.writeFile(path.join(outDir, "report.csv"), `${csv}\n`, "utf8");
  const hasFailure = results.some((row) => row.status === "FAIL");
  const hasPass = results.some((row) => row.status === "PASS");
  const output = { status: !hasFailure && hasPass ? "PASS" : "FAIL", out: outDir, tool };
  if (options.json) console.log(JSON.stringify(output));
  else console.log(`local-neutral benchmark ${output.status}: ${outDir}`);
  if (output.status !== "PASS") process.exitCode = 1;
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
