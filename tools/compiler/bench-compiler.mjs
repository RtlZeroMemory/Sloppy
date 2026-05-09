#!/usr/bin/env node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { execFileSync, spawn, spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const SIZES = {
  tiny: { files: 1, routes: 2, schemas: 1, services: 1, controllers: 0 },
  small: { files: 5, routes: 20, schemas: 5, services: 5, controllers: 0 },
  medium: { files: 25, routes: 200, schemas: 50, services: 25, controllers: 10 },
  large: { files: 100, routes: 1000, schemas: 200, services: 100, controllers: 50 },
  huge: { files: 500, routes: 5000, schemas: 1000, services: 500, controllers: 250 },
};

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(SCRIPT_DIR, "..", "..");

function parseArgs(argv) {
  const options = { suite: "smoke", sizes: [], compare: [] };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--help" || arg === "-h") {
      options.help = true;
      continue;
    }
    if (arg === "--compare") {
      const before = argv[index + 1];
      const after = argv[index + 2];
      if (!before || !after || before.startsWith("--") || after.startsWith("--")) {
        throw new Error("--compare requires BEFORE AFTER");
      }
      options.compare = [before, after];
      index += 2;
      continue;
    }
    if (!arg.startsWith("--")) {
      throw new Error(`unexpected argument '${arg}'`);
    }
    const key = arg.slice(2);
    const value = argv[index + 1];
    if (value === undefined || value.startsWith("--")) {
      throw new Error(`${arg} requires a value`);
    }
    if (key === "suite") {
      options.suite = value;
    } else if (key === "size") {
      options.sizes = value.split(",").map((item) => item.trim()).filter(Boolean);
    } else if (key === "out") {
      options.out = value;
    } else if (key === "sloppyc") {
      options.sloppyc = value;
    } else {
      throw new Error(`unknown option '${arg}'`);
    }
    index += 1;
  }
  return options;
}

function usage() {
  return `Usage:
  node tools/compiler/bench-compiler.mjs --suite smoke --out artifacts/bench/compiler-smoke.json
  node tools/compiler/bench-compiler.mjs --suite scale --size small,medium --out artifacts/bench/compiler-scale-smoke.json
  node tools/compiler/bench-compiler.mjs --compare artifacts/bench/before.json artifacts/bench/after.json
`;
}

function repoPath(...parts) {
  return path.join(ROOT, ...parts);
}

function runChecked(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd ?? ROOT,
    encoding: "utf8",
    stdio: options.stdio ?? "pipe",
  });
  if (result.status !== 0) {
    const stderr = result.stderr ? `\n${result.stderr}` : "";
    throw new Error(`${command} ${args.join(" ")} failed with exit code ${result.status}${stderr}`);
  }
  return result.stdout ?? "";
}

function gitText(args, fallback = "") {
  try {
    return runChecked("git", args).trim();
  } catch {
    return fallback;
  }
}

function resolveSloppyc(explicit) {
  if (explicit) {
    return path.resolve(explicit);
  }
  if (process.env.SLOPPYC_EXE) {
    return path.resolve(process.env.SLOPPYC_EXE);
  }
  if (process.env.SLOPPYC) {
    return path.resolve(process.env.SLOPPYC);
  }

  runChecked("cargo", ["build", "--manifest-path", repoPath("compiler", "Cargo.toml")], {
    stdio: "inherit",
  });
  const binary = path.join(
    ROOT,
    "compiler",
    "target",
    "debug",
    process.platform === "win32" ? "sloppyc.exe" : "sloppyc",
  );
  if (!fs.existsSync(binary)) {
    throw new Error(`sloppyc binary was not produced at ${binary}`);
  }
  return binary;
}

function sloppycVersion(sloppyc) {
  try {
    return runChecked(sloppyc, ["--version"]).trim();
  } catch {
    return "UNAVAILABLE";
  }
}

function sampleWorkingSetBytes(pid) {
  if (!pid) {
    return null;
  }
  try {
    if (process.platform === "win32") {
      const text = execFileSync(
        "powershell",
        [
          "-NoProfile",
          "-ExecutionPolicy",
          "Bypass",
          "-Command",
          `(Get-Process -Id ${pid} -ErrorAction SilentlyContinue).WorkingSet64`,
        ],
        { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] },
      ).trim();
      const value = Number(text);
      return Number.isFinite(value) && value > 0 ? value : null;
    }
    const statusPath = `/proc/${pid}/status`;
    if (fs.existsSync(statusPath)) {
      const status = fs.readFileSync(statusPath, "utf8");
      const match = status.match(/^VmRSS:\s+(\d+)\s+kB$/m) ?? status.match(/^VmHWM:\s+(\d+)\s+kB$/m);
      if (match) {
        return Number(match[1]) * 1024;
      }
    }
  } catch {
    return null;
  }
  return null;
}

function runMeasured(command, args, cwd) {
  return new Promise((resolve) => {
    const started = process.hrtime.bigint();
    const child = spawn(command, args, { cwd, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    let peakWorkingSetBytes = null;
    const sample = () => {
      const value = sampleWorkingSetBytes(child.pid);
      if (value !== null && (peakWorkingSetBytes === null || value > peakWorkingSetBytes)) {
        peakWorkingSetBytes = value;
      }
    };
    sample();
    const timer = setInterval(sample, 100);
    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("close", (code) => {
      clearInterval(timer);
      const durationMs = Number(process.hrtime.bigint() - started) / 1_000_000;
      resolve({ code, stdout, stderr, durationMs, peakWorkingSetBytes });
    });
  });
}

function artifactBytes(file) {
  try {
    return fs.statSync(file).size;
  } catch {
    return 0;
  }
}

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    return fallback;
  }
}

