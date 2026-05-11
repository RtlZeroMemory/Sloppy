import http from "node:http";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const host = process.env.BENCH_HOST ?? "127.0.0.1";
const port = Number(process.env.BENCH_PORT ?? "41000");
const benchApiKey = process.env.BENCH_API_KEY ?? "benchmark-secret";
const staticPath = join(import.meta.dirname, "..", "sloppy", "public", "hello.txt");
let staticText = "";
try {
  staticText = readFileSync(staticPath, "utf8");
} catch (error) {
  console.error(`failed to read benchmark static fixture ${staticPath}: ${error.message}`);
  process.exit(1);
}

function send(res, status, body, contentType) {
  res.writeHead(status, { "content-type": contentType, "content-length": Buffer.byteLength(body) });
  res.end(body);
}

function json(res, status, value) {
  send(res, status, JSON.stringify(value), "application/json");
}

async function readJson(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function validUser(input) {
  return input && typeof input.name === "string" && input.name.length > 0 &&
    input.name.length <= 100 && typeof input.email === "string" && input.email.includes("@");
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", `http://${host}:${port}`);
  if (req.method === "GET" && url.pathname === "/health") return send(res, 200, "ok", "text/plain");
  if (req.method === "GET" && url.pathname === "/json-small") {
    return json(res, 200, { ok: true, message: "hello", count: 3 });
  }
  if (req.method === "GET" && url.pathname === "/users/123") {
    return json(res, 200, { id: "123", name: "Ada" });
  }
  if (req.method === "POST" && url.pathname === "/users") {
    try {
      const body = await readJson(req);
      if (!validUser(body)) return json(res, 400, { error: "invalid user" });
      return json(res, 200, { id: 1, name: body.name, email: body.email });
    } catch {
      return json(res, 400, { error: "invalid json" });
    }
  }
  if (req.method === "GET" && url.pathname === "/private") {
    if (req.headers["x-api-key"] !== benchApiKey) return json(res, 401, { error: "unauthorized" });
    return json(res, 200, { ok: true, sub: "api-key" });
  }
  if (req.method === "GET" && url.pathname === "/public/hello.txt") {
    return send(res, 200, staticText, "text/plain");
  }
  return json(res, 404, { error: "not found" });
});

server.listen(port, host);
