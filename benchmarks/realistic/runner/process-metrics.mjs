import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

async function readWindowsProcess(pid) {
  const command = [
    "$p = Get-Process -Id",
    String(pid),
    "-ErrorAction SilentlyContinue;",
    "if ($null -eq $p) { exit 3 };",
    "$p | Select-Object Id,WorkingSet64,PeakWorkingSet64,CPU | ConvertTo-Json -Compress",
  ].join(" ");
  const { stdout } = await execFileAsync("powershell.exe", [
    "-NoLogo",
    "-NoProfile",
    "-NonInteractive",
    "-Command",
    command,
  ], { timeout: 4000 });
  const parsed = JSON.parse(stdout.trim());
  const cpuSeconds = typeof parsed.CPU === "number" ? parsed.CPU : null;
  return {
    rssBytes: Number(parsed.WorkingSet64 ?? 0),
    peakRssBytes: Number(parsed.PeakWorkingSet64 ?? 0),
    cpuTotalMs: cpuSeconds === null ? null : cpuSeconds * 1000,
  };
}

async function readUnixProcess(pid) {
  const { stdout } = await execFileAsync("ps", ["-o", "rss=", "-o", "time=", "-p", String(pid)], {
    timeout: 4000,
  });
  const line = stdout.trim().split(/\r?\n/).at(-1);
  if (!line) {
    throw new Error("process not found");
  }
  const parts = line.trim().split(/\s+/);
  return {
    rssBytes: Number(parts[0] ?? 0) * 1024,
    peakRssBytes: Number(parts[0] ?? 0) * 1024,
    cpuTotalMs: null,
  };
}

async function readProcess(pid) {
  if (process.platform === "win32") {
    return readWindowsProcess(pid);
  }
  return readUnixProcess(pid);
}

export function createProcessSampler(pid, options = {}) {
  const samples = [];
  const intervalMs = options.intervalMs ?? 1000;
  let stopped = false;
  let pending = Promise.resolve();

  async function sample() {
    try {
      const current = await readProcess(pid);
      samples.push({ at: new Date().toISOString(), ...current });
    } catch {
      stopped = true;
    }
  }

  const timer = setInterval(() => {
    if (stopped) {
      clearInterval(timer);
      return;
    }
    pending = pending.then(sample, sample);
  }, intervalMs);
  timer.unref?.();
  pending = sample();

  return {
    async stop() {
      stopped = true;
      clearInterval(timer);
      await pending.catch(() => {});
      return summarizeProcessSamples(samples);
    },
    samples,
  };
}

export function summarizeProcessSamples(samples) {
  if (samples.length === 0) {
    return {
      peakWorkingSetBytes: null,
      avgWorkingSetBytes: null,
      cpuUserMs: null,
      cpuKernelMs: null,
      cpuTotalMs: null,
      samples: [],
    };
  }
  const rssValues = samples.map((sample) => sample.rssBytes).filter((value) => Number.isFinite(value));
  const peakValues = samples.map((sample) => sample.peakRssBytes).filter((value) => Number.isFinite(value));
  const cpuValues = samples.map((sample) => sample.cpuTotalMs).filter((value) => Number.isFinite(value));
  return {
    peakWorkingSetBytes: peakValues.length > 0 ? Math.max(...peakValues) : Math.max(...rssValues),
    avgWorkingSetBytes: rssValues.length > 0
      ? rssValues.reduce((sum, value) => sum + value, 0) / rssValues.length
      : null,
    cpuUserMs: null,
    cpuKernelMs: null,
    cpuTotalMs: cpuValues.length > 0 ? cpuValues[cpuValues.length - 1] : null,
    samples,
  };
}
