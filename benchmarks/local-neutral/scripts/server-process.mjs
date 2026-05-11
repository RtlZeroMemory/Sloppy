import fs from "node:fs/promises";
import net from "node:net";
import path from "node:path";
import { spawn } from "node:child_process";
import { performance } from "node:perf_hooks";

export async function ensurePortFree(host, port) {
  await new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(port, host, () => {
      server.close(resolve);
    });
  });
}

export async function waitForHealth(url, timeoutMs = 10000) {
  const started = performance.now();
  let lastError = null;
  while (performance.now() - started < timeoutMs) {
    const remainingMs = Math.max(1, timeoutMs - (performance.now() - started));
    const controller = new AbortController();
    const abortTimer = setTimeout(() => controller.abort(), Math.min(remainingMs, 1000));
    try {
      const response = await fetch(url, { signal: controller.signal });
      const body = await response.text();
      if (response.status === 200 && body === "ok") return performance.now() - started;
    } catch (error) {
      lastError = error?.name === "AbortError" ? new Error("health probe timed out") : error;
    } finally {
      clearTimeout(abortTimer);
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Server did not become ready at ${url}: ${lastError?.message ?? "timeout"}`);
}

function waitForChildExit(child, timeoutMs) {
  return new Promise((resolve) => {
    if (!child || child.exitCode !== null || child.signalCode !== null) {
      resolve(true);
      return;
    }
    let settled = false;
    const finish = (exited) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      child.off("exit", onExit);
      child.off("close", onExit);
      resolve(exited);
    };
    const onExit = () => finish(true);
    const timer = setTimeout(() => finish(false), timeoutMs);
    child.once("exit", onExit);
    child.once("close", onExit);
  });
}

export async function startServer({ command, args, cwd, env, logDir, host, port }) {
  await fs.mkdir(logDir, { recursive: true });
  await ensurePortFree(host, port);
  const stdoutPath = path.join(logDir, "stdout.log");
  const stderrPath = path.join(logDir, "stderr.log");
  const stdout = await fs.open(stdoutPath, "w");
  const stderr = await fs.open(stderrPath, "w");
  const child = spawn(command, args, {
    cwd,
    env: { ...process.env, ...env },
    stdio: ["ignore", stdout.fd, stderr.fd],
    windowsHide: true,
    detached: process.platform !== "win32",
  });
  let exited = false;
  const markExited = () => {
    exited = true;
  };
  child.once("exit", markExited);
  let rejectEarly = null;
  const earlyExit = new Promise((_, reject) => {
    rejectEarly = reject;
  });
  const onStartupExit = () => rejectEarly(new Error("server exited before readiness check completed"));
  const onStartupError = (error) => rejectEarly(error);
  child.once("exit", onStartupExit);
  child.once("error", onStartupError);
  try {
    const startupMs = await Promise.race([waitForHealth(`http://${host}:${port}/health`), earlyExit]);
    if (exited) throw new Error("server exited before readiness check completed");
    return {
      child,
      pid: child.pid,
      command: [command, ...args].join(" "),
      stdoutPath,
      stderrPath,
      startupMs,
    };
  } catch (error) {
    await stopServer(child);
    throw error;
  } finally {
    child.off("exit", onStartupExit);
    child.off("error", onStartupError);
    await stdout.close();
    await stderr.close();
  }
}

export async function stopServer(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  try {
    if (process.platform !== "win32" && child.pid) {
      process.kill(-child.pid, "SIGTERM");
    } else {
      child.kill("SIGTERM");
    }
  } catch {
    try {
      child.kill("SIGKILL");
    } catch {
      return;
    }
  }
  if (await waitForChildExit(child, 1000)) return;
  try {
    if (process.platform !== "win32" && child.pid) {
      process.kill(-child.pid, "SIGKILL");
    } else {
      child.kill("SIGKILL");
    }
  } catch {
    // already gone
  }
  await waitForChildExit(child, 1000);
  if (child.exitCode === null && child.signalCode === null) {
    try {
      child.kill("SIGKILL");
    } catch {
      // already gone
    }
  }
}
