#!/usr/bin/env node
import fs from "node:fs/promises";
import fsSync from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { performance } from "node:perf_hooks";
import { spawn, spawnSync } from "node:child_process";
import { runLoad, singleRequest } from "./load-generator.mjs";
import { createProcessSampler } from "./process-metrics.mjs";
import { writeReports } from "./report.mjs";

const repoRoot = path.resolve(fileURLToPath(new URL("../../..", import.meta.url)));
const appsRoot = path.join(repoRoot, "benchmarks", "realistic", "apps");
const host = "127.0.0.1";

const BASIC_WORKLOADS = [
  {
    name: "health",
    description: "GET /health returns plain text ok; dispatch and small write baseline.",
    request: { method: "GET", path: "/health" },
    expectedStatus: 200,
    validate: (response) => response.body === "ok",
  },
  {
    name: "json-small",
    aliases: ["json"],
    description: "GET /json returns a small stable JSON object.",
    request: { method: "GET", path: "/json" },
    expectedStatus: 200,
    validate: (response) => {
      const body = JSON.parse(response.body);
      return body.message === "hello" && body.ok === true && body.count === 42;
    },
  },
  {
    name: "route-param",
    description: "GET /users/123 returns a JSON object with a route parameter.",
    request: { method: "GET", path: "/users/123" },
    expectedStatus: 200,
    validate: (response) => {
      const body = JSON.parse(response.body);
      return body.id === 123 && body.name === "Ada Lovelace";
    },
  },
  {
    name: "query",
    description: "GET /search parses q, page, and limit query values.",
    request: { method: "GET", path: "/search?q=ada&page=2&limit=10" },
    expectedStatus: 200,
    validate: (response) => {
      const body = JSON.parse(response.body);
      return body.q === "ada" && body.page === 2 && body.limit === 10 && Array.isArray(body.results);
    },
  },
  {
    name: "post-json-small",
    description: "POST /echo parses a small JSON request body and echoes selected fields.",
    request: {
      method: "POST",
      path: "/echo",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ name: "Ada", count: 42 }),
    },
    expectedStatus: 200,
    validate: (response) => {
      const body = JSON.parse(response.body);
      return body.name === "Ada" && body.count === 42;
    },
  },
  {
    name: "middleware-request-id",
    description: "GET /middleware propagates or generates a request ID.",
    request: { method: "GET", path: "/middleware", headers: { "x-request-id": "bench-request" } },
    expectedStatus: 200,
    validate: (response) => {
      const body = JSON.parse(response.body);
      return typeof body.requestId === "string" && body.requestId.length > 0;
    },
  },
  {
    name: "static-ish-payload",
    description: "GET /payload/64kb returns a stable 64 KiB text payload.",
    request: { method: "GET", path: "/payload/64kb" },
    expectedStatus: 200,
    validate: (response) => response.body.length === 64 * 1024,
  },
  {
    name: "mixed-realistic",
    description: "Weighted local mix: 40% health/json, 25% route param, 15% query, 10% post JSON, 10% misses.",
    expectedStatus: 200,
    request: { method: "GET", path: "/health" },
    requests: [
      { method: "GET", path: "/health", weight: 20 },
      { method: "GET", path: "/json", weight: 20 },
      { method: "GET", path: "/users/123", weight: 25 },
      { method: "GET", path: "/search?q=ada&page=2&limit=10", weight: 15 },
      {
        method: "POST",
        path: "/echo",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ name: "Ada", count: 42 }),
        weight: 10,
      },
      { method: "GET", path: "/missing", weight: 10 },
    ],
    validate: (response) => response.body === "ok",
  },
];

function largeRouteWorkloads(sizes) {
  const workloads = [];
  for (const size of sizes) {
    const positions = [
      ["first", 0],
      ["middle", Math.floor(size / 2)],
      ["last", size - 1],
    ];
    for (const [position, route] of positions) {
      workloads.push({
        name: `large-route-table-hit-${size}-${position}`,
        family: "large-route-table-hit",
        description: `GET /routes/${route} against a generated ${size}-route table (${position} hit).`,
        routeCount: size,
        routeProfile: true,
        request: { method: "GET", path: `/routes/${route}` },
        expectedStatus: 200,
        validate: (response) => JSON.parse(response.body).route === route,
      });
    }
    workloads.push({
      name: `large-route-table-miss-${size}`,
      family: "large-route-table-miss",
      description: `GET /routes/missing against a generated ${size}-route table.`,
      routeCount: size,
      routeProfile: true,
      request: { method: "GET", path: "/routes/missing" },
      expectedStatus: 404,
      validate: (response) => response.statusCode === 404,
    });
  }
  return workloads;
}

