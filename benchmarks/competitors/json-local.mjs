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
const httpProfile = ["1", "true", "TRUE", "on", "ON"].includes(args.get("http-profile") ?? "");
const httpProfileOut = args.get("http-profile-out") ?? path.join("artifacts", "bench");
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..", "..");
const httpProfileOutDir = path.resolve(repoRoot, httpProfileOut);
const httpProfileRunId = `${Date.now().toString(36)}-${process.pid.toString(36)}`;

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
    const resolved = path.resolve(repoRoot, candidate);
    const result = spawnSync(resolved, ["--version"], { encoding: "utf8" });
    if (!result.error && result.status === 0) {
      return { available: true, path: resolved, version: (result.stdout || result.stderr).trim() };
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
  if (scenario === "static-status") {
    return status === 204 && text === ""
      ? { ok: true }
      : { ok: false, reason: `static-status expected 204 with empty body, got ${status}` };
  }
  if (scenario === "static-text") {
    return status === 200 && text === "ok\n"
      ? { ok: true }
      : { ok: false, reason: "static-text response did not contain the expected text payload" };
  }
  if (scenario === "dynamic-text") {
    return status === 200 && text === "dynamic-text\n"
      ? { ok: true }
      : { ok: false, reason: "dynamic-text response did not contain the expected text payload" };
  }
  if (scenario === "exception") {
    return status === 500
      ? { ok: true }
      : { ok: false, reason: `exception expected 500, got ${status}` };
  }

  let parsed = null;
  try {
    parsed = text.length === 0 ? null : JSON.parse(text);
  } catch {
    return { ok: false, reason: "response body is not valid JSON" };
  }

  if (scenario === "static-problem") {
    return status === 400 &&
      parsed?.status === 400 &&
      parsed?.code === "SLOPPY_E_STATIC_PROBLEM"
      ? { ok: true }
      : { ok: false, reason: "static-problem response did not contain the expected problem payload" };
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
  if (scenario === "static-json") {
    return parsed?.ok === true && parsed?.mode === "static"
      ? { ok: true }
      : { ok: false, reason: "static-json response did not contain the expected static payload" };
  }
  if (scenario === "dynamic-json") {
    return parsed?.ok === true && parsed?.mode === "dynamic-0"
      ? { ok: true }
      : { ok: false, reason: "dynamic-json response did not contain the expected dynamic payload" };
  }
  if (scenario === "dynamic-async") {
    return parsed?.ok === true && parsed?.mode === "async-dynamic"
      ? { ok: true }
      : { ok: false, reason: "dynamic-async response did not contain the expected async payload" };
  }
  if (scenario === "ctx-query") {
    return parsed?.ok === true && parsed?.query === "abc"
      ? { ok: true }
      : { ok: false, reason: "ctx-query response did not contain the expected query value" };
  }
  if (scenario === "ctx-headers") {
    return parsed?.ok === true && parsed?.trace === "bench-trace"
      ? { ok: true }
      : { ok: false, reason: "ctx-headers response did not contain the expected header value" };
  }
  if (scenario === "ctx-services") {
    return parsed?.ok === true && parsed?.service === "bench-service"
      ? { ok: true }
      : { ok: false, reason: "ctx-services response did not contain the expected service value" };
  }
  if (scenario === "plain-object") {
    return parsed?.ok === true && parsed?.mode === "plain-object"
      ? { ok: true }
      : { ok: false, reason: "plain-object response did not contain the expected JSON payload" };
  }
  const expectedRoute = `/route/${requestIndex % 1000}`;
  return parsed?.ok === true && parsed?.route === expectedRoute
    ? { ok: true }
    : { ok: false, reason: `route-table response did not contain ${expectedRoute}` };
}

function boundedPreview(value, limit = 512) {
  const text = String(value ?? "");
  return text.length <= limit ? text : `${text.slice(0, limit)}...`;
}

async function profileCounterPreview(server) {
  if (!server?.profileOutPath) {
    return "unavailable";
  }
  try {
    const profile = JSON.parse(await fs.readFile(server.profileOutPath, "utf8"));
    const counters = profile.counters ?? {};
    return `nativeResponseHits=${counters.nativeResponseHits ?? "unavailable"}, v8HandlerCalls=${counters.v8HandlerCalls ?? "unavailable"}`;
  } catch {
    return "unavailable";
  }
}

async function validationFailureMessage(scenario, phase, correctness, response, text, server) {
  const contentType = response.headers.get("content-type") ?? "";
  const stderrPreview = typeof server?.stderrPreview === "function" ? server.stderrPreview() : "";
  const counters = await profileCounterPreview(server);
  return [
    `${scenario} ${phase} failed: ${correctness.reason}`,
    `HTTP status: ${response.status}`,
    `content-type: ${contentType || "missing"}`,
    `response body preview: ${boundedPreview(text)}`,
    `Sloppy stderr preview: ${stderrPreview ? boundedPreview(stderrPreview) : "unavailable"}`,
    `profile counters: ${counters}`,
  ].join("\n");
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
  if (req.method === "GET" && req.url === "/static-json") {
    const body = JSON.stringify({ ok: true, mode: "static" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/static-text") {
    res.writeHead(200, { "content-type": "text/plain; charset=utf-8", "content-length": 3 });
    res.end("ok\n");
    return;
  }
  if (req.method === "GET" && req.url === "/static-status") {
    res.writeHead(204).end();
    return;
  }
  if (req.method === "GET" && req.url === "/static-problem") {
    const body = JSON.stringify({ status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" });
    res.writeHead(400, { "content-type": "application/problem+json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/dynamic-json") {
    const body = JSON.stringify({ ok: true, mode: "dynamic-0" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/dynamic-text") {
    res.writeHead(200, { "content-type": "text/plain; charset=utf-8", "content-length": 13 });
    res.end("dynamic-text\n");
    return;
  }
  if (req.method === "GET" && req.url === "/dynamic-async") {
    const body = JSON.stringify({ ok: true, mode: "async-dynamic" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/ctx-query?q=abc") {
    const body = JSON.stringify({ ok: true, query: "abc" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/ctx-headers") {
    const body = JSON.stringify({ ok: true, trace: req.headers["x-trace"] ?? "" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/ctx-services") {
    const body = JSON.stringify({ ok: true, service: "bench-service" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/plain-object") {
    const body = JSON.stringify({ ok: true, mode: "plain-object" });
    res.writeHead(200, { "content-type": "application/json", "content-length": Buffer.byteLength(body) });
    res.end(body);
    return;
  }
  if (req.method === "GET" && req.url === "/exception") {
    const body = JSON.stringify({ status: 500, title: "Internal Server Error" });
    res.writeHead(500, { "content-type": "application/problem+json", "content-length": Buffer.byteLength(body) });
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
  app.get("/static-json", (_req, res) => res.json({ ok: true, mode: "static" }));
  app.get("/static-text", (_req, res) => res.type("text/plain; charset=utf-8").send("ok\n"));
  app.get("/static-status", (_req, res) => res.status(204).end());
  app.get("/static-problem", (_req, res) =>
    res.status(400).type("application/problem+json").send({ status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" }));
  app.get("/dynamic-json", (_req, res) => res.json({ ok: true, mode: "dynamic-0" }));
  app.get("/dynamic-text", (_req, res) => res.type("text/plain; charset=utf-8").send("dynamic-text\n"));
  app.get("/dynamic-async", async (_req, res) => res.json({ ok: true, mode: await Promise.resolve("async-dynamic") }));
  app.get("/ctx-query", (req, res) => res.json({ ok: true, query: req.query.q }));
  app.get("/ctx-headers", (req, res) => res.json({ ok: true, trace: req.get("x-trace") }));
  app.get("/ctx-services", (_req, res) => res.json({ ok: true, service: "bench-service" }));
  app.get("/plain-object", (_req, res) => res.json({ ok: true, mode: "plain-object" }));
  app.get("/exception", (_req, res) => res.status(500).json({ status: 500, title: "Internal Server Error" }));
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
  app.get("/static-json", async () => ({ ok: true, mode: "static" }));
  app.get("/static-text", async (_req, reply) => reply.type("text/plain; charset=utf-8").send("ok\n"));
  app.get("/static-status", async (_req, reply) => reply.code(204).send());
  app.get("/static-problem", async (_req, reply) =>
    reply.code(400).type("application/problem+json").send({ status: 400, title: "Static problem", code: "SLOPPY_E_STATIC_PROBLEM" }));
  app.get("/dynamic-json", async () => ({ ok: true, mode: "dynamic-0" }));
  app.get("/dynamic-text", async (_req, reply) => reply.type("text/plain; charset=utf-8").send("dynamic-text\n"));
  app.get("/dynamic-async", async () => ({ ok: true, mode: await Promise.resolve("async-dynamic") }));
  app.get("/ctx-query", async (req) => ({ ok: true, query: req.query.q }));
  app.get("/ctx-headers", async (req) => ({ ok: true, trace: req.headers["x-trace"] }));
  app.get("/ctx-services", async () => ({ ok: true, service: "bench-service" }));
  app.get("/plain-object", async () => ({ ok: true, mode: "plain-object" }));
  app.get("/exception", async (_req, reply) => reply.code(500).send({ status: 500, title: "Internal Server Error" }));
  await app.listen({ host: "127.0.0.1", port: 0 });
  return { baseUrl: app.server.address() ? `http://127.0.0.1:${app.server.address().port}` : "", close: () => app.close() };
}

async function waitForTcpReady(port) {
  const deadline = Date.now() + 10_000;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      await new Promise((resolve, reject) => {
        const socket = net.createConnection({ host: "127.0.0.1", port }, () => {
          socket.end();
          resolve();
        });
        socket.once("error", reject);
      });
      return;
    } catch (error) {
      lastError = error;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw lastError ?? new Error("server did not become ready");
}

async function startSloppyServer(mode, sloppyInfo, profile = null) {
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

  if (profile?.outPath) {
    await fs.mkdir(path.dirname(profile.outPath), { recursive: true });
  }
  const child = spawn(sloppyInfo.path, ["run", artifacts, "--host", "127.0.0.1", "--port", String(port)], {
    cwd: appRoot,
    env: {
      ...process.env,
      SLOPPY_JSON_DISPATCH: mode,
      ...(profile
        ? {
            SLOPPY_HTTP_PROFILE: "1",
            SLOPPY_HTTP_PROFILE_SCENARIO: profile.scenario,
            SLOPPY_HTTP_PROFILE_OUT: profile.outPath,
          }
        : {}),
    },
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
    await waitForTcpReady(port);
  } catch (error) {
    child.kill("SIGINT");
    throw new Error(`sloppy server did not become ready: ${error.message}\n${stderr}`);
  }
  return {
    baseUrl,
    profileOutPath: profile?.outPath,
    stderrPreview: () => stderr,
    close: () =>
      new Promise((resolve) => {
        child.once("exit", resolve);
        child.kill("SIGINT");
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
  if (scenario === "static-json") {
    return await fetch(`${baseUrl}/static-json`);
  }
  if (scenario === "static-text") {
    return await fetch(`${baseUrl}/static-text`);
  }
  if (scenario === "static-status") {
    return await fetch(`${baseUrl}/static-status`);
  }
  if (scenario === "static-problem") {
    return await fetch(`${baseUrl}/static-problem`);
  }
  if (scenario === "dynamic-json") {
    return await fetch(`${baseUrl}/dynamic-json`);
  }
  if (scenario === "dynamic-text") {
    return await fetch(`${baseUrl}/dynamic-text`);
  }
  if (scenario === "dynamic-async") {
    return await fetch(`${baseUrl}/dynamic-async`);
  }
  if (scenario === "ctx-query") {
    return await fetch(`${baseUrl}/ctx-query?q=abc`);
  }
  if (scenario === "ctx-headers") {
    return await fetch(`${baseUrl}/ctx-headers`, { headers: { "x-trace": "bench-trace" } });
  }
  if (scenario === "ctx-services") {
    return await fetch(`${baseUrl}/ctx-services`);
  }
  if (scenario === "plain-object") {
    return await fetch(`${baseUrl}/plain-object`);
  }
  if (scenario === "exception") {
    return await fetch(`${baseUrl}/exception`);
  }
  return await fetch(`${baseUrl}/route/${index % 1000}`);
}

async function runScenario(server, scenario) {
  const baseUrl = typeof server === "string" ? server : server.baseUrl;
  let bytes = 0;
  let checksum = 0n;
  for (let i = 0; i < warmupIterations; i += 1) {
    const response = await issueScenarioRequest(baseUrl, scenario, i);
    const text = await response.text();
    const correctness = expectedForScenario(scenario, i, text, response.status);
    if (!correctness.ok) {
      throw new Error(await validationFailureMessage(scenario, "warmup", correctness, response, text, server));
    }
  }
  const start = performance.now();
  for (let i = 0; i < iterations; i += 1) {
    const response = await issueScenarioRequest(baseUrl, scenario, i);
    const text = await response.text();
    const correctness = expectedForScenario(scenario, i, text, response.status);
    if (!correctness.ok) {
      throw new Error(await validationFailureMessage(scenario, "request", correctness, response, text, server));
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

function profilePathFor(runtime, scenario, repeatIndex) {
  const name = `${runtimeRowPrefix(runtime)}.${scenarioRowSegment(scenario)}`.replaceAll(/[^\w.-]/g, "_");
  return path.join(httpProfileOutDir, `http-profile-${httpProfileRunId}-${name}-repeat-${repeatIndex}.json`);
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
  const scenarios = [
    "static-json",
    "static-text",
    "static-status",
    "static-problem",
    "dynamic-json",
    "dynamic-text",
    "dynamic-async",
    "small",
    "invalid",
    "medium",
    "large",
    "ctx-query",
    "ctx-headers",
    "ctx-services",
    "plain-object",
    "exception",
    "route-table",
  ];
  try {
    if (httpProfile && name.startsWith("sloppy:loopback:")) {
      const rows = [];
      for (let repeatIndex = 1; repeatIndex <= repeat; repeatIndex += 1) {
        for (const scenario of scenarios) {
          const rowName = `${runtimeRowPrefix(name)}.${scenarioRowSegment(scenario)}`;
          try {
            server = await start({
              scenario: rowName,
              outPath: profilePathFor(name, scenario, repeatIndex),
            });
            const row = await runScenario(server, scenario);
            row.name = rowName;
            row.repeat = repeatIndex;
            rows.push(row);
          } catch (error) {
            rows.push({
              name: rowName,
              scenario,
              status: "FAIL",
              reason: error.message,
              iterations,
              warmupIterations,
              repeat: repeatIndex,
            });
          } finally {
            if (server != null) {
              await server.close();
              server = null;
            }
          }
        }
      }
      const status = rows.every((row) => row.status === "PASS") ? "PASS" : "FAIL";
      return { runtime: name, version, status, rows };
    }
    server = await start();
    const rows = [];
    for (let repeatIndex = 1; repeatIndex <= repeat; repeatIndex += 1) {
      for (const scenario of scenarios) {
        try {
          const row = await runScenario(server, scenario);
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
  results.push(
    await runRuntime("sloppy:loopback:native_json", sloppyInfo.version, (profile) =>
      startSloppyServer("native", sloppyInfo, profile),
    ),
  );
  results.push(
    await runRuntime("sloppy:loopback:generic_json", sloppyInfo.version, (profile) =>
      startSloppyServer("generic", sloppyInfo, profile),
    ),
  );
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
  httpProfile: {
    enabled: httpProfile,
    outDir: httpProfile ? httpProfileOutDir : null,
    runId: httpProfile ? httpProfileRunId : null,
  },
  client: "node fetch loop, one awaited request at a time",
  scenarios: {
    small: "POST /small with a small JSON login payload; validates JSON echo response",
    invalid: "POST /small with malformed JSON; expects HTTP 400",
    medium: "POST /medium with nested JSON payload; validates JSON echo response",
    large: "GET /large; validates 256-item JSON response",
    "static-json": "GET /static-json; validates a static Results.json payload eligible for native no-JS response dispatch",
    "static-text": "GET /static-text; validates a static Results.text payload eligible for native no-JS response dispatch",
    "static-status": "GET /static-status; validates a static no-body response eligible for native no-JS response dispatch",
    "static-problem": "GET /static-problem; validates a static ProblemDetails-style response eligible for native no-JS response dispatch",
    "dynamic-json": "GET /dynamic-json; validates a Results.json payload that still requires V8 handler execution and result conversion",
    "dynamic-text": "GET /dynamic-text; validates a Results.text payload that still requires V8 handler execution and result conversion",
    "dynamic-async": "GET /dynamic-async; validates an async Results.json payload through V8 Promise handling",
    "ctx-query": "GET /ctx-query?q=abc; validates query materialization on a V8 route",
    "ctx-headers": "GET /ctx-headers; validates request header materialization on a V8 route",
    "ctx-services": "GET /ctx-services; validates service injection on a V8 route",
    "plain-object": "GET /plain-object; validates plain object response conversion on a V8 route",
    exception: "GET /exception; validates thrown handler diagnostics mapping on a V8 route",
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
if (httpProfile) {
  const files = (await fs.readdir(httpProfileOutDir))
    .filter((entry) => entry.startsWith(`http-profile-${httpProfileRunId}-`) && entry.endsWith(".json"))
    .sort();
  const lines = [
    "# HTTP phase profile summary",
    "",
    "| Scenario | Requests | No-JS hits | Native hits | V8 calls | Cache hits | Cache misses | Sync returns | Promise returns | JSON stringify calls | Generic fallbacks | HTTP parse avg ns | Route dispatch avg ns | Handler lookup avg ns | V8 total avg ns | Context avg ns | Base ctx avg ns | Request facade avg ns | V8 call avg ns | V8 conversion avg ns | JSON stringify avg ns | Response serialization avg ns | Socket write avg ns |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
  ];
  for (const file of files) {
    const profile = JSON.parse(await fs.readFile(path.join(httpProfileOutDir, file), "utf8"));
    const phaseAvg = (name) => profile.phases?.[name]?.avgNs ?? 0;
    const counter = (name) => profile.counters?.[name] ?? 0;
    lines.push(
      `| ${profile.scenario ?? file} | ${profile.requests ?? 0} | ${counter("noJsResponsePlanHits")} | ${counter("nativeResponseHits")} | ${counter("v8HandlerCalls")} | ${counter("v8HandlerCacheHits")} | ${counter("v8HandlerCacheMisses")} | ${counter("syncReturns")} | ${counter("promiseReturns")} | ${counter("jsonStringifyCalls")} | ${counter("genericFallbacks")} | ${phaseAvg("http_parse")} | ${phaseAvg("route_dispatch")} | ${phaseAvg("v8_handler_lookup")} | ${phaseAvg("v8_handler_execution")} | ${phaseAvg("v8_context_construction")} | ${phaseAvg("v8_context_base_object_creation")} | ${phaseAvg("v8_request_facade_creation")} | ${phaseAvg("v8_handler_call")} | ${phaseAvg("v8_result_conversion")} | ${phaseAvg("v8_json_stringify_generic_serialization")} | ${phaseAvg("response_serialization_header_writing")} | ${phaseAvg("socket_write_scheduling")} |`,
    );
  }
  await fs.writeFile(path.join(httpProfileOutDir, "http-profile-summary.md"), `${lines.join("\n")}\n`, "utf8");
}
console.log(JSON.stringify(report, null, 2));
