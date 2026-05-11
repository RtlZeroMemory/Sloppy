import { Directory, File } from "sloppy/fs";
import { Process, System } from "sloppy/os";

let repoRoot = "";
let sloppy = "";
let sloppyc = "";
let update = false;
let requireV8 = false;
let selectedArea = "all";
let workRoot = "";
let goldenRoot = "";
let tempRoot = "";
let commandCounter = 0;
let selectedCliSection = "";
let selectedCompilerCase = "";
let selectedTemplate = "";
let selectedFlow = "";
let selectedExample = "";
let node = "";
let npmCli = "";

const compilerCases = [
    ["hello-mapget", "compiler/tests/fixtures/hello-mapget/input.js"],
    ["grouped-route", "compiler/tests/fixtures/grouped-route/input.js"],
    ["http-methods", "compiler/tests/fixtures/http-methods/input.js"],
    ["framework-metadata", "compiler/tests/fixtures/framework-metadata/input.ts"],
    ["full-framework-app-graph", "compiler/tests/fixtures/full-framework-app-graph/input.ts"],
    ["realistic-users-api", "compiler/tests/fixtures/realistic-users-api/input.js"],
    ["provider-capability", "compiler/tests/fixtures/provider-capability/input.js"],
    ["partial-body-without-schema", "compiler/tests/fixtures/partial-body-without-schema/input.js"],
    ["function-module", "compiler/tests/fixtures/function-module/input.js"],
    ["source-map", "compiler/tests/fixtures/source-map/input.js"],
];

const templates = ["api", "minimal-api", "program", "cli", "package-api", "node-compat"];

const diagnosticCases = [
    {
        name: "unsupported-node-import",
        command: "build",
        fixture: "compiler/tests/fixtures/node-fs-import/input.js",
        expect: "SLOPPYC_E_UNSUPPORTED",
    },
    {
        name: "unsupported-npm-import",
        command: "build",
        fixture: "compiler/tests/fixtures/unsupported-import-specifier/input.js",
        expect: "SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER",
    },
    {
        name: "unsupported-dynamic-import",
        command: "build",
        fixture: "compiler/tests/fixtures/unsupported-dynamic-import/input.js",
        expect: "SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT",
    },
    {
        name: "create-invalid-name",
        sloppyArgs: ["create", "bad name", "--template", "minimal-api"],
        expect: "project name",
    },
    {
        name: "create-removed-dogfood",
        sloppyArgs: ["create", "removed-dogfood", "--template", "dogfood"],
        expect: "unsupported template",
    },
    {
        name: "create-removed-full-api",
        sloppyArgs: ["create", "removed-full-api", "--template", "full-api"],
        expect: "unsupported template",
    },
    {
        name: "run-missing-artifacts",
        sloppyArgs: ["run", "--artifacts", "tests/fixtures/run/missing", "--once", "GET", "/"],
        expect: "artifact path not found",
    },
    {
        name: "openapi-program-plan",
        fixtureProgram: true,
        sloppyArgs: ["openapi", ".sloppy"],
        expect: "OpenAPI is only available for web Plans",
    },
];

function toSlash(value) {
    return String(value || "").replace(/\\/g, "/");
}

function isAbsolutePath(value) {
    const text = toSlash(value);
    return text.startsWith("/") || /^[A-Za-z]:\//.test(text);
}

function trimTrailingSlash(value) {
    const text = toSlash(value);
    if (/^[A-Za-z]:\/$/.test(text) || text === "/") {
        return text;
    }
    return text.replace(/\/+$/g, "");
}