function parseArgs(argv) {
  const options = {
    suite: "http",
    runtimes: ["sloppy", "node", "bun", "deno"],
    categories: null,
    workloads: null,
    durationSeconds: null,
    warmupSeconds: null,
    connections: null,
    iterations: null,
    out: path.join(repoRoot, "artifacts", "bench", "realistic"),
    requireRuntimes: [],
    quick: false,
    full: false,
    dryRun: false,
    sloppyExe: process.env.SLOPPY_EXE ?? null,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    const value = () => {
      index += 1;
      if (index >= argv.length) {
        throw new Error(`Missing value for ${arg}`);
      }
      return argv[index];
    };
    switch (arg) {
      case "--suite":
        options.suite = value();
        break;
      case "--runtime":
      case "--runtimes":
        options.runtimes = splitList(value());
        break;
      case "--category":
      case "--variant":
        options.categories = splitList(value());
        break;
      case "--workload":
      case "--workloads":
        options.workloads = splitList(value());
        break;
      case "--duration-seconds":
        options.durationSeconds = Number(value());
        break;
      case "--warmup-seconds":
        options.warmupSeconds = Number(value());
        break;
      case "--connections":
        options.connections = splitList(value()).map(Number);
        break;
      case "--iterations":
        options.iterations = Number(value());
        break;
      case "--out":
        options.out = path.resolve(value());
        break;
      case "--require-runtime":
      case "--require-runtimes":
        options.requireRuntimes = splitList(value());
        break;
      case "--sloppy-exe":
        options.sloppyExe = path.resolve(value());
        break;
      case "--quick":
        options.quick = true;
        break;
      case "--full":
        options.full = true;
        break;
      case "--dry-run":
        options.dryRun = true;
        break;
      case "--help":
      case "-h":
        printHelp();
        process.exit(0);
      default:
        throw new Error(`Unknown argument: ${arg}`);
    }
  }
  applyModeDefaults(options);
  return options;
}

function splitList(value) {
  return String(value)
    .split(",")
    .map((item) => item.trim())
    .filter(Boolean);
}

function applyModeDefaults(options) {
  if (options.full) {
    options.durationSeconds ??= 60;
    options.warmupSeconds ??= 15;
    options.iterations ??= 7;
    options.connections ??= [1, 16, 64, 256, 512];
    options.categories ??= ["baseline", "framework", "feature-rich"];
    return;
  }
  if (options.quick) {
    options.durationSeconds ??= 5;
    options.warmupSeconds ??= 2;
    options.iterations ??= 1;
    options.connections ??= [1, 16];
    options.categories ??= ["framework"];
    options.workloads ??= ["health", "json-small", "route-param"];
    return;
  }
  options.durationSeconds ??= options.suite === "stress" ? 600 : 30;
  options.warmupSeconds ??= options.suite === "stress" ? 15 : 10;
  options.iterations ??= options.suite === "stress" ? 1 : 5;
  options.connections ??= options.suite === "stress" ? [64] : [1, 16, 64, 256];
  options.categories ??= ["baseline", "framework", "feature-rich"];
}

function printHelp() {
  console.log(`Usage: node benchmarks/realistic/runner/bench-realistic.mjs [options]

Options:
  --suite http|startup|stress|all
  --runtime sloppy,node,bun,deno
  --category baseline,framework,feature-rich
  --workload health,json-small,route-param,query,post-json-small,middleware-request-id,large-routes,static-ish-payload,mixed-realistic
  --duration-seconds 30
  --warmup-seconds 10
  --connections 1,16,64
  --iterations 5
  --out artifacts/bench/realistic
  --require-runtime sloppy,node
  --sloppy-exe path/to/sloppy
  --quick
  --full
  --dry-run`);
}

function routeSizes(options) {
  if (options.full) {
    return [100, 1000, 5000];
  }
  if (options.quick) {
    return [100];
  }
  return [100, 1000];
}