function writeOutput(out, value) {
  const text = `${JSON.stringify(value, null, 2)}\n`;
  if (out) {
    fs.mkdirSync(path.dirname(path.resolve(out)), { recursive: true });
    fs.writeFileSync(out, text);
  } else {
    process.stdout.write(text);
  }
}

function hostInfo() {
  const cpus = os.cpus();
  return {
    os: `${os.type()} ${os.release()}`,
    arch: os.arch(),
    cpu: cpus.length > 0 ? cpus[0].model : "UNKNOWN",
    logicalCores: cpus.length,
  };
}

function selectedSizes(suite, sizes) {
  if (sizes.length > 0) {
    return sizes;
  }
  if (suite === "smoke") {
    return ["tiny"];
  }
  if (suite === "scale") {
    return ["tiny", "small", "medium", "large"];
  }
  throw new Error(`unsupported compiler benchmark suite '${suite}'`);
}

async function runBenchmarks(options) {
  const sizes = selectedSizes(options.suite, options.sizes);
  for (const size of sizes) {
    if (!SIZES[size]) {
      throw new Error(`unknown compiler scale size '${size}'`);
    }
  }
  const sloppyc = resolveSloppyc(options.sloppyc);
  const compilerVersion = sloppycVersion(sloppyc);
  const startedAt = new Date().toISOString();
  const benchmarks = [];
  for (const size of sizes) {
    const shape = SIZES[size];
    const projectDir = repoPath("artifacts", "compiler-scale", size);
    const outDir = path.join(projectDir, ".sloppy");
    const timingsPath = repoPath("artifacts", "bench", `compiler-${size}-timings.json`);
    runChecked(
      process.execPath,
      [
        repoPath("tools", "compiler", "generate-scale-project.mjs"),
        "--size",
        size,
        "--out",
        projectDir,
      ],
      { stdio: "inherit" },
    );
    fs.rmSync(outDir, { recursive: true, force: true });
    fs.rmSync(timingsPath, { force: true });
    const measured = await runMeasured(
      sloppyc,
      ["build", path.join(projectDir, "src", "main.ts"), "--out", outDir, "--timings-json", timingsPath],
      ROOT,
    );
    const timings = readJson(timingsPath, { phases: {}, counters: {}, artifacts: {} });
    const planPath = path.join(outDir, "app.plan.json");
    const bundlePath = path.join(outDir, "app.js");
    const sourceMapPath = path.join(outDir, "app.js.map");
    benchmarks.push({
      id: `compiler.${size}.routes_${shape.routes}.files_${shape.files}`,
      status: measured.code === 0 ? "pass" : "fail",
      projectSize: size,
      files: shape.files,
      routes: shape.routes,
      schemas: shape.schemas,
      services: shape.services,
      controllers: shape.controllers,
      durationMs: Math.round(measured.durationMs),
      peakWorkingSetBytes: measured.peakWorkingSetBytes,
      artifacts: {
        planBytes: timings.artifacts?.planBytes ?? artifactBytes(planPath),
        appJsBytes: timings.artifacts?.appJsBytes ?? artifactBytes(bundlePath),
        sourceMapBytes: timings.artifacts?.sourceMapBytes ?? artifactBytes(sourceMapPath),
      },
      phases: timings.phases ?? {},
      counters: timings.counters ?? {},
      stderr: measured.code === 0 ? undefined : measured.stderr,
    });
  }
  return {
    schemaVersion: 1,
    startedAt,
    git: {
      commit: gitText(["rev-parse", "HEAD"], "UNKNOWN"),
      branch: gitText(["rev-parse", "--abbrev-ref", "HEAD"], "UNKNOWN"),
      dirty: gitText(["status", "--short"], "").length > 0,
    },
    host: hostInfo(),
    compiler: {
      version: compilerVersion,
      path: sloppyc,
    },
    suite: options.suite,
    benchmarks,
  };
}

function compareReports(beforePath, afterPath) {
  const before = readJson(beforePath, null);
  const after = readJson(afterPath, null);
  if (!before || !after) {
    throw new Error("compare inputs must be readable benchmark JSON reports");
  }
  const beforeById = new Map((before.benchmarks ?? []).map((entry) => [entry.id, entry]));
  const comparisons = [];
  for (const afterEntry of after.benchmarks ?? []) {
    const beforeEntry = beforeById.get(afterEntry.id);
    if (!beforeEntry) {
      continue;
    }
    const durationDeltaMs = afterEntry.durationMs - beforeEntry.durationMs;
    const durationDeltaPercent =
      beforeEntry.durationMs > 0 ? (durationDeltaMs / beforeEntry.durationMs) * 100 : null;
    comparisons.push({
      id: afterEntry.id,
      projectSize: afterEntry.projectSize,
      beforeDurationMs: beforeEntry.durationMs,
      afterDurationMs: afterEntry.durationMs,
      durationDeltaMs,
      durationDeltaPercent,
      beforeArtifacts: beforeEntry.artifacts,
      afterArtifacts: afterEntry.artifacts,
    });
  }
  return {
    schemaVersion: 1,
    comparedAt: new Date().toISOString(),
    before: beforePath,
    after: afterPath,
    comparisons,
  };
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(usage());
    return;
  }
  if (options.compare.length === 2) {
    writeOutput(options.out, compareReports(options.compare[0], options.compare[1]));
    return;
  }
  writeOutput(options.out, await runBenchmarks(options));
}

main().catch((error) => {
  process.stderr.write(`bench-compiler: ${error.message}\n`);
  process.exit(1);
});