function escapeRegExp(value) {
    return String(value).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function joinPath(...parts) {
    let output = "";
    for (const raw of parts) {
        const part = toSlash(raw);
        if (!part) {
            continue;
        }
        if (!output || isAbsolutePath(part)) {
            output = part;
        } else {
            output = `${trimTrailingSlash(output)}/${part.replace(/^\/+/g, "")}`;
        }
    }
    return output || ".";
}

function dirname(file) {
    const normalized = trimTrailingSlash(file);
    const index = normalized.lastIndexOf("/");
    if (index <= 0) {
        return ".";
    }
    if (index === 2 && /^[A-Za-z]:/.test(normalized)) {
        return normalized.slice(0, 3);
    }
    return normalized.slice(0, index);
}

function basename(file) {
    const normalized = trimTrailingSlash(file);
    const index = normalized.lastIndexOf("/");
    return index >= 0 ? normalized.slice(index + 1) : normalized;
}

function resolvePath(base, value) {
    const text = toSlash(value);
    return isAbsolutePath(text) ? text : joinPath(base, text);
}

function relativePath(from, to) {
    const prefix = `${trimTrailingSlash(from)}/`;
    const target = toSlash(to);
    return target.startsWith(prefix) ? target.slice(prefix.length) : target;
}

function fsPath(value) {
    const path = toSlash(value);
    const repoPrefix = `${trimTrailingSlash(repoRoot)}/`;
    if (path === trimTrailingSlash(repoRoot)) {
        return ".";
    }
    if (path.startsWith(repoPrefix)) {
        return `./${path.slice(repoPrefix.length)}`;
    }
    if (path.startsWith("./") || path.startsWith("../") || path === ".") {
        return path;
    }
    if (!isAbsolutePath(path)) {
        return `./${path}`;
    }
    return path;
}

function winPath(value) {
    return String(value).replace(/\//g, "\\");
}

function cmdFileQuote(value) {
    return `"${winPath(value).replace(/"/g, '""')}"`;
}

function cmdArgQuote(value) {
    return `"${String(value).replace(/"/g, '""')}"`;
}

function cmdCommandQuote(value) {
    const text = String(value || "");
    return /[\\/:]/.test(text) ? cmdFileQuote(text) : text;
}

function commandFromCwd(executable, cwd) {
    return executable;
}

function shouldUseWindowsCommandScript(executable) {
    if (System.platform !== "windows") {
        return false;
    }
    const text = String(executable || "").toLowerCase();
    return /[\\/:]/.test(text) || text === "npm" || text.endsWith(".cmd") || text.endsWith(".bat");
}

function parseArgs(raw) {
    const out = {};
    for (let index = 0; index < raw.length; index += 1) {
        const value = raw[index];
        if (value === "--update") {
            out.update = true;
        } else if (value === "--require-v8") {
            out.requireV8 = true;
        } else if (value.startsWith("--")) {
            out[value.slice(2)] = raw[++index];
        } else {
            throw new Error(`unknown argument: ${value}`);
        }
    }
    return out;
}

function shouldRun(area) {
    return selectedArea === "all" || selectedArea === area;
}

async function pathExists(path) {
    return (await File.exists(fsPath(path))) || (await Directory.exists(fsPath(path)));
}

function errorMessage(error) {
    return error && error.message ? error.message : String(error);
}

async function withFsContext(description, action) {
    try {
        return await action();
    } catch (error) {
        throw new Error(`${description}: ${errorMessage(error)}`);
    }
}

async function ensureParent(file) {
    const parent = dirname(file);
    await withFsContext(`create parent directory ${parent}`, async () => {
        await Directory.create(fsPath(parent), { recursive: true });
    });
}

async function mkdirClean(dir) {
    if (await Directory.exists(fsPath(dir))) {
        await withFsContext(`delete directory ${dir}`, async () => {
            await Directory.delete(fsPath(dir), { recursive: true });
        });
    } else if (await File.exists(fsPath(dir))) {
        await withFsContext(`delete file ${dir}`, async () => {
            await File.delete(fsPath(dir));
        });
    }
    await withFsContext(`create directory ${dir}`, async () => {
        await Directory.create(fsPath(dir), { recursive: true });
    });
}

async function run(executable, runArgs, options = {}) {
    const cwd = options.cwd || repoRoot;
    let command = commandFromCwd(executable, cwd);
    let commandArgs = runArgs;
    const runOptions = {
        capture: "text",
        timeoutMs: options.timeout || 180000,
        maxStdoutBytes: 16 * 1024 * 1024,
        maxStderrBytes: 16 * 1024 * 1024,
    };
    if (shouldUseWindowsCommandScript(executable)) {
        commandCounter += 1;
        const scriptDir = joinPath(workRoot, "_commands");
        const script = joinPath(scriptDir, `run-${commandCounter}.cmd`);
        const commandLine = [cmdCommandQuote(executable), ...runArgs.map((part) => cmdArgQuote(part))].join(" ");
        await Directory.create(fsPath(scriptDir), { recursive: true });
        await File.writeText(fsPath(script), `@echo off\r\ncd /d ${cmdFileQuote(cwd)}\r\n${commandLine}\r\n`);
        command = "cmd.exe";
        commandArgs = ["/d", "/c", basename(script)];
        runOptions.cwd = fsPath(scriptDir);
    } else if (trimTrailingSlash(cwd) !== trimTrailingSlash(repoRoot)) {
        runOptions.cwd = fsPath(cwd);
    }
    const result = await Process.run(command, commandArgs, runOptions);
    return {
        exitCode: result.exitCode,
        stdout: result.stdout || "",
        stderr: result.stderr || "",
        command: [basename(executable), ...runArgs].join(" "),
    };
}

function requireSuccess(result, description) {
    if (result.exitCode !== 0) {
        throw new Error(`${description} failed with ${result.exitCode}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
    }
}

function requireFailure(result, needle, description) {
    if (result.exitCode === 0) {
        throw new Error(`${description} unexpectedly succeeded\nstdout:\n${result.stdout}`);
    }
    if (needle && !`${result.stdout}\n${result.stderr}`.includes(needle)) {
        throw new Error(`${description} did not contain ${needle}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
    }
}

function normalizeText(value) {
    let text = String(value || "").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
    const replacements = [repoRoot, workRoot, tempRoot].filter((item) => item);
    for (const replacement of replacements) {
        const token = replacement === repoRoot ? "<repoRoot>" : replacement === workRoot ? "<workRoot>" : "<tempRoot>";
        for (const form of [replacement, toSlash(replacement), toSlash(replacement).replace(/\//g, "\\")]) {
            text = text.split(form).join(token);
        }
    }
    text = text.replace(/\\+/g, "/");
    text = text.replace(/\/+\?\/+(?=<(?:repoRoot|tempRoot)>)/g, "");
    if (workRoot.startsWith(trimTrailingSlash(repoRoot))) {
        text = text.split(`<repoRoot>/${relativePath(repoRoot, workRoot)}`).join("<repoRoot>/artifacts/alpha-proof/work");
    }
    const workLeaf = basename(workRoot);
    if (workLeaf) {
        const tempWorkRootPattern = new RegExp(`/*<tempRoot>[^\\s"']*/alpha-proof/${escapeRegExp(workLeaf)}(?=/|$)`, "g");
        text = text.replace(tempWorkRootPattern, "<repoRoot>/artifacts/alpha-proof/work");
    }
    text = text.replace(/[A-Z]:[\\/][^\s"']+/g, (match) => {
        if (match.includes(".sloppy") || match.includes("artifacts") || match.includes("compiler")) {
            return toSlash(match);
        }
        return "<path>";
    });
    text = text.replace(/\/tmp\/[^\s"']+/g, "<temp-path>");
    text = text.replace(/\bport [0-9]{2,5}\b/gi, "port <port>");
    text = text.replace(/\b127\.0\.0\.1:[0-9]{2,5}\b/g, "127.0.0.1:<port>");
    text = text.replace(/\b[0-9]+(?:\.[0-9]+)?ms\b/g, "<duration>");
    text = text.replace(/\b[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+Z\b/g, "<timestamp>");
    return text;
}

function normalizeJsonValue(value) {
    if (Array.isArray(value)) {
        return value.map((item) => normalizeJsonValue(item));
    }
    if (value && typeof value === "object") {
        const out = {};
        for (const key of Object.keys(value).sort()) {
            out[key] = normalizeJsonValue(value[key]);
        }
        return out;
    }
    if (typeof value === "string") {
        return normalizeText(value);
    }
    return value;
}

function canonicalJson(value) {
    return `${JSON.stringify(normalizeJsonValue(value), null, 2)}\n`;
}

async function compareOrUpdateGolden(file, actual) {
    const normalizedActual = actual.endsWith("\n") ? actual : `${actual}\n`;
    if (update) {
        await ensureParent(file);
        await File.writeText(fsPath(file), normalizedActual);
        return;
    }
    if (!(await File.exists(fsPath(file)))) {
        throw new Error(`missing golden: ${file}`);
    }
    const expected = (await File.readText(fsPath(file))).replace(/\r\n/g, "\n");
    if (expected !== normalizedActual) {
        const actualPath = joinPath(repoRoot, "artifacts/golden-actual", relativePath(repoRoot, file));
        await ensureParent(actualPath);
        await File.writeText(fsPath(actualPath), normalizedActual);
        throw new Error(`golden mismatch: ${file}\nactual written to ${actualPath}`);
    }
}

async function snapshot(area, name, ext, content) {
    const file = joinPath(goldenRoot, area, `${name}.${ext}`);
    await compareOrUpdateGolden(file, content);
}

async function snapshotText(area, name, text) {
    await snapshot(area, name, "txt", normalizeText(text));
}

async function snapshotJson(area, name, value) {
    await snapshot(area, name, "json", canonicalJson(value));
}

async function commandSnapshot(area, name, result, options = {}) {
    const payload = {
        exitCode: result.exitCode,
        stdout: options.jsonStdout && result.stdout.trim() ? JSON.parse(result.stdout) : normalizeText(result.stdout),
        stderr: normalizeText(result.stderr),
    };
    await snapshotJson(area, name, payload);
}

function v8VariantName(name) {
    return requireV8 ? `${name}-v8` : name;
}

async function readJson(file) {
    return JSON.parse(await File.readText(fsPath(file)));
}

async function stableFileList(root, options = {}) {
    const skip = new Set(options.skip || [".git", ".sloppy", "node_modules"]);
    const files = [];
    for await (const entry of Directory.walk(fsPath(root))) {
        const parts = String(entry.name).split("/");
        if (parts.some((part) => skip.has(part))) {
            continue;
        }
        if (entry.kind === "file") {
            files.push(entry.name);
        }
    }
    return files.sort();
}

async function copyTree(from, to) {
    await withFsContext(`create copy destination ${to}`, async () => {
        await Directory.create(fsPath(to), { recursive: true });
    });
    for await (const entry of Directory.walk(fsPath(from))) {
        const source = joinPath(from, entry.name);
        const target = joinPath(to, entry.name);
        if (entry.kind === "directory") {
            await withFsContext(`copy directory ${source} to ${target}`, async () => {
                await Directory.create(fsPath(target), { recursive: true });
            });
        } else if (entry.kind === "file") {
            await ensureParent(target);
            await withFsContext(`copy file ${source} to ${target}`, async () => {
                await File.copy(fsPath(source), fsPath(target), { overwrite: true });
            });
        }
    }
}

function planSummary(plan) {
    return {
        kind: plan.kind || "web",
        requiredFeatures: plan.requiredFeatures || [],
        capabilities: (plan.capabilities || []).map((cap) => ({
            token: cap.token,
            kind: cap.kind,
            access: cap.access,
            provider: cap.provider,
        })),
        handlers: (plan.handlers || []).map((handler) => ({
            id: handler.id,
            exportName: handler.exportName,
            displayName: handler.displayName,
        })),
        routes: (plan.routes || []).map((route) => ({
            method: route.method,
            pattern: route.pattern,
            name: route.name,
            handlerId: route.handlerId,
            tags: route.tags || [],
        })),
        completeness: plan.metadata && plan.metadata.completeness ? plan.metadata.completeness : null,
    };
}

function sourceMapSummary(sourceMap) {
    return {
        version: sourceMap.version,
        sources: sourceMap.sources || [],
        x_sloppy: sourceMap.x_sloppy || null,
    };
}

async function artifactContract(dir) {
    const appJs = await File.readText(fsPath(joinPath(dir, "app.js")));
    const sourceMap = await readJson(joinPath(dir, "app.js.map"));
    const plan = await readJson(joinPath(dir, "app.plan.json"));
    return {
        files: await stableFileList(dir),
        planKind: plan.kind || "web",
        sourceMapHasXSloppy: Boolean(sourceMap.x_sloppy),
        uniqueSources: new Set(sourceMap.sources || []).size === (sourceMap.sources || []).length,
        containsProgramMain: appJs.includes("__sloppy_program_main"),
        containsHandlerRegistration: appJs.includes("globalThis.__sloppy_handler_"),
        containsTypescriptTypeSyntax: /\btype\s+\w+\s*=|:\s*(string|number|boolean)\b/.test(appJs),
        duplicateFrameworkHelpers: (appJs.match(/__sloppy_framework_services/g) || []).length > 1,
    };
}

async function buildSource(source, outDir) {
    const result = await run(sloppyc, ["build", joinPath(repoRoot, source), "--out", outDir]);
    requireSuccess(result, `sloppyc build ${source}`);
    for (const artifact of ["app.plan.json", "app.js", "app.js.map"]) {
        if (!(await File.exists(fsPath(joinPath(outDir, artifact))))) {
            throw new Error(`missing artifact ${artifact} in ${outDir}`);
        }
    }
}

async function buildWithSloppy(cwd, extraArgs = []) {
    const result = await run(sloppy, ["build", ...extraArgs], { cwd });
    requireSuccess(result, `sloppy build in ${cwd}`);
}

async function seedTemplates(cwd, onlyTemplate = "") {
    if (onlyTemplate) {
        await copyTree(joinPath(repoRoot, "templates", onlyTemplate), joinPath(cwd, "templates", onlyTemplate));
        return;
    }
    await copyTree(joinPath(repoRoot, "templates"), joinPath(cwd, "templates"));
}

async function packageWithSloppy(cwd, extraArgs = []) {
    const result = await run(sloppy, ["package", "--format", "json", ...extraArgs], { cwd });
    requireSuccess(result, `sloppy package in ${cwd}`);
    return result;
}

async function installTemplateDependencies(projectDir, template) {
    if (template !== "package-api") {
        return;
    }
    const command = node && npmCli ? node : "npm";
    const args = node && npmCli
        ? [npmCli, "install", "--ignore-scripts", "--no-audit"]
        : ["install", "--ignore-scripts", "--no-audit"];
    const result = await run(command, args, { cwd: projectDir, timeout: 180000 });
    requireSuccess(result, `npm install for ${template}`);
}

function withPassthroughArgs(runArgs) {
    return runArgs.length > 0 ? ["--", ...runArgs] : [];
}

function packageRunCommandsForTemplate(template) {
    switch (template) {
        case "api":
            return [
                ["health", ["run", "package", "--once", "GET", "/health"]],
                ["static", ["run", "package", "--once", "GET", "/public/hello.txt"]],
            ];
        case "minimal-api":
            return [
                ["health", ["run", "package", "--once", "GET", "/health"]],
                ["hello", ["run", "package", "--once", "GET", "/hello/Ada"]],
            ];
        case "package-api":
            return [
                ["health", ["run", "package", "--once", "GET", "/health"]],
                ["users", ["run", "package", "--once", "GET", "/users/Ada"]],
            ];
        case "program":
            return [["program", ["run", "package", ...withPassthroughArgs(["--name", "Ada"])]]];
        case "cli":
            return [["echo", ["run", "package", ...withPassthroughArgs(["echo", "outside"])]]];
        case "node-compat":
            return [["program", ["run", "package"]]];
        default:
            throw new Error(`unsupported package run template: ${template}`);
    }
}

async function assertPackageHasNoNodeModules(packageDir, description) {
    if (await Directory.exists(fsPath(joinPath(packageDir, "node_modules")))) {
        throw new Error(`${description} package unexpectedly contains node_modules`);
    }
}

async function proveOutsidePackageRun(area, name, projectDir, template) {
    const outsideRoot = joinPath(workRoot, area, `${name}-outside`);
    const outsidePackage = joinPath(outsideRoot, "package");
    await mkdirClean(outsideRoot);
    await copyTree(joinPath(projectDir, ".sloppy/package"), outsidePackage);
    await assertPackageHasNoNodeModules(outsidePackage, `${area}/${name}`);
    await snapshotJson(area, `${name}.outside-package-file-tree`, await stableFileList(outsidePackage, { skip: [".git"] }));

    for (const [runName, runArgs] of packageRunCommandsForTemplate(template)) {
        const runResult = await run(sloppy, runArgs, { cwd: outsideRoot });
        if (requireV8) {
            requireSuccess(runResult, `${area}/${name} outside package ${runName}`);
        } else {
            requireFailure(runResult, "requires V8-enabled build", `${area}/${name} non-V8 outside package ${runName}`);
        }
        await commandSnapshot(area, `${name}.outside-run-${runName}-${requireV8 ? "v8" : "non-v8"}`, runResult);
    }
}

async function runCliArea() {
    if (!selectedCliSection || selectedCliSection === "help") {
        const commands = [
            ["sloppy-help", [sloppy, ["--help"]]],
            ["sloppy-version", [sloppy, ["--version"]]],
            ["sloppy-create-help", [sloppy, ["create", "--help"]]],
            ["sloppy-build-help", [sloppy, ["build", "--help"]]],
            ["sloppy-run-help", [sloppy, ["run", "--help"]]],
            ["sloppy-dev-help", [sloppy, ["dev", "--help"]]],
            ["sloppy-package-help", [sloppy, ["package", "--help"]]],
            ["sloppy-routes-help", [sloppy, ["routes", "--help"]]],
            ["sloppy-deps-help", [sloppy, ["deps", "--help"]]],
            ["sloppy-capabilities-help", [sloppy, ["capabilities", "--help"]]],
            ["sloppy-doctor-help", [sloppy, ["doctor", "--help"]]],
            ["sloppy-audit-help", [sloppy, ["audit", "--help"]]],
            ["sloppy-openapi-help", [sloppy, ["openapi", "--help"]]],
            ["sloppy-db-help", [sloppy, ["db", "--help"]]],
            ["sloppyc-help", [sloppyc, ["--help"]]],
        ];
        for (const [name, pair] of commands) {
            const result = await run(pair[0], pair[1]);
            requireSuccess(result, name);
            await snapshotText("cli", name, result.stdout);
        }
    }

    if (!selectedCliSection || selectedCliSection === "web") {
        const webPlan = "tests/fixtures/cli/route-metadata.plan.json";
        const webCommands = [
            ["web-routes-json", ["routes", "--plan", webPlan, "--format", "json"], true],
            ["web-routes-text", ["routes", "--plan", webPlan, "--format", "text"], false],
            [v8VariantName("web-doctor-json"), ["doctor", "--plan", webPlan, "--format", "json"], true],
            ["web-audit-json", ["audit", "--plan", webPlan, "--format", "json"], true],
            ["web-openapi-json", ["openapi", "--plan", webPlan], true],
        ];
        for (const [name, commandArgs, json] of webCommands) {
            const result = await run(sloppy, commandArgs);
            requireSuccess(result, name);
            await commandSnapshot("cli", name, result, { jsonStdout: json });
        }
    }

    if (!selectedCliSection || selectedCliSection === "ffi") {
        const ffiPlan = "tests/fixtures/cli/ffi-policy.plan.json";
        const ffiCommands = [
            ["ffi-capabilities-json", ["capabilities", "--plan", ffiPlan, "--format", "json"], true],
            [v8VariantName("ffi-doctor-json"), ["doctor", "--plan", ffiPlan, "--format", "json"], true],
            ["ffi-audit-json", ["audit", "--plan", ffiPlan, "--format", "json"], true],
        ];
        for (const [name, commandArgs, json] of ffiCommands) {
            const result = await run(sloppy, commandArgs);
            requireSuccess(result, name);
            await commandSnapshot("cli", name, result, { jsonStdout: json });
        }
    }

    if (selectedCliSection && selectedCliSection !== "program") {
        return;
    }

    const cliRoot = joinPath(workRoot, "cli");
    const programDir = joinPath(cliRoot, "program-metadata");
    await mkdirClean(cliRoot);
    await Directory.create(fsPath(joinPath(programDir, "src")), { recursive: true });
    await File.writeText(fsPath(joinPath(programDir, "sloppy.json")), '{ "kind": "program", "entry": "src/main.ts", "outDir": ".sloppy" }\n');
    await File.writeText(fsPath(joinPath(programDir, "src/main.ts")), 'import { File } from "sloppy/fs";\nexport function main(args) { console.log(args.join("|")); return typeof File; }\n');
    await buildWithSloppy(programDir);
    const programCommands = [
        ["program-routes-json", ["routes", ".sloppy", "--format", "json"], true, true],
        ["program-routes-text", ["routes", ".sloppy"], false, true],
        ["program-capabilities-json", ["capabilities", ".sloppy", "--format", "json"], true, true],
        [v8VariantName("program-doctor-json"), ["doctor", ".sloppy", "--format", "json"], true, true],
        ["program-audit-json", ["audit", ".sloppy", "--format", "json"], true, true],
        ["program-openapi-failure", ["openapi", ".sloppy"], false, false],
    ];
    for (const [name, commandArgs, json, shouldSucceed] of programCommands) {
        const result = await run(sloppy, commandArgs, { cwd: programDir });
        if (shouldSucceed) {
            requireSuccess(result, name);
        } else {
            requireFailure(result, "OpenAPI is only available for web Plans", name);
        }
        await commandSnapshot("cli", name, result, { jsonStdout: json && result.exitCode === 0 });
    }
}

async function runCompilerArea() {
    await mkdirClean(joinPath(workRoot, "compiler"));
    for (const [name, source] of compilerCases) {
        if (selectedCompilerCase && selectedCompilerCase !== name) {
            continue;
        }
        const outDir = joinPath(workRoot, "compiler", name);
        await Directory.create(fsPath(outDir), { recursive: true });
        await buildSource(source, outDir);
        await snapshotJson("compiler", `${name}.plan`, await readJson(joinPath(outDir, "app.plan.json")));
        await snapshotJson("compiler", `${name}.source-map-summary`, sourceMapSummary(await readJson(joinPath(outDir, "app.js.map"))));
        await snapshotJson("compiler", `${name}.artifact-contract`, await artifactContract(outDir));
    }
}

async function runTemplateArea() {
    const areaRoot = joinPath(workRoot, "templates");
    await mkdirClean(areaRoot);
    await seedTemplates(areaRoot, selectedTemplate);
    for (const template of templates) {
        if (selectedTemplate && selectedTemplate !== template) {
            continue;
        }
        const projectName = `alpha-${template}`;
        const result = await run(sloppy, ["create", projectName, "--template", template, "--no-git", "--format", "json"], { cwd: areaRoot });
        requireSuccess(result, `create ${template}`);
        const projectDir = joinPath(areaRoot, projectName);
        await commandSnapshot("templates", `${template}.create`, result, { jsonStdout: true });
        await snapshotJson("templates", `${template}.file-tree`, await stableFileList(projectDir, { skip: [".sloppy", ".git"] }));
        await installTemplateDependencies(projectDir, template);
        await buildWithSloppy(projectDir);
        const artifactDir = joinPath(projectDir, ".sloppy");
        await snapshotJson("templates", `${template}.plan-summary`, planSummary(await readJson(joinPath(artifactDir, "app.plan.json"))));
        const packageResult = await packageWithSloppy(projectDir);
        await commandSnapshot("templates", `${template}.package-output`, packageResult, { jsonStdout: true });
        await snapshotJson("templates", `${template}.package-manifest`, await readJson(joinPath(projectDir, ".sloppy/package/manifest.json")));
        await proveOutsidePackageRun("templates", template, projectDir, template);

        if (template === "program" || template === "cli" || template === "node-compat") {
            const runArgs = template === "program" ? ["--name", "Ada"] : template === "cli" ? ["--help"] : [];
            const runResult = await run(sloppy, ["run", ".sloppy", ...withPassthroughArgs(runArgs)], { cwd: projectDir });
            if (requireV8) {
                requireSuccess(runResult, `${template} run`);
            } else {
                requireFailure(runResult, "requires V8-enabled build", `${template} non-V8 run`);
            }
            await commandSnapshot("templates", `${template}.run-${requireV8 ? "v8" : "non-v8"}`, runResult);
        } else {
            const routeResult = await run(sloppy, ["routes", ".sloppy", "--format", "json"], { cwd: projectDir });
            requireSuccess(routeResult, `${template} routes`);
            await commandSnapshot("templates", `${template}.routes`, routeResult, { jsonStdout: true });
            const openapiResult = await run(sloppy, ["openapi", ".sloppy"], { cwd: projectDir });
            requireSuccess(openapiResult, `${template} openapi`);
            await commandSnapshot("templates", `${template}.openapi`, openapiResult, { jsonStdout: true });
            if (template === "package-api") {
                const depsResult = await run(sloppy, ["deps", ".sloppy", "--format", "json"], { cwd: projectDir });
                requireSuccess(depsResult, `${template} deps`);
                await commandSnapshot("templates", `${template}.deps`, depsResult, { jsonStdout: true });
            }
        }
    }
}

async function runDiagnosticsArea() {
    const diagnosticsRoot = joinPath(workRoot, "diagnostics");
    await mkdirClean(diagnosticsRoot);
    for (const testCase of diagnosticCases) {
        let cwd = diagnosticsRoot;
        let result;
        if (testCase.fixtureProgram) {
            cwd = joinPath(diagnosticsRoot, testCase.name);
            await Directory.create(fsPath(joinPath(cwd, "src")), { recursive: true });
            await File.writeText(fsPath(joinPath(cwd, "sloppy.json")), '{ "kind": "program", "entry": "src/main.ts" }\n');
            await File.writeText(fsPath(joinPath(cwd, "src/main.ts")), "export function main() { return 0; }\n");
            await buildWithSloppy(cwd);
        }
        if (testCase.command === "build") {
            result = await run(sloppyc, ["build", joinPath(repoRoot, testCase.fixture), "--out", joinPath(diagnosticsRoot, `${testCase.name}-out`)]);
        } else {
            result = await run(sloppy, testCase.sloppyArgs, { cwd });
        }
        requireFailure(result, testCase.expect, testCase.name);
        await commandSnapshot("diagnostics", testCase.name, result);
    }
}

async function runAlphaFlowsArea() {
    const flowsRoot = joinPath(workRoot, "alpha-flows");
    await mkdirClean(flowsRoot);
    if (!selectedFlow || templates.includes(selectedFlow)) {
        await seedTemplates(flowsRoot, selectedFlow && templates.includes(selectedFlow) ? selectedFlow : "");
    }
    for (const template of templates) {
        if (selectedFlow && selectedFlow !== template) {
            continue;
        }
        const projectName = `flow-${template}`;
        const create = await run(sloppy, ["create", projectName, "--template", template, "--no-git", "--format", "json"], { cwd: flowsRoot });
        requireSuccess(create, `flow create ${template}`);
        await commandSnapshot("alpha-flows", `${template}.create`, create, { jsonStdout: true });
        const projectDir = joinPath(flowsRoot, projectName);
        await installTemplateDependencies(projectDir, template);
        await buildWithSloppy(projectDir);
        const artifactDir = joinPath(projectDir, ".sloppy");
        await snapshotJson("alpha-flows", `${template}.plan-summary`, planSummary(await readJson(joinPath(artifactDir, "app.plan.json"))));
        if (template === "program" || template === "cli" || template === "node-compat") {
            const runArgs = template === "program" ? ["--name", "Ada"] : template === "cli" ? ["--help"] : [];
            const runSource = await run(sloppy, ["run", "src/main.ts", ...withPassthroughArgs(runArgs)], { cwd: projectDir });
            if (requireV8) {
                requireSuccess(runSource, `${template} source flow`);
            } else {
                requireFailure(runSource, "requires V8-enabled build", `${template} non-V8 source flow`);
            }
            await commandSnapshot("alpha-flows", `${template}.run-source-${requireV8 ? "v8" : "non-v8"}`, runSource);
        } else {
            const routes = await run(sloppy, ["routes", ".sloppy", "--format", "json"], { cwd: projectDir });
            requireSuccess(routes, `${template} routes`);
            await commandSnapshot("alpha-flows", `${template}.routes`, routes, { jsonStdout: true });
            const openapi = await run(sloppy, ["openapi", ".sloppy"], { cwd: projectDir });
            requireSuccess(openapi, `${template} openapi`);
            await commandSnapshot("alpha-flows", `${template}.openapi`, openapi, { jsonStdout: true });
            if (template === "package-api") {
                const deps = await run(sloppy, ["deps", ".sloppy", "--format", "json"], { cwd: projectDir });
                requireSuccess(deps, `${template} deps`);
                await commandSnapshot("alpha-flows", `${template}.deps`, deps, { jsonStdout: true });
            }
        }
        const packaged = await packageWithSloppy(projectDir);
        await commandSnapshot("alpha-flows", `${template}.package`, packaged, { jsonStdout: true });
        await snapshotJson("alpha-flows", `${template}.package-manifest`, await readJson(joinPath(projectDir, ".sloppy/package/manifest.json")));
        await proveOutsidePackageRun("alpha-flows", template, projectDir, template);
    }

    const directProgram = joinPath(flowsRoot, "direct-program");
    if (!selectedFlow || selectedFlow === "direct-program") {
        await Directory.create(fsPath(directProgram), { recursive: true });
        await File.writeText(fsPath(joinPath(directProgram, "main.ts")), 'export function main(args) { console.log(`args=${args.join("|")}`); return 0; }\n');
        const directProgramRun = await run(sloppy, ["run", "main.ts", "--", "one", "two"], { cwd: directProgram });
        if (requireV8) {
            requireSuccess(directProgramRun, "direct program run");
        } else {
            requireFailure(directProgramRun, "requires V8-enabled build", "direct program non-V8 run");
        }
        await commandSnapshot("alpha-flows", `direct-program.run-${requireV8 ? "v8" : "non-v8"}`, directProgramRun);
        await buildWithSloppy(directProgram, ["main.ts"]);
        await packageWithSloppy(directProgram, ["main.ts"]);
    }

    const directWeb = joinPath(flowsRoot, "direct-web");
    if (!selectedFlow || selectedFlow === "direct-web") {
        await Directory.create(fsPath(directWeb), { recursive: true });
        await File.writeText(fsPath(joinPath(directWeb, "main.ts")), 'import { Sloppy, Results } from "sloppy";\nconst app = Sloppy.create();\napp.get("/health", () => Results.text("ok"));\nexport default app;\n');
        await buildWithSloppy(directWeb, ["main.ts", "--kind", "web"]);
        const webOpenapi = await run(sloppy, ["openapi", ".sloppy"], { cwd: directWeb });
        requireSuccess(webOpenapi, "direct web openapi");
        await commandSnapshot("alpha-flows", "direct-web.openapi", webOpenapi, { jsonStdout: true });
    }
}

async function runExamplesArea() {
    const manifestPath = joinPath(repoRoot, "tests/golden/examples/examples.manifest.json");
    const manifest = await readJson(manifestPath);
    const actual = [];
    for (const entry of await Directory.list(fsPath(joinPath(repoRoot, "examples")))) {
        if (entry.kind === "directory") {
            actual.push(entry.name);
        }
    }
    actual.sort();
    const declared = manifest.examples.map((entry) => entry.name).sort();
    if (JSON.stringify(actual) !== JSON.stringify(declared)) {
        throw new Error(`examples manifest is stale\nactual=${actual.join(",")}\ndeclared=${declared.join(",")}`);
    }
    await snapshotJson("examples", "classification", manifest);
    if (selectedExample === "classification") {
        return;
    }

    const examplesRoot = joinPath(workRoot, "examples");
    await mkdirClean(examplesRoot);
    const smoke = manifest.examples.filter((entry) => entry.prSmoke === true);
    for (const entry of smoke) {
        if (selectedExample && selectedExample !== entry.name) {
            continue;
        }
        const sourceDir = joinPath(repoRoot, "examples", entry.name);
        const workDir = joinPath(examplesRoot, entry.name);
        await copyTree(sourceDir, workDir);
        const buildArgs = entry.entry ? [entry.entry] : [];
        const build = await run(sloppy, ["build", ...buildArgs], { cwd: workDir });
        requireSuccess(build, `example build ${entry.name}`);
        await snapshotJson("examples", `${entry.name}.plan-summary`, planSummary(await readJson(joinPath(workDir, ".sloppy/app.plan.json"))));
        if (entry.metadata !== false && entry.kind !== "program") {
            const doctor = await run(sloppy, ["doctor", ".sloppy", "--format", "json"], { cwd: workDir });
            requireSuccess(doctor, `example doctor ${entry.name}`);
            await commandSnapshot("examples", v8VariantName(`${entry.name}.doctor`), doctor, { jsonStdout: true });
        }
        if (entry.package === true) {
            const packaged = await packageWithSloppy(workDir, buildArgs);
            await commandSnapshot("examples", `${entry.name}.package`, packaged, { jsonStdout: true });
        }
    }
}

async function runDocsSnippetsArea() {
    const manifestPath = joinPath(repoRoot, "tests/fixtures/docs-snippets/manifest.json");
    const manifest = await readJson(manifestPath);
    if (manifest.schemaVersion !== 1 || !Array.isArray(manifest.documents)) {
        throw new Error("docs snippets manifest must use schemaVersion 1 and a documents array");
    }
    if (typeof manifest.defaultProofLane !== "string" || manifest.defaultProofLane.length === 0) {
        throw new Error("docs snippets manifest must name a default proof lane");
    }
    if (manifest.semantics?.checked === undefined || manifest.semantics?.executed === undefined || manifest.semantics?.skipped === undefined) {
        throw new Error("docs snippets manifest must define checked, executed, and skipped semantics");
    }

    const summary = [];
    for (const document of manifest.documents) {
        if (typeof document.path !== "string" || document.path.length === 0) {
            throw new Error("docs snippets manifest document path must be a non-empty string");
        }
        const proofLane = document.proofLane ?? manifest.defaultProofLane;
        if (typeof proofLane !== "string" || proofLane.length === 0) {
            throw new Error(`${document.path} must name a proof lane for checked snippets`);
        }
        const docPath = joinPath(repoRoot, document.path);
        if (!(await File.exists(fsPath(docPath)))) {
            throw new Error(`docs snippet source is missing: ${document.path}`);
        }
        const text = await File.readText(fsPath(docPath));
        const checked = [];
        const skipped = [];
        for (const snippet of document.snippets || []) {
            if (typeof snippet.command !== "string" || snippet.command.length === 0) {
                throw new Error(`docs snippet command is invalid in ${document.path}`);
            }
            if (!text.includes(snippet.command)) {
                throw new Error(`${document.path} is missing documented command: ${snippet.command}`);
            }
            if (snippet.template !== undefined && !templates.includes(snippet.template)) {
                throw new Error(`${document.path} references unsupported template '${snippet.template}'`);
            }
            if (snippet.status === "skipped") {
                if (typeof snippet.reason !== "string" || snippet.reason.length === 0) {
                    throw new Error(`${document.path} skipped command needs a reason: ${snippet.command}`);
                }
                skipped.push({ command: snippet.command, reason: snippet.reason });
            } else if (snippet.status === "executed") {
                throw new Error(`${document.path} marks a command executed, but docs-snippet proof does not execute manifest commands: ${snippet.command}`);
            } else if (snippet.status === "checked") {
                checked.push({ command: snippet.command, proofLane });
            } else {
                throw new Error(`${document.path} snippet status must be checked, executed, or skipped`);
            }
        }
        summary.push({
            path: document.path,
            proofLane,
            checked: checked.length,
            executed: 0,
            skipped: skipped.length,
            skippedReasons: skipped.map((entry) => entry.reason),
        });
    }
    await snapshotJson("docs-snippets", "summary", {
        coverage: manifest.coverage,
        semantics: manifest.semantics,
        documents: summary,
    });
}

export async function main(args, ctx) {
    const parsed = parseArgs(args);
    repoRoot = resolvePath(ctx.cwd, parsed.root || ctx.cwd);
    sloppy = resolvePath(ctx.cwd, parsed.sloppy || joinPath(repoRoot, "build/windows-dev/sloppy.exe"));
    sloppyc = resolvePath(ctx.cwd, parsed.sloppyc || joinPath(repoRoot, "compiler/target/release/sloppyc.exe"));
    node = parsed.node ? resolvePath(ctx.cwd, parsed.node) : "";
    npmCli = parsed["npm-cli"] ? resolvePath(ctx.cwd, parsed["npm-cli"]) : "";
    update = Boolean(parsed.update);
    requireV8 = Boolean(parsed.requireV8);
    selectedArea = parsed.area || "all";
    workRoot = resolvePath(ctx.cwd, parsed["work-root"] || joinPath(repoRoot, "artifacts/alpha-proof/work"));
    goldenRoot = joinPath(repoRoot, "tests/golden/alpha");
    tempRoot = System.tempDirectory;
    selectedCliSection = parsed["cli-section"] || "";
    selectedCompilerCase = parsed["compiler-case"] || "";
    selectedTemplate = parsed.template || "";
    selectedFlow = parsed.flow || "";
    selectedExample = parsed.example || "";

    if (!(await File.exists(fsPath(sloppy)))) {
        throw new Error(`sloppy executable not found: ${sloppy}`);
    }
    if (!(await File.exists(fsPath(sloppyc)))) {
        throw new Error(`sloppyc executable not found: ${sloppyc}`);
    }
    if ((node && !npmCli) || (!node && npmCli)) {
        throw new Error("--node and --npm-cli must be provided together");
    }
    if (node && !(await File.exists(fsPath(node)))) {
        throw new Error(`node executable not found: ${node}`);
    }
    if (npmCli && !(await File.exists(fsPath(npmCli)))) {
        throw new Error(`npm CLI not found: ${npmCli}`);
    }
    await withFsContext(`create work root ${workRoot}`, async () => {
        await Directory.create(fsPath(workRoot), { recursive: true });
    });

    if (shouldRun("cli")) await runCliArea();
    if (shouldRun("compiler")) await runCompilerArea();
    if (shouldRun("templates")) await runTemplateArea();
    if (shouldRun("diagnostics")) await runDiagnosticsArea();
    if (shouldRun("alpha-flows")) await runAlphaFlowsArea();
    if (shouldRun("examples")) await runExamplesArea();
    if (shouldRun("docs-snippets")) await runDocsSnippetsArea();

    return 0;
}