function expandWorkloads(options) {
  const all = [...BASIC_WORKLOADS, ...largeRouteWorkloads(routeSizes(options))];
  const aliases = new Map();
  for (const workload of all) {
    aliases.set(workload.name, [workload]);
    for (const alias of workload.aliases ?? []) {
      aliases.set(alias, [workload]);
    }
  }
  aliases.set("large-route-table-hit", all.filter((item) => item.family === "large-route-table-hit"));
  aliases.set("large-route-table-miss", all.filter((item) => item.family === "large-route-table-miss"));
  aliases.set("large-routes", all.filter((item) => item.routeProfile));
  if (options.suite === "stress") {
    return all.filter((item) => item.name === "mixed-realistic");
  }
  if (!options.workloads) {
    return all;
  }
  const selected = [];
  for (const name of options.workloads) {
    const found = aliases.get(name);
    if (!found) {
      throw new Error(`Unknown workload: ${name}`);
    }
    selected.push(...found);
  }
  return Array.from(new Map(selected.map((item) => [item.name, item])).values());
}

function commandOutput(command, args = [], options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd ?? repoRoot,
    encoding: "utf8",
    windowsHide: true,
  });
  if (result.error || result.status !== 0) {
    return null;
  }
  return result.stdout.trim() || result.stderr.trim();
}

function findOnPath(name) {
  const command = process.platform === "win32" ? "where.exe" : "which";
  const result = spawnSync(command, [name], { encoding: "utf8", windowsHide: true });
  if (result.status !== 0) {
    return null;
  }
  return result.stdout.split(/\r?\n/).map((line) => line.trim()).find(Boolean) ?? null;
}

function fileIfExists(candidate) {
  return candidate && fsSync.existsSync(candidate) ? candidate : null;
}

function resolveSloppyPath(explicit) {
  if (explicit) {
    return fileIfExists(explicit);
  }
  const candidates = [
    path.join(repoRoot, "build", "windows-relwithdebinfo", "sloppy.exe"),
    path.join(repoRoot, "build", "windows-release", "sloppy.exe"),
    path.join(repoRoot, "build", "unix-relwithdebinfo", "sloppy"),
    path.join(repoRoot, "build", "unix-release", "sloppy"),
    findOnPath("sloppy"),
  ];
  return candidates.map(fileIfExists).find(Boolean) ?? null;
}

function resolveSloppycPath() {
  const candidates = [
    path.join(repoRoot, "compiler", "target", "release", "sloppyc.exe"),
    path.join(repoRoot, "compiler", "target", "release", "sloppyc"),
    path.join(repoRoot, "compiler", "target", "debug", "sloppyc.exe"),
    path.join(repoRoot, "compiler", "target", "debug", "sloppyc"),
    findOnPath("sloppyc"),
  ];
  return candidates.map(fileIfExists).find(Boolean) ?? null;
}

function detectRuntime(name, options) {
  if (name === "sloppy") {
    const resolved = resolveSloppyPath(options.sloppyExe);
    if (!resolved) {
      return { status: "UNAVAILABLE", version: null, path: null };
    }
    return {
      status: "AVAILABLE",
      version: commandOutput(resolved, ["--version"]) ?? "unknown",
      path: resolved,
    };
  }
  const resolved = findOnPath(name);
  if (!resolved) {
    return { status: "UNAVAILABLE", version: null, path: null };
  }
  return {
    status: "AVAILABLE",
    version: commandOutput(resolved, ["--version"]) ?? "unknown",
    path: resolved,
  };
}

function detectTools(options) {
  const tools = {};
  for (const runtime of ["sloppy", "node", "bun", "deno"]) {
    tools[runtime] = detectRuntime(runtime, options);
  }
  const sloppycPath = resolveSloppycPath();
  tools.sloppyc = sloppycPath
    ? { status: "AVAILABLE", version: commandOutput(sloppycPath, ["--version"]) ?? "unknown", path: sloppycPath }
    : { status: "UNAVAILABLE", version: null, path: null };
  tools.loadGenerator = {
    name: "node-internal-http-keepalive",
    version: process.version,
    path: process.execPath,
  };
  return tools;
}

