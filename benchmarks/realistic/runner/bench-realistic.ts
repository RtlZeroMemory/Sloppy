import { Directory, File } from "sloppy/fs";
import { Environment, Process, System } from "sloppy/os";
import { Time } from "sloppy/time";

const host = "127.0.0.1";

const baseWorkloads = [
  { name: "health", description: "GET /health returns plain text ok; dispatch and small write baseline.", request: { method: "GET", path: "/health" }, expectedStatus: 200 },
  { name: "json-small", aliases: ["json"], description: "GET /json returns a small stable JSON object.", request: { method: "GET", path: "/json" }, expectedStatus: 200 },
  { name: "route-param", description: "GET /users/123 returns a JSON object with a route parameter.", request: { method: "GET", path: "/users/123" }, expectedStatus: 200 },
  { name: "query", description: "GET /search parses q, page, and limit query values.", request: { method: "GET", path: "/search?q=ada&page=2&limit=10" }, expectedStatus: 200 },
  { name: "post-json-small", description: "POST /echo parses a small JSON request body and echoes selected fields.", request: { method: "POST", path: "/echo", headers: { "content-type": "application/json" }, body: JSON.stringify({ name: "Ada", count: 42 }) }, expectedStatus: 200 },
  { name: "middleware-request-id", description: "GET /middleware propagates or generates a request ID.", request: { method: "GET", path: "/middleware", headers: { "x-request-id": "bench-request" } }, expectedStatus: 200 },
  { name: "static-ish-payload", description: "GET /payload/64kb returns a stable 64 KiB text payload.", request: { method: "GET", path: "/payload/64kb" }, expectedStatus: 200 },
  {
    name: "mixed-realistic",
    description: "Weighted local mix: 40% health/json, 25% route param, 15% query, 10% post JSON, 10% misses.",
    request: { method: "GET", path: "/health" },
    expectedStatus: 200,
    requests: [
      { method: "GET", path: "/health", weight: 20 },
      { method: "GET", path: "/json", weight: 20 },
      { method: "GET", path: "/users/123", weight: 25 },
      { method: "GET", path: "/search?q=ada&page=2&limit=10", weight: 15 },
      { method: "POST", path: "/echo", headers: { "content-type": "application/json" }, body: JSON.stringify({ name: "Ada", count: 42 }), weight: 10 },
      { method: "GET", path: "/missing", weight: 10 },
    ],
  },
];

function sep() {
  return isWindows() ? "\\" : "/";
}

function isWindows() {
  return String(System.platform).toLowerCase().includes("win");
}

function joinPath(...parts) {
  const s = sep();
  const cleaned = parts.filter((part) => part !== undefined && part !== null && String(part).length > 0).map(String);
  if (cleaned.length === 0) return "";
  let out = cleaned[0].replace(/[\\/]+$/g, "");
  for (let i = 1; i < cleaned.length; i += 1) {
    out += s + cleaned[i].replace(/^[\\/]+/g, "").replace(/[\\/]+$/g, "");
  }
  return out;
}

function splitList(value, fallback = []) {
  if (Array.isArray(value)) {
    const items = value.map(String).filter(Boolean);
    return items.length === 0 ? fallback : items;
  }
  if (typeof value !== "string" || value.length === 0) return fallback;
  return value.split(",").map((item) => item.trim()).filter(Boolean);
}

function largeRouteWorkloads(sizes) {
  const workloads = [];
  for (const size of sizes) {
    for (const [position, route] of [["first", 0], ["middle", Math.floor(size / 2)], ["last", size - 1]]) {
      workloads.push({
        name: `large-route-table-hit-${size}-${position}`,
        family: "large-route-table-hit",
        routeProfile: true,
        routeCount: size,
        description: `GET /routes/${route} against a generated ${size}-route table (${position} hit).`,
        request: { method: "GET", path: `/routes/${route}` },
        expectedStatus: 200,
      });
    }
    workloads.push({
      name: `large-route-table-miss-${size}`,
      family: "large-route-table-miss",
      routeProfile: true,
      routeCount: size,
      description: `GET /routes/missing against a generated ${size}-route table.`,
      request: { method: "GET", path: "/routes/missing" },
      expectedStatus: 404,
    });
  }
  return workloads;
}

function applyDefaults(options) {
  options.suite ??= "http";
  options.runtimes = splitList(options.runtimes, ["sloppy", "node", "bun", "deno"]);
  options.categories = splitList(options.categories, options.quick ? ["framework"] : ["baseline", "framework", "feature-rich"]);
  options.workloads = splitList(options.workloads, options.quick ? ["health", "json-small", "route-param"] : []);
  options.durationSeconds ??= options.full ? 60 : options.quick ? 5 : options.suite === "stress" ? 600 : 30;
  options.warmupSeconds ??= options.full ? 15 : options.quick ? 2 : options.suite === "stress" ? 15 : 10;
  options.iterations ??= options.full ? 7 : options.quick ? 1 : options.suite === "stress" ? 1 : 5;
  options.connections = Array.isArray(options.connections)
    ? options.connections.map(Number)
    : splitList(options.connections, []).map(Number);
  if (options.connections.length === 0) {
    options.connections = options.full ? [1, 16, 64, 256, 512] : options.quick ? [1, 16] : options.suite === "stress" ? [64] : [1, 16, 64, 256];
  }
  options.requireRuntimes = splitList(options.requireRuntimes, []);
  return options;
}

