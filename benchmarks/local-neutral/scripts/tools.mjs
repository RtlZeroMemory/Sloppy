import os from "node:os";
import fs from "node:fs";
import { spawnSync } from "node:child_process";

export const TOOL_PRIORITY = ["oha", "wrk", "k6", "vegeta"];
export const RUNTIMES = ["sloppy", "node", "bun", "deno"];

export function commandOnPath(name) {
  const probe = process.platform === "win32" ? "where.exe" : "command";
  const args = process.platform === "win32" ? [name] : ["-v", name];
  const result = spawnSync(probe, args, { encoding: "utf8", shell: process.platform !== "win32", timeout: 5000 });
  if (result.error || result.signal || result.status !== 0) return null;
  const first = result.stdout.trim().split(/\r?\n/)[0];
  return first || null;
}

export function commandOutput(file, args = []) {
  if (!file) return null;
  const result = spawnSync(file, args, { encoding: "utf8", timeout: 5000 });
  if (result.error || result.signal || result.status !== 0) return null;
  return `${result.stdout}${result.stderr}`.trim().split(/\r?\n/)[0] ?? "";
}

function versionFor(name, path) {
  if (!path) return null;
  if (name === "wrk") return commandOutput(path, ["--version"]) ?? commandOutput(path, ["-v"]);
  if (name === "vegeta") return commandOutput(path, ["-version"]);
  return commandOutput(path, ["--version"]);
}

function resolveExecutable(name, explicit) {
  if (!explicit) return commandOnPath(name);
  const discovered = commandOnPath(explicit);
  if (discovered) return discovered;
  try {
    fs.accessSync(explicit, fs.constants.X_OK);
    return explicit;
  } catch {
    return null;
  }
}

export function detectTools(overrides = {}) {
  const tools = {};
  for (const name of [...TOOL_PRIORITY, ...RUNTIMES, "sloppyc"]) {
    const path = resolveExecutable(name, overrides[name]);
    tools[name] = path
      ? { status: "AVAILABLE", path, version: versionFor(name, path) ?? "unknown" }
      : { status: "UNAVAILABLE", path: "", version: "" };
  }
  return tools;
}

export function selectLoadTool(tools, requested) {
  if (requested && requested !== "auto") {
    if (!TOOL_PRIORITY.includes(requested)) {
      throw new Error(`Requested load generator is unavailable: ${requested}`);
    }
    const tool = tools[requested];
    if (!tool || tool.status !== "AVAILABLE") {
      throw new Error(`Requested load generator is unavailable: ${requested}`);
    }
    return requested;
  }
  const selected = TOOL_PRIORITY.find((name) => tools[name]?.status === "AVAILABLE");
  if (!selected) {
    throw new Error("No neutral load generator found. Install one of: oha, wrk, k6, vegeta.");
  }
  return selected;
}

export function runtimeAvailable(tools, runtime) {
  return runtime === "node" ? true : tools[runtime]?.status === "AVAILABLE";
}

export function hostMetadata() {
  return {
    os: process.platform,
    release: os.release(),
    arch: os.arch(),
    hostname: os.hostname(),
    cpu: os.cpus()[0]?.model ?? "",
    logicalCores: os.cpus().length,
    memoryBytes: os.totalmem(),
  };
}
