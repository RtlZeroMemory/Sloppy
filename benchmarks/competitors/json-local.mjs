import http from "node:http";
import os from "node:os";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { performance } from "node:perf_hooks";
import { spawnSync } from "node:child_process";
import { spawn } from "node:child_process";
import net from "node:net";

const require = createRequire(import.meta.url);

const args = new Map();
for (let i = 2; i < process.argv.length; i += 1) {
  if (process.argv[i].startsWith("--")) {
    args.set(process.argv[i].slice(2), process.argv[i + 1]);
    i += 1;
  }
}

const iterations = Number(args.get("iterations") ?? "100");
const warmupIterations = Number(args.get("warmup") ?? "10");
const repeat = Number(args.get("repeat") ?? "1");
const outPath = args.get("out") ?? "artifacts/bench/json-competitors.json";
const sloppyBinArg = args.get("sloppy-bin") ?? "";
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..", "..");

const payloads = {
  small: { username: "ada", password: "correct horse battery staple" },
  invalid: "{",
  medium: {
    name: "Ada Lovelace",
    email: "ada@example.test",
    roles: ["admin", "author"],
    profile: { active: true, age: 42, tags: ["math", "engine"] },
  },
};

function checksumText(text) {
  let checksum = 1469598103934665603n;
  for (let i = 0; i < text.length; i += 1) {
    checksum ^= BigInt(text.charCodeAt(i));
    checksum *= 1099511628211n;
    checksum &= (1n << 64n) - 1n;
  }
  return checksum.toString();
}

function detectCommand(command, args = ["--version"]) {
  const result = spawnSync(command, args, { encoding: "utf8" });
  if (result.error || result.status !== 0) {
    return { available: false, version: null };
  }
  return { available: true, version: (result.stdout || result.stderr).trim() };
}

function optionalPackage(name) {
  try {
    let version = "unknown";
    try {
      version = require(`${name}/package.json`).version ?? "unknown";
    } catch {
      version = "unknown";
    }
    return { available: true, path: require.resolve(name), version };
  } catch {
    return { available: false, path: null, version: null };
  }
}

function resolveSloppyBin() {
  const candidates = [
    sloppyBinArg,
    path.join(repoRoot, "build", "windows-relwithdebinfo", "sloppy.exe"),
    path.join(repoRoot, "build", "windows-release", "sloppy.exe"),
    path.join(repoRoot, "build", "windows-dev", "sloppy.exe"),
    path.join(repoRoot, "build", "unix-relwithdebinfo", "sloppy"),
    path.join(repoRoot, "build", "unix-release", "sloppy"),
  ].filter(Boolean);
  for (const candidate of candidates) {
    const result = spawnSync(candidate, ["--version"], { encoding: "utf8" });
    if (!result.error && result.status === 0) {
      return { available: true, path: candidate, version: (result.stdout || result.stderr).trim() };
    }
  }
  return { available: false, path: null, version: null };
}

async function getFreePort() {
  return await new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      server.close(() => resolve(address.port));
    });
  });
}

function largeList() {
  return Array.from({ length: 256 }, (_, id) => ({ id, name: `user-${id}`, active: id % 2 === 0 }));
}

function validateLogin(value) {
  return value && typeof value.username === "string" && typeof value.password === "string";
}