function routeSizes(options) {
  if (options.full) return [100, 1000, 5000];
  if (options.quick) return [100];
  return [100, 1000];
}

function expandWorkloads(options) {
  const all = [...baseWorkloads, ...largeRouteWorkloads(routeSizes(options))];
  const aliases = new Map();
  for (const workload of all) {
    aliases.set(workload.name, [workload]);
    for (const alias of workload.aliases ?? []) aliases.set(alias, [workload]);
  }
  aliases.set("large-route-table-hit", all.filter((item) => item.family === "large-route-table-hit"));
  aliases.set("large-route-table-miss", all.filter((item) => item.family === "large-route-table-miss"));
  aliases.set("large-routes", all.filter((item) => item.routeProfile));
  if (options.suite === "stress") return all.filter((item) => item.name === "mixed-realistic");
  if (options.workloads.length === 0) return all;
  const selected = [];
  for (const name of options.workloads) {
    const found = aliases.get(name);
    if (!found) throw new Error(`Unknown workload: ${name}`);
    selected.push(...found);
  }
  return Array.from(new Map(selected.map((item) => [item.name, item])).values());
}

async function runText(command, args = [], options = {}) {
  try {
    const result = await Process.run(command, args, { cwd: options.cwd, capture: "text", timeoutMs: options.timeoutMs ?? 10000, maxStdoutBytes: options.maxStdoutBytes ?? 65536, maxStderrBytes: options.maxStderrBytes ?? 65536 });
    return result.exitCode === 0 ? String(result.stdout ?? "").trim() || String(result.stderr ?? "").trim() : null;
  } catch (_) {
    return null;
  }
}

async function findOnPath(name) {
  const command = isWindows() ? "where.exe" : "which";
  const out = await runText(command, [name], { timeoutMs: 5000 });
  return out ? out.split(/\r?\n/).map((line) => line.trim()).find(Boolean) ?? null : null;
}

async function detectTools(options) {
  const tools = {};
  for (const name of ["sloppy", "node", "bun", "deno"]) {
    let path = name === "sloppy" ? options.sloppyExe : options.runtimePaths?.[name];
    if (!path) path = await findOnPath(name);
    tools[name] = path
      ? { status: "AVAILABLE", version: await runText(path, ["--version"], { cwd: options.repoRoot }) ?? "unknown", path }
      : { status: "UNAVAILABLE", version: null, path: null };
  }
  const sloppyc = options.sloppycExe || await findOnPath("sloppyc");
  tools.sloppyc = sloppyc
    ? { status: "AVAILABLE", version: await runText(sloppyc, ["--version"], { cwd: options.repoRoot }) ?? "unknown", path: sloppyc }
    : { status: "UNAVAILABLE", version: null, path: null };
  tools.loadGenerator = { name: "sloppy-program-httpclient", version: tools.sloppy.version, path: tools.sloppy.path };
  return tools;
}