function hostInfo() {
  return {
    os: os.type(),
    release: os.release(),
    arch: os.arch(),
    cpu: os.cpus()[0]?.model ?? "unknown",
    logicalCores: os.cpus().length,
    memoryBytes: os.totalmem(),
    gitCommit: commandOutput("git", ["rev-parse", "HEAD"]),
    gitBranch: commandOutput("git", ["branch", "--show-current"]),
  };
}

async function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, host, () => {
      const address = server.address();
      server.close(() => resolve(address.port));
    });
    server.on("error", reject);
  });
}

function spawnCapture(command, args, options = {}) {
  return new Promise((resolve) => {
    const stdout = [];
    const stderr = [];
    const started = performance.now();
    const child = spawn(command, args, {
      cwd: options.cwd ?? repoRoot,
      env: { ...process.env, ...(options.env ?? {}) },
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"],
    });
    const timeout = options.timeoutMs
      ? setTimeout(() => child.kill(), options.timeoutMs)
      : null;
    child.stdout.on("data", (chunk) => stdout.push(chunk.toString("utf8")));
    child.stderr.on("data", (chunk) => stderr.push(chunk.toString("utf8")));
    child.on("error", (error) => {
      if (timeout) {
        clearTimeout(timeout);
      }
      resolve({
        status: 1,
        stdout: stdout.join(""),
        stderr: `${stderr.join("")}${error.message}`,
        durationMs: performance.now() - started,
      });
    });
    child.on("exit", (status) => {
      if (timeout) {
        clearTimeout(timeout);
      }
      resolve({
        status,
        stdout: stdout.join(""),
        stderr: stderr.join(""),
        durationMs: performance.now() - started,
      });
    });
  });
}

function generateSloppySource(category, routeCount = 0) {
  const imports = category === "baseline"
    ? 'import { Sloppy, Results, Body, Query, RequestContext, Route } from "sloppy";'
    : 'import { Sloppy, Results, Body, Query, RequestContext, RequestId, Route } from "sloppy";';
  const lines = [imports, ""];
  lines.push("type EchoBody = { name: string; count: number };");
  if (category === "feature-rich") {
    lines.push("function auditMiddleware(ctx, next) { return next(); }");
    lines.push("function quietRequestLogging(ctx, next) { return next(); }");
    lines.push('const builder = Sloppy.createBuilder();');
    lines.push('builder.services.addSingleton("BenchClock", () => ({ now: "2026-05-10T00:00:00Z" }));');
    lines.push("const app = builder.build();");
  } else {
    lines.push("const app = Sloppy.create();");
  }
  if (category !== "baseline") {
    lines.push('app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));');
  }
  if (category === "feature-rich") {
    lines.push("app.use(quietRequestLogging);");
    lines.push("app.use(auditMiddleware);");
    lines.push("app.useCors({");
    lines.push('  origins: ["https://app.example.com"],');
    lines.push('  headers: ["content-type", "x-request-id"],');
    lines.push('  exposedHeaders: ["x-request-id"],');
    lines.push("  credentials: true,");
    lines.push("  maxAgeSeconds: 600,");
    lines.push("});");
  }
  lines.push('app.get("/health", () => Results.text("ok"));');
  lines.push('app.get("/json", () => Results.json({ message: "hello", ok: true, count: 42 }));');
  lines.push('app.get("/users/{id:int}", (id: Route<number>) => Results.json({ id, name: "Ada Lovelace" }));');
  lines.push("app.get(\"/search\", (q: Query<string>, page: Query<number>, limit: Query<number>) => Results.json({");
  lines.push("  q,");
  lines.push("  page,");
  lines.push("  limit,");
  lines.push("  results: [],");
  lines.push("}));");
  lines.push('app.post("/echo", (body: Body<EchoBody>) => Results.json({ name: body.name, count: body.count }));');
  if (category === "baseline") {
    lines.push('app.get("/middleware", (ctx: RequestContext) => Results.json({ requestId: "generated" }));');
  } else {
    lines.push('app.get("/middleware", (ctx: RequestContext) => Results.json({ requestId: ctx.requestId }));');
  }
  lines.push(`app.get("/payload/64kb", () => Results.text("${"x".repeat(64 * 1024)}"));`);
  for (let i = 0; i < routeCount; i += 1) {
    lines.push(`app.get("/routes/${i}", () => Results.json({ route: ${i} }));`);
  }
  lines.push("");
  lines.push("export default app;");
  lines.push("");
  return lines.join("\n");
}

