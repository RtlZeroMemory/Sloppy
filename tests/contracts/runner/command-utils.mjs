import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function repoRootFromRunner() {
    return path.resolve(fileURLToPath(new URL("../../..", import.meta.url)));
}

export function parseRunnerArgs(argv) {
    const options = {
        area: "all",
        tier: "pr",
        format: "json",
        out: "",
        help: false,
    };
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index];
        if (arg === "--help" || arg === "-h") {
            options.help = true;
        } else if (arg === "--area") {
            options.area = String(argv[++index] ?? "");
        } else if (arg === "--tier") {
            options.tier = String(argv[++index] ?? "");
        } else if (arg === "--format") {
            options.format = String(argv[++index] ?? "");
        } else if (arg === "--out") {
            const value = argv[++index];
            if (typeof value !== "string" || value.length === 0 || value.startsWith("-")) {
                throw new Error("missing value for --out");
            }
            options.out = value;
        } else {
            throw new Error(`unknown option: ${arg}`);
        }
    }
    if (!["all", "cache", "package"].includes(options.area)) {
        throw new Error(`invalid --area: ${options.area}`);
    }
    if (!["pr", "extended", "torture"].includes(options.tier)) {
        throw new Error(`invalid --tier: ${options.tier}`);
    }
    if (!["json", "markdown"].includes(options.format)) {
        throw new Error(`invalid --format: ${options.format}`);
    }
    return options;
}

export async function writeJsonReport(out, report, repoRoot) {
    if (out === "") {
        return;
    }
    const outPath = path.isAbsolute(out) ? out : path.join(repoRoot, out);
    await fs.mkdir(path.dirname(outPath), { recursive: true });
    await fs.writeFile(outPath, `${JSON.stringify(report, null, 2)}\n`);
}

export function printHelp() {
    process.stdout.write(`Usage: node tests/contracts/runner/contract-runner.mjs --area cache|package|all --tier pr|extended|torture [--format json|markdown] [--out path]

Examples:
  node tests/contracts/runner/contract-runner.mjs --area cache --tier pr
  node tests/contracts/runner/contract-runner.mjs --area package --tier pr
  node tests/contracts/runner/contract-runner.mjs --area all --tier pr
  node tests/contracts/runner/contract-runner.mjs --area package --tier pr --format markdown
  node tests/contracts/runner/contract-runner.mjs --area package --tier pr --out artifacts/contracts/package-report.json
`);
}
