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

function pick(obj, paths) {
  for (const path of paths) {
    let current = obj;
    for (const part of path) current = current?.[part];
    if (Number.isFinite(Number(current))) return Number(current);
  }
  return null;
}

export async function runOha({ toolPath, workload, url, connections, duration, repeat }) {
  if (workload.mixed) return { status: "SKIPPED", reason: "oha adapter does not implement weighted mixed workloads" };
  const args = ["--output-format", "json", "--no-tui", "-c", String(connections), "-z", duration];
  for (const [name, value] of Object.entries(workload.headers ?? {})) args.push("-H", `${name}: ${value}`);
  if (workload.method && workload.method !== "GET") args.push("-m", workload.method);
  if (workload.body) args.push("-d", workload.body);
  args.push(url);
  const result = await run(toolPath, args);
  if (result.status !== 0) return { status: "FAIL", reason: result.stderr || result.stdout, raw: result };
  let raw;
  try {
    raw = JSON.parse(result.stdout);
  } catch {
    return { status: "FAIL", reason: "oha did not emit parseable JSON", raw: result };
  }
  const requests = pick(raw, [["summary", "total"], ["total"], ["requests", "total"]]);
  const rps = pick(raw, [["summary", "requestsPerSec"], ["requestsPerSec"], ["rps"]]);
  const statusCounts = raw.statusCodeDistribution ?? raw.statusCodes ?? {};
  const non2xx = Object.entries(statusCounts)
    .filter(([code]) => !String(code).startsWith("2"))
    .reduce((sum, [, count]) => sum + Number(count), 0);
  return {
    status: "PASS",
    tool: "oha",
    repeat,
    rps,
    latencyMs: {
      p50: pick(raw, [["latencyPercentiles", "p50"], ["latency", "p50"]]),
      p95: pick(raw, [["latencyPercentiles", "p95"], ["latency", "p95"]]),
      p99: pick(raw, [["latencyPercentiles", "p99"], ["latency", "p99"]]),
    },
    requests,
    errors: pick(raw, [["summary", "failed"], ["errors"]]) ?? 0,
    non2xx,
    raw,
  };
}