function sloppyBenchmarkAppsettings(options) {
  const maxConnections = Math.max(128, ...options.connections);
  return {
    Sloppy: {
      Server: {
        MaxConnections: maxConnections,
        MaxRequestsPerConnection: 0,
        KeepAliveIdleTimeoutMs: 60000,
        RequestTimeoutMs: 60000,
      },
    },
  };
}

async function buildSloppyApp(runtime, category, workload, outDir, buildCache, options) {
  const profile = workload.routeProfile ? `routes-${workload.routeCount}` : "main";
  const key = `${category}:${profile}:${Math.max(...options.connections)}`;
  if (buildCache.has(key)) {
    return buildCache.get(key);
  }
  const workDir = path.join(outDir, "work", "sloppy", category, profile);
  const sourcePath = path.join(workDir, "app.ts");
  const appsettingsPath = path.join(workDir, "appsettings.json");
  const artifactDir = path.join(workDir, ".sloppy");
  await fs.mkdir(workDir, { recursive: true });
  await fs.writeFile(sourcePath, generateSloppySource(category, workload.routeProfile ? workload.routeCount : 0), "utf8");
  await fs.writeFile(appsettingsPath, `${JSON.stringify(sloppyBenchmarkAppsettings(options), null, 2)}\n`, "utf8");
  const started = performance.now();
  const build = await spawnCapture(runtime.path, [
    "build",
    sourcePath,
    "--out",
    artifactDir,
    "--host",
    host,
    "--port",
    "5173",
    "--kind",
    "web",
  ], { timeoutMs: 120000 });
  const metadata = await sloppyArtifactMetadata(artifactDir).catch(() => ({}));
  const prepared = {
    ok: build.status === 0,
    runtime: "sloppy",
    category,
    profile,
    artifactDir,
    sourcePath,
    buildDurationMs: performance.now() - started,
    stdout: build.stdout,
    stderr: build.stderr,
    metadata,
  };
  buildCache.set(key, prepared);
  return prepared;
}

async function sloppyArtifactMetadata(artifactDir) {
  async function size(name) {
    const stat = await fs.stat(path.join(artifactDir, name));
    return stat.size;
  }
  let plan = {};
  try {
    plan = JSON.parse(await fs.readFile(path.join(artifactDir, "app.plan.json"), "utf8"));
  } catch {
    plan = {};
  }
  return {
    appPlanBytes: await size("app.plan.json"),
    appJsBytes: await size("app.js"),
    appJsMapBytes: await size("app.js.map"),
    routeCount: Array.isArray(plan.routes) ? plan.routes.length : null,
    planKind: plan.kind ?? null,
    requiredFeatures: plan.requiredFeatures ?? null,
    serverConfig: Object.fromEntries(
      (plan.configuration?.keys ?? [])
        .filter((entry) => typeof entry.key === "string" && entry.key.startsWith("Sloppy:Server:"))
        .map((entry) => [entry.key, entry.value]),
    ),
  };
}

async function prepareApp(runtimeName, runtime, category, workload, outDir, buildCache, options) {
  if (runtimeName === "sloppy") {
    return buildSloppyApp(runtime, category, workload, outDir, buildCache, options);
  }
  return {
    ok: true,
    runtime: runtimeName,
    category,
    profile: workload.routeProfile ? `routes-${workload.routeCount}` : "main",
    serverPath: path.join(appsRoot, runtimeName, category, "server.mjs"),
    routeCount: workload.routeProfile ? workload.routeCount : 0,
    buildDurationMs: null,
    metadata: {
      routeCount: workload.routeProfile ? workload.routeCount : null,
    },
    stdout: "",
    stderr: "",
  };
}

