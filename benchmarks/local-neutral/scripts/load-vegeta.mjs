import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";

function runPipeline(file, args, stdin = "") {
  return new Promise((resolve) => {
    const child = spawn(file, args, { windowsHide: true });
    const stdout = [];
    const stderr = [];
    child.stdout.on("data", (chunk) => { stdout.push(chunk); });
    child.stderr.on("data", (chunk) => { stderr.push(chunk); });
    child.on("error", (error) => resolve({ status: 1, stdout: Buffer.alloc(0), stderr: String(error) }));
    child.stdin.end(stdin);
    child.on("close", (status) => resolve({ status, stdout: Buffer.concat(stdout), stderr: Buffer.concat(stderr).toString("utf8") }));
  });
}

function targetLines(workload, baseUrl) {
  const requests = workload.mixed ? workload.requests : [workload];
  return requests.map((req) => {
    const lines = [`${req.method ?? "GET"} ${baseUrl.replace(/\/$/, "")}${req.path}`];
    for (const [name, value] of Object.entries(req.headers ?? {})) lines.push(`${name}: ${value}`);
    if (req.body) lines.push(`@${Buffer.from(req.body).toString("base64")}`);
    return lines.join("\n");
  }).join("\n\n");
}

function safeToken(value) {
  return path.basename(String(value)).replace(/[^A-Za-z0-9._-]/g, "_") || "0";
}

export async function runVegeta({ toolPath, workload, url, connections, duration, repeat, tempDir }) {
  if (workload.mixed) return { status: "SKIPPED", reason: "vegeta adapter does not implement weighted mixed workloads" };
  const binPath = path.join(tempDir, `vegeta-${workload.name}-${safeToken(repeat)}.bin`);
  const targets = targetLines(workload, url.replace(workload.path ?? "", ""));
  const rate = Math.max(connections * 1000, 1);
  const attack = await runPipeline(toolPath, ["attack", "-duration", duration, "-rate", String(rate), "-connections", String(connections), "-format", "http"], targets);
  if (attack.status !== 0) return { status: "FAIL", reason: attack.stderr || attack.stdout.toString("utf8"), raw: attack };
  await fs.writeFile(binPath, attack.stdout);
  const report = await runPipeline(toolPath, ["report", "-type", "json"], attack.stdout);
  if (report.status !== 0) return { status: "FAIL", reason: report.stderr || report.stdout.toString("utf8"), raw: report };
  const raw = JSON.parse(report.stdout.toString("utf8"));
  return {
    status: "PASS",
    tool: "vegeta",
    repeat,
    rps: raw.rate ?? null,
    latencyMs: {
      p50: raw.latencies?.["50th"] != null ? raw.latencies["50th"] / 1000000 : null,
      p95: raw.latencies?.["95th"] != null ? raw.latencies["95th"] / 1000000 : null,
      p99: raw.latencies?.["99th"] != null ? raw.latencies["99th"] / 1000000 : null,
    },
    requests: raw.requests ?? null,
    errors: raw.errors?.length ?? 0,
    non2xx: Object.entries(raw.status_codes ?? {}).filter(([code]) => !code.startsWith("2")).reduce((sum, [, count]) => sum + Number(count), 0),
    raw,
  };
}