function expectedForScenario(scenario, requestIndex, text, status) {
  if (scenario === "invalid") {
    return status === 400
      ? { ok: true }
      : { ok: false, reason: `invalid scenario expected 400, got ${status}` };
  }

  let parsed = null;
  try {
    parsed = text.length === 0 ? null : JSON.parse(text);
  } catch {
    return { ok: false, reason: "response body is not valid JSON" };
  }

  if (status !== 200) {
    return { ok: false, reason: `${scenario} expected 200, got ${status}` };
  }
  if (scenario === "small") {
    return parsed?.ok === true && parsed?.echo?.username === payloads.small.username
      ? { ok: true }
      : { ok: false, reason: "small response did not echo the login payload" };
  }
  if (scenario === "medium") {
    return parsed?.ok === true && parsed?.echo?.profile?.age === payloads.medium.profile.age
      ? { ok: true }
      : { ok: false, reason: "medium response did not echo the medium payload" };
  }
  if (scenario === "large") {
    return Array.isArray(parsed?.items) && parsed.items.length === 256
      ? { ok: true }
      : { ok: false, reason: "large response did not contain the expected 256 items" };
  }
  const expectedRoute = `/route/${requestIndex % 1000}`;
  return parsed?.ok === true && parsed?.route === expectedRoute
    ? { ok: true }
    : { ok: false, reason: `route-table response did not contain ${expectedRoute}` };
}

function handleScenario(req, res) {
  if (req.method === "GET" && req.url === "/large") {
    const body = JSON.stringify({ items: largeList() });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url?.startsWith("/route/")) {
    const body = JSON.stringify({ ok: true, route: req.url });
    res.writeHead(200, { "content-type": "application/json" });
    res.end(body);
    return;
  }
  if (req.method !== "POST") {
    res.writeHead(404).end();
    return;
  }

  let raw = "";
  req.setEncoding("utf8");
  req.on("data", (chunk) => {
    raw += chunk;
  });
  req.on("end", () => {
    let parsed;
    try {
      parsed = JSON.parse(raw);
    } catch {
      res.writeHead(400, { "content-type": "application/json" });
      res.end('{"error":"malformed_json"}');
      return;
    }

    if (req.url === "/small" && !validateLogin(parsed)) {
      res.writeHead(400, { "content-type": "application/json" });
      res.end('{"error":"invalid_body"}');
      return;
    }
    const body = JSON.stringify({ ok: true, echo: parsed });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
  });
}

async function startNodeHttpServer() {
  const server = http.createServer(handleScenario);
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const address = server.address();
  return { baseUrl: `http://127.0.0.1:${address.port}`, close: () => new Promise((resolve) => server.close(resolve)) };
}

async function startExpressServer() {
  const express = (await import("express")).default;
  const app = express();
  app.use(express.json());
  app.post("/small", (req, res) => {
    if (!validateLogin(req.body)) {
      res.status(400).json({ error: "invalid_body" });
      return;
    }
    res.json({ ok: true, echo: req.body });
  });
  app.post("/medium", (req, res) => res.json({ ok: true, echo: req.body }));
  app.post("/invalid", (_req, res) => res.status(400).json({ error: "malformed_json" }));
  app.get("/large", (_req, res) => res.json({ items: largeList() }));
  app.get("/route/:id", (req, res) => res.json({ ok: true, route: `/route/${req.params.id}` }));
  app.use((error, _req, res, _next) => {
    if (error instanceof SyntaxError) {
      res.status(400).json({ error: "malformed_json" });
      return;
    }
    res.status(500).json({ error: "internal_error" });
  });
  const server = await new Promise((resolve) => {
    const instance = app.listen(0, "127.0.0.1", () => resolve(instance));
  });
  const address = server.address();
  return { baseUrl: `http://127.0.0.1:${address.port}`, close: () => new Promise((resolve) => server.close(resolve)) };
}

async function startFastifyServer() {
  const fastifyFactory = (await import("fastify")).default;
  const app = fastifyFactory({ logger: false });
  app.post("/small", async (req, reply) => {
    if (!validateLogin(req.body)) {
      return reply.code(400).send({ error: "invalid_body" });
    }
    return { ok: true, echo: req.body };
  });
  app.post("/medium", async (req) => ({ ok: true, echo: req.body }));
  app.get("/large", async () => ({ items: largeList() }));
  app.get("/route/:id", async (req) => ({ ok: true, route: `/route/${req.params.id}` }));
  await app.listen({ host: "127.0.0.1", port: 0 });
  return { baseUrl: app.server.address() ? `http://127.0.0.1:${app.server.address().port}` : "", close: () => app.close() };
}