function startServer(runtimeName, runtime, prepared, port) {
  const stdout = [];
  const stderr = [];
  let command;
  let args;
  const env = {
    HOST: host,
    PORT: String(port),
    ROUTE_COUNT: String(prepared.routeCount ?? 0),
  };
  if (runtimeName === "sloppy") {
    command = runtime.path;
    args = ["run", "--artifacts", prepared.artifactDir, "--host", host, "--port", String(port)];
  } else if (runtimeName === "node") {
    command = runtime.path;
    args = [prepared.serverPath];
  } else if (runtimeName === "bun") {
    command = runtime.path;
    args = [prepared.serverPath];
  } else if (runtimeName === "deno") {
    command = runtime.path;
    args = ["run", "--allow-net", "--allow-env", prepared.serverPath];
  } else {
    throw new Error(`Unsupported runtime ${runtimeName}`);
  }
  const child = spawn(command, args, {
    cwd: repoRoot,
    env: { ...process.env, ...env },
    windowsHide: true,
    stdio: ["ignore", "pipe", "pipe"],
  });
  child.stdout.on("data", (chunk) => stdout.push(chunk.toString("utf8")));
  child.stderr.on("data", (chunk) => stderr.push(chunk.toString("utf8")));
  return { child, stdout, stderr, command, args };
}

async function stopServer(server) {
  if (server.child.exitCode !== null || server.child.signalCode !== null) {
    return;
  }
  server.child.kill("SIGINT");
  await new Promise((resolve) => {
    const timer = setTimeout(() => {
      if (server.child.exitCode === null && server.child.signalCode === null) {
        server.child.kill();
      }
      resolve();
    }, 3000);
    server.child.once("exit", () => {
      clearTimeout(timer);
      resolve();
    });
  });
}