async function hostInfo(repoRoot) {
  const cpu = isWindows()
    ? await runText("powershell.exe", ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", "(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)"], { timeoutMs: 5000 })
    : await runText("sh", ["-c", "uname -mpr"], { timeoutMs: 5000 });
  const memoryText = isWindows()
    ? await runText("powershell.exe", ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", "(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory"], { timeoutMs: 5000 })
    : await runText("sh", ["-c", "awk '/MemTotal/ {print $2*1024}' /proc/meminfo 2>/dev/null || echo 0"], { timeoutMs: 5000 });
  return {
    os: System.platform,
    release: await runText(isWindows() ? "cmd.exe" : "uname", isWindows() ? ["/c", "ver"] : ["-r"], { timeoutMs: 5000 }) ?? "",
    arch: System.arch,
    cpu: cpu ?? "unknown",
    logicalCores: System.cpuCount,
    memoryBytes: Number(memoryText ?? 0) || null,
    gitCommit: await runText("git", ["rev-parse", "HEAD"], { cwd: repoRoot }),
    gitBranch: await runText("git", ["branch", "--show-current"], { cwd: repoRoot }),
  };
}

async function freePort() {
  if (isWindows()) {
    const script = "$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Parse('127.0.0.1'), 0); $listener.Start(); $port = $listener.LocalEndpoint.Port; $listener.Stop(); $port";
    const result = await Process.run("powershell.exe", ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script], { capture: "text", timeoutMs: 5000, maxStdoutBytes: 1024, maxStderrBytes: 1024 }).catch(() => null);
    const port = Number(String(result?.stdout ?? "").trim());
    if (Number.isInteger(port) && port > 0 && port < 65536) {
      return port;
    }
  }
  return 41000 + Math.floor(Time.systemClock().monotonicNowMs() % 20000);
}

function generateSloppySource(category, routeCount = 0) {
  const lines = [
    category === "baseline" ? 'import { Sloppy, Results, Body, Query, RequestContext, Route } from "sloppy";' : 'import { Sloppy, Results, Body, Query, RequestContext, RequestId, Route } from "sloppy";',
    "",
    "type EchoBody = { name: string; count: number };",
  ];
  if (category === "feature-rich") {
    lines.push("function auditMiddleware(ctx, next) { return next(); }");
    lines.push("function quietRequestLogging(ctx, next) { return next(); }");
    lines.push("const builder = Sloppy.createBuilder();");
    lines.push('builder.services.addSingleton("BenchClock", () => ({ now: "2026-05-10T00:00:00Z" }));');
    lines.push("const app = builder.build();");
  } else {
    lines.push("const app = Sloppy.create();");
  }
  if (category !== "baseline") lines.push('app.use(RequestId.defaults({ header: "x-request-id", responseHeader: true, trustIncoming: true }));');
  if (category === "feature-rich") {
    lines.push("app.use(quietRequestLogging);");
    lines.push("app.use(auditMiddleware);");
    lines.push('app.useCors({ origins: ["https://app.example.com"], headers: ["content-type", "x-request-id"], exposedHeaders: ["x-request-id"], credentials: true, maxAgeSeconds: 600 });');
  }
  lines.push('app.get("/health", () => Results.text("ok"));');
  lines.push('app.get("/json", () => Results.json({ message: "hello", ok: true, count: 42 }));');
  lines.push('app.get("/users/{id:int}", (id: Route<number>) => Results.json({ id, name: "Ada Lovelace" }));');
  lines.push('app.get("/search", (q: Query<string>, page: Query<number>, limit: Query<number>) => Results.json({ q, page, limit, results: [] }));');
  lines.push('app.post("/echo", (body: Body<EchoBody>) => Results.json({ name: body.name, count: body.count }));');
  lines.push(category === "baseline"
    ? 'app.get("/middleware", (ctx: RequestContext) => Results.json({ requestId: "generated" }));'
    : 'app.get("/middleware", (ctx: RequestContext) => Results.json({ requestId: ctx.requestId }));');
  lines.push(`app.get("/payload/64kb", () => Results.text("${"x".repeat(64 * 1024)}"));`);
  for (let i = 0; i < routeCount; i += 1) lines.push(`app.get("/routes/${i}", () => Results.json({ route: ${i} }));`);
  lines.push("export default app;");
  return lines.join("\n");
}

async function buildSloppy(runtime, category, workload, options, cache) {
  const profile = workload.routeProfile ? `routes-${workload.routeCount}` : "main";
  const key = `${category}:${profile}`;
  if (cache.has(key)) return cache.get(key);
  const workDir = joinPath(options.out, "work", "sloppy", category, profile);
  const artifactDir = joinPath(workDir, ".sloppy");
  await Directory.create(workDir, { recursive: true });
  const sourcePath = joinPath(workDir, "app.ts");
  await File.writeText(sourcePath, generateSloppySource(category, workload.routeProfile ? workload.routeCount : 0));
  await File.writeJson(joinPath(workDir, "appsettings.json"), { Sloppy: { Server: { MaxConnections: Math.max(128, ...options.connections), MaxRequestsPerConnection: 0, KeepAliveIdleTimeoutMs: 60000, RequestTimeoutMs: 60000 } } }, { indent: 2 });
  const started = Time.systemClock().monotonicNowMs();
  const build = await Process.run(runtime.path, ["build", sourcePath, "--out", artifactDir, "--host", host, "--port", "5173", "--kind", "web"], { cwd: options.repoRoot, capture: "text", timeoutMs: 120000, maxStdoutBytes: 262144, maxStderrBytes: 262144 });
  const prepared = { ok: build.exitCode === 0, artifactDir, sourcePath, buildDurationMs: Time.systemClock().monotonicNowMs() - started, stdout: String(build.stdout ?? ""), stderr: String(build.stderr ?? ""), routeCount: workload.routeProfile ? workload.routeCount : 0 };
  cache.set(key, prepared);
  return prepared;
}

async function prepareApp(runtimeName, runtime, category, workload, options, cache) {
  if (runtimeName === "sloppy") return await buildSloppy(runtime, category, workload, options, cache);
  return { ok: true, serverPath: joinPath(options.repoRoot, "benchmarks", "realistic", "apps", runtimeName, category, "server.mjs"), routeCount: workload.routeProfile ? workload.routeCount : 0, buildDurationMs: null, stdout: "", stderr: "" };
}

async function startServer(runtimeName, runtime, prepared, port, options) {
  const env = { HOST: host, PORT: String(port), ROUTE_COUNT: String(prepared.routeCount ?? 0) };
  const runId = safeName(`${runtimeName}-${port}-${Time.systemClock().monotonicNowMs()}`);
  const logDir = joinPath(options.out, "raw");
  await Directory.create(logDir, { recursive: true });
  const stdout = joinPath(logDir, `${runId}.server.stdout.log`);
  const stderr = joinPath(logDir, `${runId}.server.stderr.log`);
  let commandLine;
  if (runtimeName === "sloppy") {
    commandLine = `"${runtime.path}" run --artifacts "${prepared.artifactDir}" --host ${host} --port ${port}`;
  } else if (runtimeName === "deno") {
    commandLine = `"${runtime.path}" run --allow-net --allow-env "${prepared.serverPath}"`;
  } else {
    commandLine = `"${runtime.path}" "${prepared.serverPath}"`;
  }
  if (isWindows()) {
    const envPrefix = `set HOST=${env.HOST}&& set PORT=${env.PORT}&& set ROUTE_COUNT=${env.ROUTE_COUNT}&& `;
    const cmdLine = `${envPrefix}${commandLine} > "${stdout}" 2> "${stderr}"`;
    const ps = `$p = Start-Process -FilePath "cmd.exe" -ArgumentList @("/d","/s","/c",'${cmdLine.replace(/'/g, "''")}') -WorkingDirectory '${String(options.repoRoot).replace(/'/g, "''")}' -WindowStyle Hidden -PassThru; $p.Id`;
    const started = await Process.run("powershell.exe", ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", ps], { cwd: options.repoRoot, capture: "text", timeoutMs: 10000, maxStdoutBytes: 4096, maxStderrBytes: 4096 });
    if (started.exitCode !== 0) throw new Error(`failed to start server: ${started.stderr}`);
    return { pid: Number(String(started.stdout).trim()), stdout, stderr };
  }
  const sh = `HOST='${env.HOST}' PORT='${env.PORT}' ROUTE_COUNT='${env.ROUTE_COUNT}' ${commandLine} > '${stdout}' 2> '${stderr}' & echo $!`;
  const started = await Process.run("sh", ["-c", sh], { cwd: options.repoRoot, capture: "text", timeoutMs: 10000, maxStdoutBytes: 4096, maxStderrBytes: 4096 });
  if (started.exitCode !== 0) throw new Error(`failed to start server: ${started.stderr}`);
  return { pid: Number(String(started.stdout).trim()), stdout, stderr };
}

async function stopServer(proc) {
  if (!proc || !Number.isFinite(proc.pid)) return;
  if (isWindows()) {
    const script = `
$ids = New-Object System.Collections.Generic.List[int]
function Add-ProcessTree([int]$Id) {
  if ($ids.Contains($Id)) { return }
  $ids.Add($Id) | Out-Null
  Get-CimInstance Win32_Process -Filter "ParentProcessId=$Id" | ForEach-Object {
    Add-ProcessTree ([int]$_.ProcessId)
  }
}
Add-ProcessTree ${proc.pid}
foreach ($id in [System.Linq.Enumerable]::Reverse($ids)) {
  Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
}
`;
    await Process.run("powershell.exe", ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", script], { capture: "none", timeoutMs: 5000 }).catch(() => {});
    return;
  }
  await Process.run("sh", ["-c", `kill ${proc.pid} 2>/dev/null || true`], { capture: "none", timeoutMs: 5000 }).catch(() => {});
}

async function readLogs(proc) {
  let stdout = "";
  let stderr = "";
  try { stdout = await File.readText(proc.stdout, { timeoutMs: 1000 }); } catch (_) {}
  try { stderr = await File.readText(proc.stderr, { timeoutMs: 1000 }); } catch (_) {}
  return { stdout, stderr };
}

async function singleRequest(client, request, options = {}) {
  const script = joinPath(Environment.get("SLOPPY_BENCH_REPO") ?? "", "benchmarks", "realistic", "runner", "load-generator.ps1");
  const response = await Process.run("pwsh.exe", [
    "-NoLogo", "-NoProfile", "-NonInteractive", "-File", script,
    "-BaseUrl", client,
    "-RequestsJson", JSON.stringify([request]),
    "-Connections", "1",
    "-DurationSeconds", "1",
    "-WarmupSeconds", "0",
    "-Seed", "1",
    "-Single",
    ...(options.captureBody === false ? [] : ["-CaptureBody"]),
  ], { capture: "text", timeoutMs: options.timeoutMs ?? 8000, maxStdoutBytes: 4 * 1024 * 1024, maxStderrBytes: 65536 });
  if (response.exitCode !== 0) {
    return { ok: false, statusCode: 0, bytes: 0, body: "", durationMs: 0, timeout: false, error: String(response.stderr ?? "") };
  }
  return JSON.parse(String(response.stdout ?? "{}"));
}

async function waitForHealth(baseUrl) {
  const deadline = Time.systemClock().monotonicNowMs() + 15000;
  let last = null;
  while (Time.systemClock().monotonicNowMs() < deadline) {
    last = await singleRequest(baseUrl, { method: "GET", path: "/health" }, { timeoutMs: 8000 });
    if (last.ok && last.statusCode === 200 && last.body === "ok") return { ok: true };
    await Time.delay(100);
  }
  return { ok: false, last };
}

function validateBody(workload, response) {
  if (response.statusCode !== workload.expectedStatus) return `expected status ${workload.expectedStatus}, got ${response.statusCode}`;
  if (workload.name === "health") return response.body === "ok" ? null : "expected ok";
  if (workload.name === "static-ish-payload") return response.body.length === 64 * 1024 ? null : "expected 64 KiB body";
  if (workload.name.includes("large-route-table-miss")) return response.statusCode === 404 ? null : "expected 404";
  if (workload.name.includes("large-route-table-hit")) return JSON.parse(response.body).route === Number(workload.request.path.split("/").pop()) ? null : "wrong route";
  if (workload.name === "mixed-realistic") return null;
  const body = JSON.parse(response.body);
  if (workload.name === "json-small") return body.message === "hello" && body.ok === true && body.count === 42 ? null : "wrong json";
  if (workload.name === "route-param") return body.id === 123 && body.name === "Ada Lovelace" ? null : "wrong route param";
  if (workload.name === "query") return body.q === "ada" && body.page === 2 && body.limit === 10 && Array.isArray(body.results) ? null : "wrong query";
  if (workload.name === "post-json-small") return body.name === "Ada" && body.count === 42 ? null : "wrong echo";
  if (workload.name === "middleware-request-id") return typeof body.requestId === "string" && body.requestId.length > 0 ? null : "wrong request id";
  return null;
}

function random(seed) {
  let state = seed >>> 0;
  return () => {
    state = (1664525 * state + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

function hashSeed(value) {
  let hash = 2166136261;
  for (let i = 0; i < value.length; i += 1) {
    hash ^= value.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function pickWeighted(requests, seed) {
  const weighted = requests.map((request) => ({ ...request, weight: request.weight ?? 1 }));
  const total = weighted.reduce((sum, request) => sum + request.weight, 0);
  const next = random(seed);
  return () => {
    const value = next() * total;
    let cursor = 0;
    for (const request of weighted) {
      cursor += request.weight;
      if (value <= cursor) return request;
    }
    return weighted[weighted.length - 1];
  };
}

function percentile(sorted, pct) {
  if (sorted.length === 0) return null;
  return sorted[Math.min(sorted.length - 1, Math.max(0, Math.ceil((pct / 100) * sorted.length) - 1))];
}

function summarize(samples, durationSeconds) {
  const latencies = samples.latencies.slice().sort((a, b) => a - b);
  const totalLatency = samples.latencies.reduce((sum, value) => sum + value, 0);
  return {
    requestsPerSecond: durationSeconds > 0 ? samples.totalRequests / durationSeconds : 0,
    latency: {
      avgMs: samples.totalRequests > 0 ? totalLatency / samples.totalRequests : null,
      p50Ms: percentile(latencies, 50),
      p75Ms: percentile(latencies, 75),
      p90Ms: percentile(latencies, 90),
      p95Ms: percentile(latencies, 95),
      p99Ms: percentile(latencies, 99),
      maxMs: latencies.length > 0 ? latencies[latencies.length - 1] : null,
    },
    transfer: {
      bytesPerSecond: durationSeconds > 0 ? samples.bytes / durationSeconds : 0,
      totalRequests: samples.totalRequests,
      errors: samples.errors,
      timeouts: samples.timeouts,
      non2xx: samples.non2xx,
    },
    raw: samples,
  };
}

async function runPhase(baseUrl, requests, connections, seconds, collect, seed) {
  const samples = { totalRequests: 0, latencies: [], bytes: 0, errors: 0, timeouts: 0, non2xx: 0, errorTypes: {} };
  const client = HttpClient.create({ baseUrl, pool: { maxConnectionsPerOrigin: connections, idleTimeoutMs: 30000 } });
  const pick = pickWeighted(requests, seed);
  const deadline = Time.systemClock().monotonicNowMs() + seconds * 1000;
  async function worker() {
    while (Time.systemClock().monotonicNowMs() < deadline) {
      const result = await singleRequest(client, pick(), { captureBody: false, timeoutMs: 5000 });
      if (!collect) continue;
      if (result.ok) {
        samples.totalRequests += 1;
        samples.latencies.push(result.durationMs);
        samples.bytes += result.bytes;
        if (result.statusCode < 200 || result.statusCode >= 300) samples.non2xx += 1;
      } else {
        samples.errors += 1;
        if (result.timeout) samples.timeouts += 1;
        const key = result.error ?? "unknown";
        samples.errorTypes[key] = (samples.errorTypes[key] ?? 0) + 1;
      }
    }
  }
  await Promise.all(Array.from({ length: connections }, () => worker()));
  return samples;
}

async function runLoad(baseUrl, workload, connections, options, seed) {
  const requests = workload.requests ?? [workload.request];
  const script = joinPath(options.repoRoot, "benchmarks", "realistic", "runner", "load-generator.ps1");
  const result = await Process.run("pwsh.exe", [
    "-NoLogo", "-NoProfile", "-NonInteractive", "-File", script,
    "-BaseUrl", baseUrl,
    "-RequestsJson", JSON.stringify(requests),
    "-Connections", String(connections),
    "-DurationSeconds", String(options.durationSeconds),
    "-WarmupSeconds", String(options.warmupSeconds),
    "-Seed", String(seed),
  ], { capture: "text", timeoutMs: (options.durationSeconds + options.warmupSeconds + 20) * 1000, maxStdoutBytes: 8 * 1024 * 1024, maxStderrBytes: 1024 * 1024 });
  if (result.exitCode !== 0) {
    return { requestsPerSecond: 0, latency: { avgMs: null, p50Ms: null, p75Ms: null, p90Ms: null, p95Ms: null, p99Ms: null, maxMs: null }, transfer: { bytesPerSecond: 0, totalRequests: 0, errors: 1, timeouts: 0, non2xx: 0 }, raw: { stderr: result.stderr } };
  }
  return JSON.parse(String(result.stdout ?? "{}"));
}

function emptyRun(runtimeName, category, workload, options, iteration, connections) {
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
    latency: { avgMs: null, p50Ms: null, p75Ms: null, p90Ms: null, p95Ms: null, p99Ms: null, maxMs: null },
    transfer: { bytesPerSecond: 0, totalRequests: 0, errors: 0, timeouts: 0, non2xx: 0 },
    process: { peakWorkingSetBytes: null, avgWorkingSetBytes: null, cpuUserMs: null, cpuKernelMs: null, note: "UNAVAILABLE: Sloppy ProcessHandle intentionally exposes no child PID." },
    sloppy: {},
    artifacts: { stdout: null, stderr: null, rawLoadOutput: null },
  };
}

function safeName(value) {
  return String(value).replace(/[^A-Za-z0-9_.-]+/g, "-");
}

async function writeRaw(outDir, runId, stdout, stderr, raw) {
  const rawDir = joinPath(outDir, "raw");
  await Directory.create(rawDir, { recursive: true });
  const stdoutPath = joinPath(rawDir, `${runId}.stdout.log`);
  const stderrPath = joinPath(rawDir, `${runId}.stderr.log`);
  const rawPath = joinPath(rawDir, `${runId}.load.json`);
  await File.writeText(stdoutPath, stdout ?? "");
  await File.writeText(stderrPath, stderr ?? "");
  await File.writeJson(rawPath, raw ?? {}, { indent: 2 });
  return { stdout: stdoutPath, stderr: stderrPath, rawLoadOutput: rawPath };
}

async function runMeasurement(runtimeName, runtime, category, workload, options, cache, iteration, connections) {
  const debugPath = joinPath(options.out, "runner-debug.log");
  await File.appendText(debugPath, `start ${runtimeName} ${workload.name} c${connections}\n`).catch(() => {});
  const base = emptyRun(runtimeName, category, workload, options, iteration, connections);
  const prepared = await prepareApp(runtimeName, runtime, category, workload, options, cache);
  await File.appendText(debugPath, `prepared ${runtimeName} ok=${prepared.ok}\n`).catch(() => {});
  if (runtimeName === "sloppy") base.sloppy = { buildDurationMs: prepared.buildDurationMs };
  if (!prepared.ok) {
    base.reason = "server build failed";
    base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-build`), prepared.stdout, prepared.stderr, {});
    return base;
  }
  const port = await freePort();
  const baseUrl = `http://${host}:${port}`;
  const proc = await startServer(runtimeName, runtime, prepared, port, options);
  await File.appendText(debugPath, `started ${runtimeName} pid=${proc.pid} port=${port}\n`).catch(() => {});
  try {
    const ready = await waitForHealth(baseUrl);
    await File.appendText(debugPath, `ready ${runtimeName} ok=${ready.ok}\n`).catch(() => {});
    if (!ready.ok) {
      base.reason = "server did not become ready";
      await stopServer(proc).catch(() => {});
      const logs = await readLogs(proc);
      base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-startup`), logs.stdout, logs.stderr, ready);
      return base;
    }
    const validation = await singleRequest(baseUrl, workload.request, { timeoutMs: 8000 });
    await File.appendText(debugPath, `validated ${runtimeName} ok=${validation.ok} status=${validation.statusCode}\n`).catch(() => {});
    const invalid = validation.ok ? validateBody(workload, validation) : validation.error;
    if (invalid) {
      base.reason = invalid;
      await stopServer(proc).catch(() => {});
      const logs = await readLogs(proc);
      base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}-validation`), logs.stdout, logs.stderr, validation);
      return base;
    }
    const load = await runLoad(baseUrl, workload, connections, options, hashSeed(`${runtimeName}:${category}:${workload.name}:${connections}:${iteration}`));
    await File.appendText(debugPath, `loaded ${runtimeName} total=${load.transfer.totalRequests} errors=${load.transfer.errors}\n`).catch(() => {});
    base.status = load.transfer.errors === 0 && load.transfer.timeouts === 0 ? "PASS" : "FAIL";
    base.reason = base.status === "PASS" ? undefined : "load generator observed request errors";
    base.requestsPerSecond = load.requestsPerSecond;
    base.latency = load.latency;
    base.transfer = load.transfer;
    await stopServer(proc).catch(() => {});
    const logs = await readLogs(proc);
    base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-${workload.name}-${connections}-${iteration}`), logs.stdout, logs.stderr, { load });
    return base;
  } finally {
    await stopServer(proc).catch(() => {});
  }
}

async function runStartupMeasurement(runtimeName, runtime, category, workload, options, cache, iteration) {
  const debugPath = joinPath(options.out, "runner-debug.log");
  await File.appendText(debugPath, `startup ${runtimeName} ${workload.name}\n`).catch(() => {});
  const base = emptyRun(runtimeName, category, { ...workload, name: "startup-health" }, options, iteration, 1);
  base.durationSeconds = 0;
  base.warmupSeconds = 0;
  const prepared = await prepareApp(runtimeName, runtime, category, workload, options, cache);
  if (runtimeName === "sloppy") base.sloppy = { buildDurationMs: prepared.buildDurationMs };
  if (!prepared.ok) {
    base.reason = "server build failed";
    base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-startup-health-${iteration}-startup-build`), prepared.stdout, prepared.stderr, {});
    return base;
  }
  const port = await freePort();
  const baseUrl = `http://${host}:${port}`;
  const started = Time.systemClock().monotonicNowMs();
  const proc = await startServer(runtimeName, runtime, prepared, port, options);
  try {
    const ready = await waitForHealth(baseUrl);
    base.startupMs = Time.systemClock().monotonicNowMs() - started;
    base.status = ready.ok ? "PASS" : "FAIL";
    base.reason = ready.ok ? undefined : "server did not become ready";
    const logs = await readLogs(proc);
    base.artifacts = await writeRaw(options.out, safeName(`${runtimeName}-${category}-startup-health-${iteration}-startup`), logs.stdout, logs.stderr, ready);
    return base;
  } finally {
    await stopServer(proc).catch(() => {});
  }
}

function median(values) {
  const sorted = values.filter((value) => Number.isFinite(value)).sort((a, b) => a - b);
  return sorted.length === 0 ? null : sorted[Math.floor(sorted.length / 2)];
}

function fmtNumber(value, digits = 2) {
  return value === null || value === undefined || Number.isNaN(value) ? "" : Number(value).toFixed(digits);
}

function fmtInt(value) {
  return value === null || value === undefined || Number.isNaN(value) ? "" : String(Math.round(Number(value)));
}

function fmtBytes(value) {
  if (value === null || value === undefined || Number.isNaN(value)) return "";
  const units = ["B", "KB", "MB", "GB"];
  let current = Number(value);
  let unit = 0;
  while (current >= 1024 && unit < units.length - 1) {
    current /= 1024;
    unit += 1;
  }
  return `${current.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function table(headers, rows) {
  return [`| ${headers.join(" | ")} |`, `| ${headers.map(() => "---").join(" | ")} |`, ...rows.map((row) => `| ${row.join(" | ")} |`)].join("\n");
}

function summaries(runs) {
  const groups = new Map();
  for (const run of runs) {
    if (run.status !== "PASS") continue;
    const key = [run.runtime, run.workload, run.variant, run.connections].join("\u0001");
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(run);
  }
  return Array.from(groups.values()).map((items) => {
    const first = items[0];
    return {
      runtime: first.runtime,
      workload: first.workload,
      variant: first.variant,
      connections: first.connections,
      requestsPerSecond: median(items.map((item) => item.requestsPerSecond)),
      p50Ms: median(items.map((item) => item.latency.p50Ms)),
      p95Ms: median(items.map((item) => item.latency.p95Ms)),
      p99Ms: median(items.map((item) => item.latency.p99Ms)),
      startupMs: median(items.map((item) => item.startupMs)),
      errors: items.reduce((sum, item) => sum + item.transfer.errors, 0),
      non2xx: items.reduce((sum, item) => sum + item.transfer.non2xx, 0),
      rssBytes: median(items.map((item) => item.process.peakWorkingSetBytes)),
    };
  }).sort((a, b) => cmp(a.workload, b.workload) || cmp(a.variant, b.variant) || a.connections - b.connections || cmp(a.runtime, b.runtime));
}

function cmp(left, right) {
  left = String(left);
  right = String(right);
  if (left < right) return -1;
  if (left > right) return 1;
  return 0;
}

function deltaRows(summary, competitor) {
  const sloppy = summary.filter((item) => item.runtime === "sloppy");
  const byOther = new Map(summary.filter((item) => item.runtime === competitor).map((item) => [[item.workload, item.variant, item.connections].join("\u0001"), item]));
  const rows = [];
  for (const item of sloppy) {
    const other = byOther.get([item.workload, item.variant, item.connections].join("\u0001"));
    if (!other || !Number.isFinite(other.requestsPerSecond) || other.requestsPerSecond === 0) continue;
    rows.push([item.workload, item.variant, String(item.connections), fmtNumber(item.requestsPerSecond), fmtNumber(other.requestsPerSecond), `${fmtNumber(((item.requestsPerSecond - other.requestsPerSecond) / other.requestsPerSecond) * 100, 1)}%`]);
  }
  return rows;
}

function renderMarkdown(result) {
  const summary = summaries(result.runs);
  const lines = [];
  lines.push("# Realistic Local Benchmark Summary", "", "## Caveat", "");
  lines.push("These benchmarks are local engineering measurements from one machine. They are not official performance claims. They exist to track regressions and identify bottlenecks while Sloppy is pre-alpha.", "");
  lines.push("Do not use these numbers as marketing copy, do not cherry-pick them into public claims, and do not compare debug Sloppy builds to release-like Node, Bun, or Deno runs.", "");
  lines.push("The load generator in this artifact is a Sloppy Program Mode app using `sloppy/net` HttpClient. Process RSS is unavailable until Sloppy exposes child process identity.", "");
  lines.push("## Environment", "", `- Started: ${result.startedAt}`, `- OS: ${result.host.os} ${result.host.release}`, `- Arch: ${result.host.arch}`, `- CPU: ${result.host.cpu}`, `- Logical cores: ${result.host.logicalCores}`, `- Memory: ${fmtBytes(result.host.memoryBytes)}`, `- Git commit: ${result.host.gitCommit ?? ""}`, "");
  lines.push("## Runtime Versions", "", table(["Runtime", "Status", "Version", "Path"], Object.entries(result.tools).filter(([name]) => name !== "loadGenerator").map(([name, tool]) => [name, tool.status ?? "", String(tool.version ?? "").replace(/\r?\n/g, "<br>"), tool.path ?? ""])), "");
  lines.push(`Load generator: ${result.tools.loadGenerator.name} ${result.tools.loadGenerator.version}`, "");
  lines.push("## Workloads", "", table(["Workload", "Definition"], result.workloadDefinitions.map((item) => [item.name, item.description])), "");
  lines.push("## Results", "");
  lines.push(summary.length === 0 ? "No passing measurement rows were produced." : table(["Workload", "Variant", "Runtime", "Conn", "RPS", "Startup ms", "p50 ms", "p95 ms", "p99 ms", "Errors", "Non-2xx", "Peak RSS"], summary.map((item) => [item.workload, item.variant, item.runtime, String(item.connections), fmtNumber(item.requestsPerSecond), fmtNumber(item.startupMs), fmtNumber(item.p50Ms), fmtNumber(item.p95Ms), fmtNumber(item.p99Ms), fmtInt(item.errors), fmtInt(item.non2xx), fmtBytes(item.rssBytes)])));
  lines.push("", "## Relative Deltas", "");
  for (const competitor of ["node", "bun", "deno"]) {
    const rows = deltaRows(summary, competitor);
    lines.push(`### Sloppy relative to ${competitor}`, "", rows.length === 0 ? "No comparable passing local rows." : table(["Workload", "Variant", "Conn", "Sloppy RPS", `${competitor} RPS`, "Delta"], rows), "");
  }
  const failed = result.runs.filter((run) => run.status !== "PASS");
  if (failed.length > 0) lines.push("## Non-PASS Rows", "", table(["Runtime", "Variant", "Workload", "Status", "Reason"], failed.map((run) => [run.runtime, run.variant, run.workload, run.status, run.reason ?? ""])), "");
  return `${lines.join("\n")}\n`;
}

async function writeReports(result, outDir) {
  await Directory.create(outDir, { recursive: true });
  await File.writeJson(joinPath(outDir, "results.json"), result, { indent: 2 });
  await File.writeText(joinPath(outDir, "summary.md"), renderMarkdown(result));
}

function unavailableRun(runtimeName, reason, options) {
  return { runtime: runtimeName, workload: "*", variant: "*", status: "UNAVAILABLE", reason, durationSeconds: options.durationSeconds, warmupSeconds: options.warmupSeconds, connections: 0, pipeline: 1, requestsPerSecond: 0, latency: { avgMs: null, p50Ms: null, p75Ms: null, p90Ms: null, p95Ms: null, p99Ms: null, maxMs: null }, transfer: { bytesPerSecond: 0, totalRequests: 0, errors: 0, timeouts: 0, non2xx: 0 }, process: { peakWorkingSetBytes: null, avgWorkingSetBytes: null, cpuUserMs: null, cpuKernelMs: null }, artifacts: { stdout: null, stderr: null, rawLoadOutput: null } };
}

export async function main() {
  try {
  const configPath = Environment.get("SLOPPY_BENCH_CONFIG");
  if (!configPath) throw new Error("SLOPPY_BENCH_CONFIG must point to a benchmark config JSON file.");
  const options = applyDefaults(await File.readJson(configPath));
  await Directory.create(options.out, { recursive: true });
  const tools = await detectTools(options);
  const workloads = options.suite === "startup" ? [baseWorkloads[0]] : expandWorkloads(options);
  const startupWorkloadDefinitions = options.suite === "startup" || options.suite === "all"
    ? [{ name: "startup-health", description: "Process start to first successful GET /health response." }]
    : [];
  const result = {
    schemaVersion: 1,
    suite: options.suite,
    startedAt: new Date().toISOString(),
    host: await hostInfo(options.repoRoot),
    tools,
    workloadDefinitions: [
      ...workloads.map((workload) => ({ name: workload.name, description: workload.description })),
      ...startupWorkloadDefinitions,
    ],
    options: { durationSeconds: options.durationSeconds, warmupSeconds: options.warmupSeconds, connections: options.connections, iterations: options.iterations, runtimes: options.runtimes, categories: options.categories, quick: options.quick === true, full: options.full === true },
    runs: [],
  };
  if (options.dryRun) {
    await writeReports(result, options.out);
    return JSON.stringify({ status: "PASS", out: options.out, dryRun: true });
  }
  const cache = new Map();
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
          for (let iteration = 1; iteration <= options.iterations; iteration += 1) {
            result.runs.push(await runStartupMeasurement(runtimeName, runtime, category, baseWorkloads[0], options, cache, iteration));
          }
          continue;
        }
        if (suite !== "http" && suite !== "stress") throw new Error(`Unsupported suite in Sloppy quick runner: ${suite}`);
        for (const workload of workloads) {
          for (const connections of options.connections) {
            for (let iteration = 1; iteration <= options.iterations; iteration += 1) {
              result.runs.push(await runMeasurement(runtimeName, runtime, category, workload, options, cache, iteration, connections));
            }
          }
        }
      }
    }
  }
  await writeReports(result, options.out);
  const requiredUnavailable = options.requireRuntimes.filter((name) => !tools[name] || tools[name].status !== "AVAILABLE");
  if (requiredUnavailable.length > 0) throw new Error(`Required runtime unavailable: ${requiredUnavailable.join(", ")}`);
  return JSON.stringify({ status: "PASS", out: options.out, failedRows: result.runs.filter((run) => run.status === "FAIL").length });
  } catch (error) {
    return JSON.stringify({
      status: "FAIL",
      message: error && error.message ? String(error.message) : String(error),
      stack: error && error.stack ? String(error.stack) : null,
    });
  }
}
