#!/usr/bin/env node
// Optional npm compatibility smoke script. Not part of normal CI.
//
// Installs a curated list of small pure-JavaScript packages into a scratch
// directory using `npm install --ignore-scripts --no-audit --no-fund`, then
// runs `sloppy build` against a generated entry that imports each package and
// reports a pass/fail summary.
//
// Internet access is required. The script does not gate release and does not
// promote any package to officially supported status; the only durable signal
// stays in the committed `tests/fixtures/npm-compat/` matrix and the dependency
// graph compatibility findings.
//
// Usage:
//   node tools/scripts/npm-compat-smoke.mjs                 # default candidates
//   node tools/scripts/npm-compat-smoke.mjs ms p-limit      # subset
//
// Environment overrides:
//   SLOPPY_BIN                path to a built sloppy binary
//   SLOPPY_NPM_SMOKE_DIR      scratch directory (default: temp/npm-smoke)

import { execFile, spawn } from "node:child_process";
import { mkdtempSync, mkdirSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const repoRoot = resolve(__dirname, "..", "..");

const defaultCandidates = [
    { name: "ms", import: 'import ms from "ms";\nexport const result = typeof ms;\n' },
    { name: "debug", import: 'import debug from "debug";\nexport const result = typeof debug;\n' },
    { name: "kleur", import: 'import { red } from "kleur/colors";\nexport const result = typeof red;\n' },
    { name: "picocolors", import: 'import pc from "picocolors";\nexport const result = typeof pc;\n' },
    { name: "mime", import: 'import mime from "mime";\nexport const result = typeof mime;\n' },
    { name: "qs", import: 'import qs from "qs";\nexport const result = typeof qs;\n' },
    { name: "fast-deep-equal", import: 'import equal from "fast-deep-equal";\nexport const result = typeof equal;\n' },
    { name: "eventemitter3", import: 'import EventEmitter from "eventemitter3";\nexport const result = typeof EventEmitter;\n' },
    { name: "p-limit", import: 'import pLimit from "p-limit";\nexport const result = typeof pLimit;\n' },
    { name: "nanoid", import: 'import { nanoid } from "nanoid";\nexport const result = typeof nanoid;\n' }
];

function pickCandidates(argv) {
    if (argv.length === 0) {
        return defaultCandidates;
    }
    const wanted = new Set(argv);
    const matched = defaultCandidates.filter((entry) => wanted.has(entry.name));
    if (matched.length !== wanted.size) {
        const missing = [...wanted].filter((name) => !matched.some((entry) => entry.name === name));
        console.warn(`smoke: skipping unknown candidates: ${missing.join(", ")}`);
    }
    return matched;
}

function resolveSloppyBinary() {
    if (process.env.SLOPPY_BIN && existsSync(process.env.SLOPPY_BIN)) {
        return process.env.SLOPPY_BIN;
    }
    const candidates = [
        join(repoRoot, "build/windows-dev/bin/Debug/sloppy.exe"),
        join(repoRoot, "build/windows-dev/bin/Release/sloppy.exe"),
        join(repoRoot, "build/windows-dev/bin/sloppy.exe"),
        join(repoRoot, "build/windows-dev/bin/sloppy"),
        join(repoRoot, "build/sloppy"),
        join(repoRoot, "build/sloppy.exe")
    ];
    for (const candidate of candidates) {
        if (existsSync(candidate)) {
            return candidate;
        }
    }
    return null;
}

async function runProcess(command, args, options, timeoutMs = 180_000) {
    return new Promise((resolveResult) => {
        const child = spawn(command, args, { ...options, shell: false });
        let stdout = "";
        let stderr = "";
        let settled = false;
        const finish = (result) => {
            if (settled) {
                return;
            }
            settled = true;
            clearTimeout(timeoutHandle);
            resolveResult(result);
        };
        const timeoutHandle = setTimeout(() => {
            child.kill("SIGKILL");
            finish({
                exitCode: -2,
                stdout,
                stderr: `${stderr}\nprocess timed out after ${timeoutMs}ms`.trim()
            });
        }, timeoutMs);

        child.stdout.on("data", (chunk) => {
            stdout += chunk.toString();
        });
        child.stderr.on("data", (chunk) => {
            stderr += chunk.toString();
        });
        child.on("error", (error) => {
            finish({ exitCode: -1, stdout, stderr: stderr + String(error) });
        });
        child.on("close", (exitCode) => {
            finish({ exitCode, stdout, stderr });
        });
    });
}

async function smokeOne(candidate, scratchDir, sloppyBin) {
    const projectDir = join(scratchDir, candidate.name);
    if (existsSync(projectDir)) {
        rmSync(projectDir, { recursive: true, force: true });
    }
    mkdirSync(projectDir, { recursive: true });

    writeFileSync(join(projectDir, "package.json"), JSON.stringify({
        name: `smoke-${candidate.name}`,
        private: true,
        type: "module",
        dependencies: { [candidate.name]: "*" }
    }, null, 4) + "\n");

    writeFileSync(join(projectDir, "sloppy.json"), JSON.stringify({
        kind: "program",
        entry: "src/main.ts",
        outDir: ".sloppy"
    }, null, 4) + "\n");

    mkdirSync(join(projectDir, "src"), { recursive: true });
    writeFileSync(join(projectDir, "src", "main.ts"), candidate.import + 'export function main() {\n    return result;\n}\n');

    const npm = process.platform === "win32" ? "npm.cmd" : "npm";
    const install = await runProcess(npm, [
        "install",
        "--ignore-scripts",
        "--no-audit",
        "--no-fund",
        "--silent"
    ], { cwd: projectDir });
    if (install.exitCode !== 0) {
        return { name: candidate.name, status: "install-failed", detail: install.stderr.trim().split("\n").slice(-3).join(" | ") };
    }

    if (sloppyBin === null) {
        return { name: candidate.name, status: "skipped", detail: "sloppy binary not built" };
    }

    const build = await runProcess(sloppyBin, ["build"], { cwd: projectDir });
    if (build.exitCode === 0) {
        return { name: candidate.name, status: "build-pass" };
    }
    const reason = build.stderr.trim().split("\n").slice(-2).join(" | ") || "build failed";
    return { name: candidate.name, status: "build-failed", detail: reason };
}

async function main() {
    const argv = process.argv.slice(2);
    const candidates = pickCandidates(argv);
    if (candidates.length === 0) {
        console.error("smoke: no candidates selected");
        process.exit(2);
    }

    const scratchDir = process.env.SLOPPY_NPM_SMOKE_DIR
        ? resolve(process.env.SLOPPY_NPM_SMOKE_DIR)
        : mkdtempSync(join(tmpdir(), "sloppy-npm-smoke-"));
    mkdirSync(scratchDir, { recursive: true });

    const sloppyBin = resolveSloppyBinary();
    if (sloppyBin === null) {
        console.warn("smoke: no built sloppy binary found; install will run but build will be skipped");
    } else {
        console.log(`smoke: using sloppy binary at ${sloppyBin}`);
    }
    console.log(`smoke: scratch directory at ${scratchDir}`);

    const results = [];
    for (const candidate of candidates) {
        process.stdout.write(`smoke: ${candidate.name} ... `);
        const result = await smokeOne(candidate, scratchDir, sloppyBin);
        results.push(result);
        process.stdout.write(`${result.status}${result.detail ? ` (${result.detail})` : ""}\n`);
    }

    const summary = results.reduce((accumulator, entry) => {
        accumulator[entry.status] = (accumulator[entry.status] ?? 0) + 1;
        return accumulator;
    }, {});
    console.log("\nsmoke summary:");
    for (const [status, count] of Object.entries(summary)) {
        console.log(`  ${status}: ${count}`);
    }
    console.log("\nThis script is advisory. Real package coverage is committed under tests/fixtures/npm-compat/.");
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