async function waitForHealth(baseUrl, timeoutMs = 15000) {
  const deadline = performance.now() + timeoutMs;
  let last = null;
  while (performance.now() < deadline) {
    const response = await singleRequest(baseUrl, { method: "GET", path: "/health" }, { timeoutMs: 1000 });
    if (response.ok && response.statusCode === 200 && response.body === "ok") {
      return { ok: true };
    }
    last = response;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  return { ok: false, last };
}

async function validateWorkload(baseUrl, workload) {
  const response = await singleRequest(baseUrl, workload.request, { timeoutMs: 5000 });
  if (!response.ok) {
    return { ok: false, reason: response.error ?? "request failed", response };
  }
  if (response.statusCode !== workload.expectedStatus) {
    return { ok: false, reason: `expected status ${workload.expectedStatus}, got ${response.statusCode}`, response };
  }
  try {
    if (workload.validate && !workload.validate(response)) {
      return { ok: false, reason: "response validation failed", response };
    }
  } catch (error) {
    return { ok: false, reason: `response validation threw: ${error.message}`, response };
  }
  return { ok: true, response };
}

function emptyRunBase({ runtimeName, category, workload, options, iteration, connections }) {
  return {
    runtime: runtimeName,
    workload: workload.name,
    variant: category,
    status: "FAIL",
    durationSeconds: options.durationSeconds,
    warmupSeconds: options.warmupSeconds,
    connections,
    pipeline: 1,
    iteration,
    requestsPerSecond: 0,
    latency: {
      avgMs: null,
      p50Ms: null,
      p75Ms: null,
      p90Ms: null,
      p95Ms: null,
      p99Ms: null,
      maxMs: null,
    },
    transfer: {
      bytesPerSecond: 0,
      totalRequests: 0,
      errors: 0,
      timeouts: 0,
      non2xx: 0,
    },
    process: {
      peakWorkingSetBytes: null,
      avgWorkingSetBytes: null,
      cpuUserMs: null,
      cpuKernelMs: null,
    },
    sloppy: {},
    artifacts: {
      stdout: null,
      stderr: null,
      rawLoadOutput: null,
    },
  };
}

function safeName(value) {
  return value.replace(/[^A-Za-z0-9_.-]+/g, "-");
}

async function writeRawArtifacts(outDir, runId, stdout, stderr, raw) {
  const rawDir = path.join(outDir, "raw");
  await fs.mkdir(rawDir, { recursive: true });
  const stdoutPath = path.join(rawDir, `${runId}.stdout.log`);
  const stderrPath = path.join(rawDir, `${runId}.stderr.log`);
  const rawPath = path.join(rawDir, `${runId}.load.json`);
  await fs.writeFile(stdoutPath, stdout, "utf8");
  await fs.writeFile(stderrPath, stderr, "utf8");
  await fs.writeFile(rawPath, `${JSON.stringify(raw, null, 2)}\n`, "utf8");
  return { stdout: stdoutPath, stderr: stderrPath, rawLoadOutput: rawPath };
}

async function runMeasurement({ runtimeName, runtime, category, workload, options, outDir, buildCache, iteration, connections }) {
  const base = emptyRunBase({ runtimeName, category, workload, options, iteration, connections });
  const prepared = await prepareApp(runtimeName, runtime, category, workload, outDir, buildCache, options);
  base.sloppy = runtimeName === "sloppy"
    ? { buildDurationMs: prepared.buildDurationMs, ...prepared.metadata }
    : {};
  if (!prepared.ok) {
    base.reason = "server build failed";
    const runId = safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-build`);
    base.artifacts = await writeRawArtifacts(outDir, runId, prepared.stdout, prepared.stderr, {});
    return base;
  }

  const port = await freePort();
  const baseUrl = `http://${host}:${port}`;
  const server = startServer(runtimeName, runtime, prepared, port);
  let sampler = null;
  try {
    const ready = await waitForHealth(baseUrl);
    if (!ready.ok) {
      base.reason = "server did not become ready";
      const runId = safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-startup`);
      base.artifacts = await writeRawArtifacts(outDir, runId, server.stdout.join(""), server.stderr.join(""), ready);
      return base;
    }
    const validation = await validateWorkload(baseUrl, workload);
    if (!validation.ok) {
      base.reason = validation.reason;
      const runId = safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-validation`);
      base.artifacts = await writeRawArtifacts(outDir, runId, server.stdout.join(""), server.stderr.join(""), validation);
      return base;
    }
    sampler = createProcessSampler(server.child.pid);
    const load = await runLoad({
      baseUrl,
      request: workload.request,
      requests: workload.requests ?? [workload.request],
      connections,
      durationSeconds: options.durationSeconds,
      warmupSeconds: options.warmupSeconds,
      timeoutMs: 5000,
      seed: hashSeed(`${runtimeName}:${category}:${workload.name}:${connections}:${iteration}`),
    });
    const processStats = await sampler.stop();
    sampler = null;
    base.status = load.transfer.errors === 0 && load.transfer.timeouts === 0 ? "PASS" : "FAIL";
    base.reason = base.status === "PASS" ? undefined : "load generator observed request errors";
    base.requestsPerSecond = load.requestsPerSecond;
    base.latency = load.latency;
    base.transfer = load.transfer;
    base.process = {
      peakWorkingSetBytes: processStats.peakWorkingSetBytes,
      avgWorkingSetBytes: processStats.avgWorkingSetBytes,
      cpuUserMs: processStats.cpuUserMs,
      cpuKernelMs: processStats.cpuKernelMs,
      cpuTotalMs: processStats.cpuTotalMs,
    };
    const runId = safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}`);
    base.artifacts = await writeRawArtifacts(outDir, runId, server.stdout.join(""), server.stderr.join(""), {
      load,
      processSamples: processStats.samples,
    });
    return base;
  } finally {
    if (sampler) {
      await sampler.stop().catch(() => {});
    }
    await stopServer(server).catch(() => {});
  }
}

async function runStartupMeasurement({ runtimeName, runtime, category, workload, options, outDir, buildCache, iteration }) {
  const base = emptyRunBase({ runtimeName, category, workload, options, iteration, connections: 1 });
  base.durationSeconds = 0;
  base.warmupSeconds = 0;
  const prepared = await prepareApp(runtimeName, runtime, category, workload, outDir, buildCache, options);
  base.sloppy = runtimeName === "sloppy"
    ? { buildDurationMs: prepared.buildDurationMs, ...prepared.metadata }
    : {};
  if (!prepared.ok) {
    base.reason = "server build failed";
    const runId = safeName(`${runtimeName}-${category}-${workload.name}-${iteration}-startup-build`);
    base.artifacts = await writeRawArtifacts(outDir, runId, prepared.stdout, prepared.stderr, {});
    return base;
  }
  const port = await freePort();
  const baseUrl = `http://${host}:${port}`;
  const started = performance.now();
  const server = startServer(runtimeName, runtime, prepared, port);
  try {
    const ready = await waitForHealth(baseUrl);
    base.startupMs = performance.now() - started;
    base.status = ready.ok ? "PASS" : "FAIL";
    base.reason = ready.ok ? undefined : "server did not become ready";
    const runId = safeName(`${runtimeName}-${category}-${workload.name}-${iteration}-startup`);
    base.artifacts = await writeRawArtifacts(outDir, runId, server.stdout.join(""), server.stderr.join(""), ready);
    return base;
  } finally {
    await stopServer(server).catch(() => {});
  }
}

function hashSeed(value) {
  let hash = 2166136261;
  for (let i = 0; i < value.length; i += 1) {
    hash ^= value.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function unavailableRun(runtimeName, reason, options) {
  return {
    runtime: runtimeName,
    workload: "*",
    variant: "*",
    status: "UNAVAILABLE",
    reason,
    durationSeconds: options.durationSeconds,
    warmupSeconds: options.warmupSeconds,
    connections: 0,
    pipeline: 1,
    requestsPerSecond: 0,
    latency: { avgMs: null, p50Ms: null, p75Ms: null, p90Ms: null, p95Ms: null, p99Ms: null, maxMs: null },
    transfer: { bytesPerSecond: 0, totalRequests: 0, errors: 0, timeouts: 0, non2xx: 0 },
    process: { peakWorkingSetBytes: null, avgWorkingSetBytes: null, cpuUserMs: null, cpuKernelMs: null },
    artifacts: { stdout: null, stderr: null, rawLoadOutput: null },
  };
}

function workloadDefinitions(workloads) {
  return workloads.map((workload) => ({
    name: workload.name,
    description: workload.description,
  }));
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const tools = detectTools(options);
  const workloads = expandWorkloads(options);
  const result = {
    schemaVersion: 1,
    suite: options.suite,
    startedAt: new Date().toISOString(),
    host: hostInfo(),
    tools,
    workloadDefinitions: workloadDefinitions(workloads),
    options: {
      durationSeconds: options.durationSeconds,
      warmupSeconds: options.warmupSeconds,
      connections: options.connections,
      iterations: options.iterations,
      runtimes: options.runtimes,
      categories: options.categories,
      quick: options.quick,
      full: options.full,
    },
    runs: [],
  };

  if (options.dryRun) {
    console.log(JSON.stringify({
      schemaVersion: result.schemaVersion,
      suite: result.suite,
      tools,
      workloads: workloads.map((workload) => workload.name),
      options: result.options,
    }, null, 2));
    return;
  }

  await fs.mkdir(options.out, { recursive: true });
  const buildCache = new Map();
  const suites = options.suite === "all" ? ["http", "startup"] : [options.suite];
  for (const runtimeName of options.runtimes) {
    const runtime = tools[runtimeName];
    if (!runtime || runtime.status !== "AVAILABLE") {
      result.runs.push(unavailableRun(runtimeName, `${runtimeName} executable not found`, options));
      continue;
    }
    for (const category of options.categories) {
      for (const suite of suites) {
        if (suite === "startup") {
          const workload = BASIC_WORKLOADS[0];
          for (let iteration = 1; iteration <= options.iterations; iteration += 1) {
            result.runs.push(await runStartupMeasurement({
              runtimeName,
              runtime,
              category,
              workload: { ...workload, name: "startup-health" },
              options,
              outDir: options.out,
              buildCache,
              iteration,
            }));
          }
          continue;
        }
        if (suite !== "http" && suite !== "stress") {
          throw new Error(`Unsupported suite: ${suite}`);
        }
        for (const workload of workloads) {
          for (const connections of options.connections) {
            for (let iteration = 1; iteration <= options.iterations; iteration += 1) {
              result.runs.push(await runMeasurement({
                runtimeName,
                runtime,
                category,
                workload,
                options,
                outDir: options.out,
                buildCache,
                iteration,
                connections,
              }));
            }
          }
        }
      }
    }
  }
  await writeReports(result, options.out);
  const requiredUnavailable = options.requireRuntimes
    .filter((runtimeName) => !tools[runtimeName] || tools[runtimeName].status !== "AVAILABLE");
  if (requiredUnavailable.length > 0) {
    console.error(`Required runtime unavailable: ${requiredUnavailable.join(", ")}`);
    process.exitCode = 2;
    return;
  }
  const failures = result.runs.filter((run) => run.status === "FAIL");
  if (failures.length > 0) {
    console.error(`${failures.length} benchmark row(s) failed. See ${path.join(options.out, "summary.md")}`);
  }
}

main().catch((error) => {
  console.error(error.stack ?? error.message);
  process.exitCode = 1;
});
