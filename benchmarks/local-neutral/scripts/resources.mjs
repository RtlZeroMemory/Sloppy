import { spawnSync } from "node:child_process";
import os from "node:os";

function finiteNumber(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

let cachedPowerShellCommand = null;

function powershellCommand() {
  if (cachedPowerShellCommand != null) return cachedPowerShellCommand;
  for (const command of ["pwsh", "powershell"]) {
    const result = spawnSync(process.platform === "win32" ? "where.exe" : "command", process.platform === "win32" ? [command] : ["-v", command], {
      encoding: "utf8",
      shell: process.platform !== "win32",
      timeout: 2000,
    });
    if (result.status === 0 && result.stdout.trim()) {
      cachedPowerShellCommand = result.stdout.trim().split(/\r?\n/)[0];
      return cachedPowerShellCommand;
    }
  }
  cachedPowerShellCommand = "powershell";
  return cachedPowerShellCommand;
}

function windowsSnapshot(pid) {
  const script = `$p = Get-Process -Id ${Number(pid)} -ErrorAction SilentlyContinue; ` +
    "if ($null -eq $p) { exit 3 }; " +
    "[pscustomobject]@{" +
    "pid = $p.Id; " +
    "rssBytes = $p.WorkingSet64; " +
    "workingSetBytes = $p.WorkingSet64; " +
    "privateMemoryBytes = $p.PrivateMemorySize64; " +
    "virtualMemoryBytes = $p.VirtualMemorySize64; " +
    "pagedMemoryBytes = $p.PagedMemorySize64; " +
    "cpuSeconds = $p.CPU; " +
    "handles = $p.HandleCount; " +
    "threads = $p.Threads.Count" +
    "} | ConvertTo-Json -Compress";
  const result = spawnSync(powershellCommand(), ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script], {
    encoding: "utf8",
    timeout: 5000,
    windowsHide: true,
  });
  if (result.status !== 0 || !result.stdout.trim()) return null;
  try {
    return JSON.parse(result.stdout);
  } catch {
    return null;
  }
}

function unixSnapshot(pid) {
  const result = spawnSync("ps", ["-p", String(pid), "-o", "rss=,vsz=,pcpu=,nlwp="], {
    encoding: "utf8",
    timeout: 5000,
  });
  if (result.status !== 0 || !result.stdout.trim()) return null;
  const [rssKb, virtualKb, cpuPercent, threads] = result.stdout.trim().split(/\s+/).map(finiteNumber);
  return {
    pid: Number(pid),
    rssBytes: rssKb == null ? null : rssKb * 1024,
    workingSetBytes: rssKb == null ? null : rssKb * 1024,
    privateMemoryBytes: null,
    virtualMemoryBytes: virtualKb == null ? null : virtualKb * 1024,
    pagedMemoryBytes: null,
    cpuSeconds: null,
    cpuPercent,
    handles: null,
    threads,
  };
}

export function processSnapshot(pid) {
  const base = process.platform === "win32" ? windowsSnapshot(pid) : unixSnapshot(pid);
  if (base == null) return null;
  return {
    sampledAt: new Date().toISOString(),
    monotonicMs: performance.now(),
    pid: Number(pid),
    ...base,
  };
}

function max(values) {
  const finite = values.filter(Number.isFinite);
  return finite.length === 0 ? null : Math.max(...finite);
}

function mean(values) {
  const finite = values.filter(Number.isFinite);
  if (finite.length === 0) return null;
  return finite.reduce((sum, value) => sum + value, 0) / finite.length;
}

export function summarizeResourceSamples(samples) {
  if (!Array.isArray(samples) || samples.length === 0) {
    return { status: "UNAVAILABLE", sampleCount: 0, reason: "no process resource samples were captured" };
  }
  const first = samples[0];
  const last = samples[samples.length - 1];
  const elapsedSeconds = (last.monotonicMs - first.monotonicMs) / 1000;
  const cpuSecondsDelta = Number.isFinite(first.cpuSeconds) && Number.isFinite(last.cpuSeconds)
    ? Math.max(0, last.cpuSeconds - first.cpuSeconds)
    : null;
  const averageCpuPercent = cpuSecondsDelta != null && elapsedSeconds > 0
    ? (cpuSecondsDelta / elapsedSeconds) * 100
    : mean(samples.map((sample) => sample.cpuPercent));
  return {
    status: "PASS",
    sampleCount: samples.length,
    intervalModel: "best-effort process sampler",
    platform: process.platform,
    logicalCores: os.cpus().length,
    elapsedSeconds,
    cpuSecondsDelta,
    averageCpuPercent,
    peakCpuPercent: max(samples.map((sample) => sample.cpuPercent)),
    peakRssBytes: max(samples.map((sample) => sample.rssBytes)),
    peakWorkingSetBytes: max(samples.map((sample) => sample.workingSetBytes)),
    peakPrivateMemoryBytes: max(samples.map((sample) => sample.privateMemoryBytes)),
    peakVirtualMemoryBytes: max(samples.map((sample) => sample.virtualMemoryBytes)),
    peakPagedMemoryBytes: max(samples.map((sample) => sample.pagedMemoryBytes)),
    peakHandles: max(samples.map((sample) => sample.handles)),
    peakThreads: max(samples.map((sample) => sample.threads)),
  };
}

export function startResourceSampler(pid, { intervalMs = 500 } = {}) {
  const samples = [];
  let timer = null;
  let running = true;
  const sample = () => {
    if (!running) return;
    const snapshot = processSnapshot(pid);
    if (snapshot != null) samples.push(snapshot);
  };
  sample();
  timer = setInterval(sample, Math.max(100, intervalMs));
  return {
    sample,
    stop() {
      sample();
      running = false;
      if (timer != null) clearInterval(timer);
      return {
        samples,
        summary: summarizeResourceSamples(samples),
      };
    },
  };
}
