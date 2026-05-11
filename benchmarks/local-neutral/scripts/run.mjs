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
  --preset quick|alpha|full
  --check-tools
  --json`);
}

async function readJson(file) {
  return JSON.parse(await fs.readFile(file, "utf8"));
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
          await runAdapter(tool, {
            toolPath: tools[tool].path,
            workload,
            url: `http://${options.host}:${port}${workload.path ?? ""}`,
            connections,
            duration: matrix.warmup,
            repeat: 0,
            tempDir,
          });
          for (let repeat = 1; repeat <= matrix.repeats; repeat += 1) {
            const row = await runAdapter(tool, {
              toolPath: tools[tool].path,
              workload,
              url: `http://${options.host}:${port}${workload.path ?? ""}`,
              connections,
              duration: matrix.duration,
              repeat,
              tempDir,
            });
            results.push({
              ...row,
              runtime,
              workload: workload.name,
              connections,
              durationSeconds: seconds(matrix.duration),
              server: { pid: server.pid, command: server.command, startupMs: server.startupMs, stdoutPath: server.stdoutPath, stderrPath: server.stderrPath },
            });
          }
        } catch (error) {
          results.push({ status: "FAIL", runtime, workload: workload.name, connections, reason: error.message });
        } finally {
          if (server) await stopServer(server.child);
        }
      }
    }
  }
  const report = {
    schemaVersion: 1,
    startedAt: new Date().toISOString(),
    tool,
    tools,
    environment: hostMetadata(),
    gitCommit: gitCommit(),
    workloads,
    matrix,
    results,
    summary: summarize(results),
    comparisons: comparisons(summarize(results)),
    reproductionCommand: `node benchmarks/local-neutral/scripts/run.mjs ${process.argv.slice(2).join(" ")}`,
  };
  await fs.writeFile(path.join(outDir, "environment.json"), `${JSON.stringify({ ...report.environment, tools, gitCommit: report.gitCommit, startedAt: report.startedAt }, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "results.json"), `${JSON.stringify(results, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "summary.json"), `${JSON.stringify({ summary: report.summary, comparisons: report.comparisons }, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outDir, "report.md"), renderMarkdown(report), "utf8");
  const csv = ["workload,connections,runtime,medianRps,meanRps,p50Ms,p95Ms,p99Ms,errors,non2xx"]
    .concat(report.summary.map((row) => [row.workload, row.connections, row.runtime, row.medianRps, row.meanRps, row.p50Ms, row.p95Ms, row.p99Ms, row.errors, row.non2xx].join(",")))
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
