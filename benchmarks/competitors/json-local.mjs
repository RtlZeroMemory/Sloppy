import http from "node:http";
import os from "node:os";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { performance } from "node:perf_hooks";
import { spawnSync } from "node:child_process";
import { spawn } from "node:child_process";

const require = createRequire(import.meta.url);

const args = new Map();
for (let i = 2; i < process.argv.length; i += 1) {
  if (process.argv[i].startsWith("--")) {
    args.set(process.argv[i].slice(2), process.argv[i + 1]);
    i += 1;
  }
}

const iterations = Number(args.get("iterations") ?? "100");
const outPath = args.get("out") ?? "artifacts/bench/json-competitors.json";
const scriptDir = path.dirname(fileURLToPath(import.meta.url));

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
    return { available: true, path: require.resolve(name) };
  } catch {
    return { available: false, path: null };
  }
}

function largeList() {
  return Array.from({ length: 256 }, (_, id) => ({ id, name: `user-${id}`, active: id % 2 === 0 }));
}

function validateLogin(value) {
  return value && typeof value.username === "string" && typeof value.password === "string";
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
  app.get("/route/:id", (req, res) => res.json({ ok: true, route: req.params.id }));
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
  app.get("/route/:id", async (req) => ({ ok: true, route: req.params.id }));
  await app.listen({ host: "127.0.0.1", port: 0 });
  return { baseUrl: app.server.address() ? `http://127.0.0.1:${app.server.address().port}` : "", close: () => app.close() };
}

async function runScenario(baseUrl, scenario) {
  let bytes = 0;
  let checksum = 0n;
  const start = performance.now();
  for (let i = 0; i < iterations; i += 1) {
    let response;
    if (scenario === "small") {
      response = await fetch(`${baseUrl}/small`, { method: "POST", body: JSON.stringify(payloads.small), headers: { "content-type": "application/json" } });
    } else if (scenario === "invalid") {
      response = await fetch(`${baseUrl}/small`, { method: "POST", body: payloads.invalid, headers: { "content-type": "application/json" } });
    } else if (scenario === "medium") {
      response = await fetch(`${baseUrl}/medium`, { method: "POST", body: JSON.stringify(payloads.medium), headers: { "content-type": "application/json" } });
    } else if (scenario === "large") {
      response = await fetch(`${baseUrl}/large`);
    } else {
      response = await fetch(`${baseUrl}/route/${i % 1000}`);
    }
    const text = await response.text();
    bytes += Buffer.byteLength(text);
    checksum += BigInt(checksumText(text));
  }
  const elapsedNs = Math.round((performance.now() - start) * 1_000_000);
  const seconds = elapsedNs / 1_000_000_000;
  return {
    scenario,
    status: "PASS",
    iterations,
    elapsedNs,
    nsPerOp: elapsedNs / iterations,
    bytesPerSecond: seconds > 0 ? bytes / seconds : 0,
    checksum: checksum.toString(),
  };
}

async function runRuntime(name, version, start) {
  const server = await start();
  try {
    const scenarios = ["small", "invalid", "medium", "large", "route-table"];
    const rows = [];
    for (const scenario of scenarios) {
      rows.push(await runScenario(server.baseUrl, scenario));
    }
    return { runtime: name, version, status: "PASS", rows };
  } finally {
    await server.close();
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
const nodeVersion = detectCommand("node");
if (nodeVersion.available) {
  results.push(await runRuntime("node:http", nodeVersion.version, startNodeHttpServer));
} else {
  results.push({ runtime: "node:http", status: "SKIPPED", reason: "node executable not found", rows: [] });
}

const expressInfo = optionalPackage("express");
if (nodeVersion.available && expressInfo.available) {
  results.push(await runRuntime("node:express", nodeVersion.version, startExpressServer));
} else {
  results.push({ runtime: "node:express", status: "SKIPPED", reason: "express dependency not installed", rows: [] });
}

const fastifyInfo = optionalPackage("fastify");
if (nodeVersion.available && fastifyInfo.available) {
  results.push(await runRuntime("node:fastify", nodeVersion.version, startFastifyServer));
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
  host: {
    platform: os.platform(),
    arch: os.arch(),
    release: os.release(),
    cpu: os.cpus()[0]?.model ?? "unknown",
  },
  results,
};

await fs.mkdir(path.dirname(outPath), { recursive: true });
await fs.writeFile(outPath, `${JSON.stringify(report, null, 2)}\n`, "utf8");
console.log(JSON.stringify(report, null, 2));