async function waitForHttpReady(baseUrl) {
  const deadline = Date.now() + 10_000;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`${baseUrl}/large`);
      if (response.status === 200) {
        await response.arrayBuffer();
        return;
      }
      lastError = new Error(`ready probe returned ${response.status}`);
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw lastError ?? new Error("server did not become ready");
}

async function startSloppyServer(mode, sloppyInfo) {
  const port = await getFreePort();
  const appRoot = path.join(scriptDir, "sloppy-json");
  const artifacts = path.join(repoRoot, "artifacts", "bench", `sloppy-json-${mode}`);
  const source = path.join(appRoot, "src", "main.ts");
  const build = spawnSync(
    sloppyInfo.path,
    ["build", source, "--out", artifacts, "--kind", "web", "--host", "127.0.0.1", "--port", String(port)],
    { cwd: appRoot, encoding: "utf8" },
  );
  if (build.error || build.status !== 0) {
    throw new Error(`sloppy build failed: ${build.error?.message ?? ""}\n${build.stdout}\n${build.stderr}`);
  }

  const child = spawn(sloppyInfo.path, ["run", artifacts, "--host", "127.0.0.1", "--port", String(port)], {
    cwd: appRoot,
    env: { ...process.env, SLOPPY_JSON_DISPATCH: mode },
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  let stderr = "";
  child.stderr.on("data", (chunk) => {
    stderr += chunk.toString();
  });
  child.once("error", () => {});
  const baseUrl = `http://127.0.0.1:${port}`;
  try {
    await waitForHttpReady(baseUrl);
  } catch (error) {
    child.kill();
    throw new Error(`sloppy server did not become ready: ${error.message}\n${stderr}`);
  }
  return {
    baseUrl,
    close: () =>
      new Promise((resolve) => {
        child.once("exit", resolve);
        child.kill();
        setTimeout(resolve, 1000).unref();
      }),
  };
}

async function issueScenarioRequest(baseUrl, scenario, index) {
  if (scenario === "small") {
    return await fetch(`${baseUrl}/small`, {
      method: "POST",
      body: JSON.stringify(payloads.small),
      headers: { "content-type": "application/json" },
    });
  }
  if (scenario === "invalid") {
    return await fetch(`${baseUrl}/small`, {
      method: "POST",
      body: payloads.invalid,
      headers: { "content-type": "application/json" },
    });
  }
  if (scenario === "medium") {
    return await fetch(`${baseUrl}/medium`, {
      method: "POST",
      body: JSON.stringify(payloads.medium),
      headers: { "content-type": "application/json" },
    });
  }
  if (scenario === "large") {
    return await fetch(`${baseUrl}/large`);
  }
  return await fetch(`${baseUrl}/route/${index % 1000}`);
}

async function runScenario(baseUrl, scenario) {
  let bytes = 0;
  let checksum = 0n;
  for (let i = 0; i < warmupIterations; i += 1) {
    const response = await issueScenarioRequest(baseUrl, scenario, i);
    const text = await response.text();
    const correctness = expectedForScenario(scenario, i, text, response.status);
    if (!correctness.ok) {
      throw new Error(`${scenario} warmup failed: ${correctness.reason}`);
    }
  }
  const start = performance.now();
  for (let i = 0; i < iterations; i += 1) {
    const response = await issueScenarioRequest(baseUrl, scenario, i);
    const text = await response.text();
    const correctness = expectedForScenario(scenario, i, text, response.status);
    if (!correctness.ok) {
      throw new Error(`${scenario} failed: ${correctness.reason}`);
    }
    bytes += Buffer.byteLength(text);
    checksum += BigInt(checksumText(text));
  }
  const elapsedNs = Math.round((performance.now() - start) * 1_000_000);
  const seconds = elapsedNs / 1_000_000_000;
  return {
    scenario,
    status: "PASS",
    iterations,
    warmupIterations,
    elapsedNs,
    nsPerOp: elapsedNs / iterations,
    bytesPerSecond: seconds > 0 ? bytes / seconds : 0,
    checksum: checksum.toString(),
  };
}

function scenarioRowSegment(scenario) {
  return scenario.replaceAll("-", "_");
}

function runtimeRowPrefix(runtime) {
  if (runtime === "sloppy:loopback:native_json") {
    return "sloppy.loopback.native_json";
  }
  if (runtime === "sloppy:loopback:generic_json") {
    return "sloppy.loopback.generic_json";
  }
  return `${runtime.replaceAll(":", "_")}.loopback`;
}

function median(values) {
  if (values.length === 0) {
    return null;
  }
  const sorted = values.toSorted((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 1 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function summarizeRows(results) {
  const groups = new Map();
  for (const result of results) {
    for (const row of result.rows ?? []) {
      const key = `${result.runtime}\u0000${row.name}`;
      if (!groups.has(key)) {
        groups.set(key, { runtime: result.runtime, version: result.version, name: row.name, scenario: row.scenario, rows: [] });
      }
      groups.get(key).rows.push(row);
    }
  }
  return Array.from(groups.values()).map((group) => {
    const passed = group.rows.filter((row) => row.status === "PASS");
    const nsValues = passed.map((row) => row.nsPerOp);
    return {
      runtime: group.runtime,
      version: group.version,
      name: group.name,
      scenario: group.scenario,
      repeats: group.rows.length,
      passRepeats: passed.length,
      status: group.rows.every((row) => row.status === "PASS") ? "PASS" : "FAIL",
      medianNsPerOp: median(nsValues),
      minNsPerOp: nsValues.length > 0 ? Math.min(...nsValues) : null,
      maxNsPerOp: nsValues.length > 0 ? Math.max(...nsValues) : null,
    };
  });
}

async function runRuntime(name, version, start) {
  let server = null;
  try {
    server = await start();
    const scenarios = ["small", "invalid", "medium", "large", "route-table"];
    const rows = [];
    for (let repeatIndex = 1; repeatIndex <= repeat; repeatIndex += 1) {
      for (const scenario of scenarios) {
        try {
          const row = await runScenario(server.baseUrl, scenario);
          row.name = `${runtimeRowPrefix(name)}.${scenarioRowSegment(scenario)}`;
          row.repeat = repeatIndex;
          rows.push(row);
        } catch (error) {
          rows.push({
            name: `${runtimeRowPrefix(name)}.${scenarioRowSegment(scenario)}`,
            scenario,
            status: "FAIL",
            reason: error.message,
            iterations,
            warmupIterations,
            repeat: repeatIndex,
          });
        }
      }
    }
    const status = rows.every((row) => row.status === "PASS") ? "PASS" : "FAIL";
    return { runtime: name, version, status, rows };
  } catch (error) {
    return { runtime: name, version, status: "SKIPPED", reason: error.message, rows: [] };
  } finally {
    if (server != null) {
      await server.close();
    }
  }
}

async function startExternalServer(command, commandArgs) {
  const child = spawn(command, commandArgs, {
    cwd: scriptDir,
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  let stderr = "";
  const port = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      reject(new Error(`${command} server did not report a port. ${stderr}`));
    }, 5000);
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.once("error", (error) => {
      clearTimeout(timeout);
      reject(error);
    });
    child.stdout.on("data", (chunk) => {
      for (const line of chunk.toString().split(/\r?\n/)) {
        if (!line.trim().startsWith("{")) {
          continue;
        }
        try {
          const message = JSON.parse(line);
          if (message.port) {
            clearTimeout(timeout);
            resolve(message.port);
          }
        } catch {
          // Ignore non-protocol stdout from the runtime.
        }
      }
    });
  });

  return {
    baseUrl: `http://127.0.0.1:${port}`,
    close: () =>
      new Promise((resolve) => {
        child.once("exit", resolve);
        child.kill();
        setTimeout(resolve, 1000).unref();
      }),
  };
}

const results = [];
const sloppyInfo = resolveSloppyBin();
if (sloppyInfo.available) {
  results.push(await runRuntime("sloppy:loopback:native_json", sloppyInfo.version, () => startSloppyServer("native", sloppyInfo)));
  results.push(await runRuntime("sloppy:loopback:generic_json", sloppyInfo.version, () => startSloppyServer("generic", sloppyInfo)));
} else {
  results.push({ runtime: "sloppy:loopback:native_json", status: "SKIPPED", reason: "sloppy executable not found", rows: [] });
  results.push({ runtime: "sloppy:loopback:generic_json", status: "SKIPPED", reason: "sloppy executable not found", rows: [] });
}
const nodeVersion = detectCommand("node");
if (nodeVersion.available) {
  results.push(await runRuntime("node:http", nodeVersion.version, startNodeHttpServer));
} else {
  results.push({ runtime: "node:http", status: "SKIPPED", reason: "node executable not found", rows: [] });
}

const expressInfo = optionalPackage("express");
if (nodeVersion.available && expressInfo.available) {
  results.push(await runRuntime("node:express", `${nodeVersion.version}; express ${expressInfo.version}`, startExpressServer));
} else {
  results.push({ runtime: "node:express", status: "SKIPPED", reason: "express dependency not installed", rows: [] });
}

const fastifyInfo = optionalPackage("fastify");
if (nodeVersion.available && fastifyInfo.available) {
  results.push(await runRuntime("node:fastify", `${nodeVersion.version}; fastify ${fastifyInfo.version}`, startFastifyServer));
} else {
  results.push({ runtime: "node:fastify", status: "SKIPPED", reason: "fastify dependency not installed", rows: [] });
}

const bunInfo = detectCommand("bun");
if (bunInfo.available) {
  results.push(await runRuntime("bun", bunInfo.version, () => startExternalServer("bun", ["bun-server.mjs"])));
} else {
  results.push({ runtime: "bun", status: "SKIPPED", reason: "bun executable not found", rows: [] });
}

const denoInfo = detectCommand("deno");
if (denoInfo.available) {
  results.push(
    await runRuntime("deno", denoInfo.version, () =>
      startExternalServer("deno", ["run", "--allow-net=127.0.0.1", "deno-server.mjs"]),
    ),
  );
} else {
  results.push({ runtime: "deno", status: "SKIPPED", reason: "deno executable not found", rows: [] });
}

const report = {
  schemaVersion: 1,
  label: "local dev-machine JSON competitor benchmark",
  localDevMachineMeasurements: true,
  generatedAtUtc: new Date().toISOString(),
  iterations,
  warmupIterations,
  repeat,
  client: "node fetch loop, one awaited request at a time",
  scenarios: {
    small: "POST /small with a small JSON login payload; validates JSON echo response",
    invalid: "POST /small with malformed JSON; expects HTTP 400",
    medium: "POST /medium with nested JSON payload; validates JSON echo response",
    large: "GET /large; validates 256-item JSON response",
    "route-table": "GET /route/{id}; validates route echo; raw runtimes may use a parameter route, so this is loopback routing evidence rather than a generated 1000-route-table comparison",
  },
  host: {
    platform: os.platform(),
    arch: os.arch(),
    release: os.release(),
    cpu: os.cpus()[0]?.model ?? "unknown",
  },
  results,
  summary: summarizeRows(results),
};

await fs.mkdir(path.dirname(outPath), { recursive: true });
await fs.writeFile(outPath, `${JSON.stringify(report, null, 2)}\n`, "utf8");
console.log(JSON.stringify(report, null, 2));
