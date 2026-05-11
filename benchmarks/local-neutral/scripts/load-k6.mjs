import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";

function run(file, args) {
  return new Promise((resolve) => {
    const child = spawn(file, args, { encoding: "utf8", windowsHide: true });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => { stdout += chunk; });
    child.stderr.on("data", (chunk) => { stderr += chunk; });
    child.on("error", (error) => resolve({ status: 1, stdout: "", stderr: String(error) }));
    child.on("close", (status) => resolve({ status, stdout, stderr }));
  });
}

export async function runK6({ toolPath, workload, url, connections, duration, repeat, tempDir }) {
  const summaryPath = path.join(tempDir, `k6-${workload.name}-${connections}-${repeat}.json`);
  const requests = workload.mixed ? workload.requests : [workload];
  const baseUrl = new URL(url).origin;
  const scriptPath = path.join(tempDir, `k6-${workload.name}-${repeat}.js`);
  await fs.writeFile(scriptPath, `
import http from "k6/http";
import { sleep } from "k6";
import { Counter } from "k6/metrics";
export const options = { vus: ${connections}, duration: ${JSON.stringify(duration)} };
const benchErrors = new Counter("bench_errors");
const benchNon2xx = new Counter("bench_non2xx");
const requests = ${JSON.stringify(requests)};
const weighted = [];
for (const req of requests) for (let i = 0; i < (req.weight || 1); i++) weighted.push(req);
export default function() {
  const req = weighted[Math.floor(Math.random() * weighted.length)];
  const params = { headers: req.headers || {} };
  const response = http.request(req.method || "GET", ${JSON.stringify(baseUrl)} + req.path, req.body || null, params);
  if (response.error) benchErrors.add(1);
  if (response.status < 200 || response.status >= 300) benchNon2xx.add(1);
  sleep(0);
}
`, "utf8");
  const result = await run(toolPath, ["run", "--summary-export", summaryPath, scriptPath]);
  if (result.status !== 0) return { status: "FAIL", reason: result.stderr || result.stdout, raw: result };
  let raw;
  try {
    raw = JSON.parse(await fs.readFile(summaryPath, "utf8"));
  } catch (error) {
    return { status: "FAIL", reason: `k6 summary read/parse failed: ${String(error)}`, raw: result };
  }
  const metrics = raw.metrics ?? {};
  const value = (metric, key) => metrics[metric]?.values?.[key] ?? metrics[metric]?.[key] ?? null;
  return {
    status: "PASS",
    tool: "k6",
    repeat,
    rps: value("http_reqs", "rate"),
    latencyMs: {
      p50: value("http_req_duration", "p(50)") ?? value("http_req_duration", "med"),
      p95: value("http_req_duration", "p(95)"),
      p99: value("http_req_duration", "p(99)"),
    },
    requests: value("http_reqs", "count"),
    errors: value("bench_errors", "count") ?? 0,
    non2xx: value("bench_non2xx", "count") ?? 0,
    raw,
  };
}
