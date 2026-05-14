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

function safeToken(value) {
  return path.basename(String(value)).replace(/[^A-Za-z0-9._-]/g, "_") || "0";
}

export async function runWrk({ toolPath, workload, url, connections, duration, repeat, tempDir, runtime = "runtime", runLabel = null }) {
  if (workload.mixed) return { status: "SKIPPED", reason: "wrk adapter does not implement weighted mixed workloads" };
  const label = safeToken(runLabel ?? [runtime, workload.name, connections, repeat].join("-"));
  let scriptPath = null;
  const headers = Object.entries(workload.headers ?? {});
  if ((workload.method && workload.method !== "GET") || workload.body || headers.length > 0) {
    scriptPath = path.join(tempDir, `wrk-${label}.lua`);
    const headerLines = headers.map(([name, value]) => `wrk.headers[${JSON.stringify(name)}] = ${JSON.stringify(value)}`);
    const lines = [
      `wrk.method = ${JSON.stringify(workload.method ?? "GET")}`,
      `wrk.body = ${JSON.stringify(workload.body ?? "")}`,
      ...headerLines,
    ];
    await fs.writeFile(scriptPath, `${lines.join("\n")}\n`, "utf8");
  }
  const args = ["-t", "1", "-c", String(connections), "-d", duration, "--latency"];
  if (scriptPath) args.push("-s", scriptPath);
  args.push(url);
  const result = await run(toolPath, args);
  if (result.status !== 0) return { status: "FAIL", reason: result.stderr || result.stdout, raw: result };
  const text = result.stdout;
  const rpsMatch = text.match(/Requests\/sec:\s+([0-9.]+)/);
  const requestsMatch = text.match(/(\d+)\s+requests in/);
  if (!rpsMatch || !requestsMatch) {
    return { status: "FAIL", reason: "wrk output did not include Requests/sec or request count", raw: { stdout: text, stderr: result.stderr } };
  }
  const rps = Number(rpsMatch[1]);
  const requests = Number(requestsMatch[1]);
  const errors = Number(text.match(/Socket errors:.*?(\d+)/)?.[1] ?? 0);
  return {
    status: "PASS",
    tool: "wrk",
    repeat,
    rps: Number.isFinite(rps) ? rps : null,
    latencyMs: { p50: null, p95: null, p99: null }, // wrk needs custom Lua/latency parsing for structured p50/p95/p99.
    requests,
    errors,
    non2xx: null,
    raw: { stdout: text, stderr: result.stderr },
  };
}
