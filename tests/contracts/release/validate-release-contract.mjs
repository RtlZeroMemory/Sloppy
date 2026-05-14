import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { exists, isAbsolutePackagePath, isSafeRelativePackagePath, listFiles, packagePath, readJson } from "../runner/artifact-utils.mjs";
import { loadFixtures } from "../runner/fixture-loader.mjs";

const SUBSYSTEM = "release";
const ROOT_PACKAGE_NAME = "@slopware/sloppy";
const PACKAGE_SCOPE = "@slopware/";
const SEMVER_ALPHA = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)-alpha\.(0|[1-9]\d*)$/u;
const EXPECTED_PLATFORM_PACKAGES = new Map([
    ["@slopware/sloppy-win32-x64", { os: "win32", cpu: "x64", binary: "bin/sloppy.exe" }],
    ["@slopware/sloppy-linux-x64", { os: "linux", cpu: "x64", binary: "bin/sloppy" }],
    ["@slopware/sloppy-darwin-arm64", { os: "darwin", cpu: "arm64", binary: "bin/sloppy" }],
    ["@slopware/sloppy-darwin-x64", { os: "darwin", cpu: "x64", binary: "bin/sloppy" }],
]);
const SECRET_FILE_NAMES = new Set([".env", ".npmrc", ".yarnrc", ".pnpmrc"]);
const JUNK_PATTERNS = [
    /^node_modules\//u,
    /^\.git\//u,
    /^build\//u,
    /^target\//u,
    /^compiler\/target\//u,
    /^v8(?:[-_/]|$)/iu,
    /(?:^|\/)v8(?:\/|$).*(?:src|source)/iu,
    /(?:^|\/)(?:out|obj)\/.*v8/iu,
    /(?:^|\/).*\.(?:log|binlog)$/iu,
];
const TOKEN_FILE_PATTERN = /(?:token|secret|password|passwd|credential|apikey|api-key)/iu;
const ABSOLUTE_PATH_PATTERN = /(?:[A-Za-z]:\\|\/home\/|\/Users\/|\/tmp\/|\\\\[^\\]+\\)/u;
const MAX_REASONABLE_PACKAGE_FILE_BYTES = 25 * 1024 * 1024;

async function readPackageJson(root, collector) {
    const packageJsonPath = path.join(root, "package.json");
    if (!(await exists(packageJsonPath))) {
        collector.fail("npm.package-json.exists", "package.json must exist", { root });
        return undefined;
    }
    try {
        return await readJson(packageJsonPath);
    } catch (error) {
        collector.fail("npm.package-json.parses", "package.json must parse as JSON", {
            path: packageJsonPath,
            error: error.message,
        });
        return undefined;
    }
}

async function discoverPackageRoots(root) {
    if (await exists(path.join(root, "package.json"))) {
        return [root];
    }
    const entries = await fs.readdir(root, { withFileTypes: true });
    return entries
        .filter((entry) => entry.isDirectory())
        .map((entry) => path.join(root, entry.name))
        .sort((left, right) => left.localeCompare(right));
}

async function loadPackageSet(root, collector) {
    const packages = new Map();
    for (const packageRoot of await discoverPackageRoots(root)) {
        const packageJson = await readPackageJson(packageRoot, collector);
        if (packageJson === undefined) {
            continue;
        }
        if (typeof packageJson.name !== "string") {
            collector.fail("npm.name.scope", "package name must be a string", { packageRoot });
            continue;
        }
        packages.set(packageJson.name, { root: packageRoot, packageJson });
    }
    return packages;
}

function checkNameAndVersion(packages, collector) {
    for (const [name, { packageJson }] of packages) {
        if (name.startsWith(PACKAGE_SCOPE)) {
            collector.pass("npm.name.scope", "package name uses @slopware scope", { name });
        } else {
            collector.fail("npm.name.scope", "package name must use @slopware scope", { name });
        }
        if (typeof packageJson.version === "string" && SEMVER_ALPHA.test(packageJson.version) && packageJson.version !== "0.0.0") {
            collector.pass("npm.version.semver-alpha", "package version is a real alpha semver", {
                name,
                version: packageJson.version,
            });
        } else {
            collector.fail("npm.version.semver-alpha", "package version must be a non-zero semver alpha", {
                name,
                version: packageJson.version,
            });
        }
    }
}

async function checkRelativeFile(root, relativePath, invariant, collector, message) {
    if (typeof relativePath !== "string" || relativePath.length === 0) {
        collector.fail(invariant, `${message} must be a non-empty package-relative path`, { path: relativePath });
        return false;
    }
    if (isAbsolutePackagePath(relativePath)) {
        collector.fail(invariant, `${message} must be a package-relative path`, { path: relativePath });
        return false;
    }
    if (!isSafeRelativePackagePath(relativePath)) {
        collector.fail(invariant, `${message} must not escape the package root`, { path: relativePath });
        return false;
    }
    const resolved = packagePath(root, relativePath);
    if (await exists(resolved)) {
        collector.pass(invariant, message, { path: relativePath });
        return true;
    }
    collector.fail(invariant, `${message} must exist`, { path: relativePath });
    return false;
}

async function checkRootPackage(rootPackage, packages, collector) {
    if (rootPackage === undefined) {
        collector.fail("npm.name.scope", "root @slopware/sloppy package must exist");
        return;
    }
    const { root, packageJson } = rootPackage;
    if (packageJson.bin === null || typeof packageJson.bin !== "object" || Array.isArray(packageJson.bin)) {
        collector.fail("npm.bin.exists", "root package must declare a bin map");
    } else {
        for (const [binName, binPath] of Object.entries(packageJson.bin)) {
            await checkRelativeFile(root, binPath, "npm.bin.exists", collector, `bin '${binName}' target`);
        }
    }

    if (typeof packageJson.types === "string") {
        await checkRelativeFile(root, packageJson.types, "npm.exports.types-exists", collector, "root types path");
    }

    await checkExports(root, packageJson, collector);
    await checkTypesVersions(root, packageJson, collector);
    await checkOptionalPlatforms(rootPackage, packages, collector);
    await checkLifecycleScripts(packages, collector);
}

async function checkExports(root, packageJson, collector) {
    if (packageJson.exports === undefined) {
        collector.pass("npm.exports.js-exists", "package has no explicit JS exports to validate");
        collector.pass("npm.exports.types-exists", "package has no explicit export-specific types to validate");
        return;
    }
    const exportEntries = flattenExports(packageJson.exports);
    for (const entry of exportEntries) {
        if (entry.js !== undefined) {
            await checkRelativeFile(root, entry.js, "npm.exports.js-exists", collector, `export '${entry.name}' JS target`);
        }
        if (entry.types !== undefined) {
            await checkRelativeFile(root, entry.types, "npm.exports.types-exists", collector, `export '${entry.name}' types target`);
        } else if (entry.js !== undefined) {
            collector.fail("npm.exports.types-exists", "public JS export must declare a types target", {
                export: entry.name,
                js: entry.js,
            });
        }
    }
}

function flattenExports(exportsValue) {
    if (typeof exportsValue === "string") {
        return [{ name: ".", js: exportsValue }];
    }
    if (exportsValue === null || typeof exportsValue !== "object" || Array.isArray(exportsValue)) {
        return [];
    }
    return Object.entries(exportsValue).map(([name, value]) => {
        if (typeof value === "string") {
            return { name, js: value };
        }
        if (value !== null && typeof value === "object" && !Array.isArray(value)) {
            return {
                name,
                js: value.import ?? value.require ?? value.default,
                types: value.types,
            };
        }
        return { name };
    });
}

async function checkTypesVersions(root, packageJson, collector) {
    const typesVersions = packageJson.typesVersions;
    if (typesVersions === undefined) {
        collector.pass("npm.exports.types-exists", "package has no typesVersions subpaths to validate");
        return;
    }
    for (const versionMap of Object.values(typesVersions)) {
        if (versionMap === null || typeof versionMap !== "object" || Array.isArray(versionMap)) {
            collector.fail("npm.exports.types-exists", "typesVersions entries must be objects");
            continue;
        }
        for (const [subpath, targets] of Object.entries(versionMap)) {
            for (const target of Array.isArray(targets) ? targets : []) {
                await checkRelativeFile(root, target, "npm.exports.types-exists", collector, `typesVersions '${subpath}' target`);
            }
        }
    }
}

async function checkOptionalPlatforms(rootPackage, packages, collector) {
    const optionalDependencies = rootPackage.packageJson.optionalDependencies;
    if (optionalDependencies === null || typeof optionalDependencies !== "object" || Array.isArray(optionalDependencies)) {
        collector.fail("npm.optional-platforms", "root package must declare platform optionalDependencies");
        return;
    }
    const optionalNames = Object.keys(optionalDependencies).sort();
    const expectedNames = [...EXPECTED_PLATFORM_PACKAGES.keys()].filter((name) => packages.has(name)).sort();
    for (const name of expectedNames) {
        if (optionalDependencies[name] === rootPackage.packageJson.version) {
            collector.pass("npm.optional-platforms", "optional platform dependency matches root version", { name });
        } else {
            collector.fail("npm.optional-platforms", "optional platform dependency must match root version", {
                name,
                expected: rootPackage.packageJson.version,
                actual: optionalDependencies[name],
            });
        }
    }
    for (const name of optionalNames) {
        if (!EXPECTED_PLATFORM_PACKAGES.has(name)) {
            collector.fail("npm.optional-platforms", "optional dependency must be an expected platform package", { name });
        }
    }
}

async function checkPlatformPackages(packages, rootVersion, collector, { requirePlatformBinary }) {
    for (const [name, expectation] of EXPECTED_PLATFORM_PACKAGES) {
        const entry = packages.get(name);
        if (entry === undefined) {
            continue;
        }
        const { root, packageJson } = entry;
        if (Array.isArray(packageJson.os) && packageJson.os.includes(expectation.os) && Array.isArray(packageJson.cpu) && packageJson.cpu.includes(expectation.cpu)) {
            collector.pass("npm.optional-platforms", "platform package declares expected os/cpu", { name });
        } else {
            collector.fail("npm.optional-platforms", "platform package must declare expected os/cpu", {
                name,
                expected: { os: expectation.os, cpu: expectation.cpu },
                actual: { os: packageJson.os, cpu: packageJson.cpu },
            });
        }
        if (packageJson.version === rootVersion) {
            collector.pass("npm.version.consistency", "platform package version matches root package", { name });
        } else {
            collector.fail("npm.version.consistency", "platform package version must match root package", {
                name,
                expected: rootVersion,
                actual: packageJson.version,
            });
        }
        const platformBinary = packageJson.bin?.sloppy ?? expectation.binary;
        if (requirePlatformBinary || (await exists(path.join(root, "bin"))) || packageJson.bin?.sloppy !== undefined) {
            await checkRelativeFile(root, platformBinary, "npm.platform.binary-exists", collector, "platform package binary");
        } else {
            collector.unavailable("npm.platform.binary-exists", "platform package binary is validated only for staged package roots", {
                name,
            });
        }
        await checkRelativeFile(root, "README.md", "npm.package.readme-license", collector, "platform package README");
    }
}

async function checkLifecycleScripts(packages, collector) {
    for (const [name, { packageJson }] of packages) {
        for (const [scriptName, scriptValue] of Object.entries(packageJson.scripts ?? {})) {
            if (/^(preinstall|install|postinstall|prepare)$/u.test(scriptName)) {
                collector.fail("npm.install.smoke", "npm package must not use install lifecycle scripts", { name, scriptName });
            }
            if (/node-gyp|cmake|cargo|vcpkg|fetch-v8|build-v8|postinstall/iu.test(String(scriptValue))) {
                collector.fail("npm.tarball.no-sdk-source", "npm package scripts must not build or fetch V8/native SDKs", {
                    name,
                    scriptName,
                    scriptValue,
                });
            }
        }
    }
}

async function checkTarballSafety(root, collector) {
    const files = await listFiles(root);
    const secretFiles = [];
    const sdkFiles = [];
    const localPaths = [];
    const hugeFiles = [];
    for (const file of files) {
        const leaf = path.posix.basename(file);
        const fullPath = packagePath(root, file);
        if (SECRET_FILE_NAMES.has(leaf) || TOKEN_FILE_PATTERN.test(leaf)) {
            secretFiles.push(file);
        }
        if (JUNK_PATTERNS.some((pattern) => pattern.test(file))) {
            sdkFiles.push(file);
        }
        const stat = await fs.stat(fullPath);
        if (stat.size > MAX_REASONABLE_PACKAGE_FILE_BYTES) {
            hugeFiles.push({ file, size: stat.size });
        }
        if (/\.(?:json|js|mjs|cjs|ts|d\.ts|md|txt|ps1|sh|yml|yaml)$/iu.test(file)) {
            const text = await fs.readFile(fullPath, "utf8");
            if (ABSOLUTE_PATH_PATTERN.test(text)) {
                localPaths.push(file);
            }
        }
    }
    if (secretFiles.length === 0) {
        collector.pass("npm.tarball.no-secrets", "package file list has no secret-looking files");
    } else {
        collector.fail("npm.tarball.no-secrets", "package file list must not include secret-looking files", { files: secretFiles });
    }
    if (localPaths.length === 0) {
        collector.pass("npm.tarball.no-local-paths", "package text files contain no local absolute paths");
    } else {
        collector.fail("npm.tarball.no-local-paths", "package text files must not contain local absolute paths", { files: localPaths });
    }
    if (sdkFiles.length === 0 && hugeFiles.length === 0) {
        collector.pass("npm.tarball.no-sdk-source", "package file list has no SDK/source checkout junk or huge accidental files");
    } else {
        collector.fail("npm.tarball.no-sdk-source", "package file list must not include SDK/source checkout junk or huge files", {
            files: sdkFiles,
            hugeFiles,
        });
    }
}

async function checkReadmeLicense(rootPackage, collector) {
    await checkRelativeFile(rootPackage.root, "README.md", "npm.package.readme-license", collector, "root package README");
    await checkRelativeFile(rootPackage.root, "LICENSE", "npm.package.readme-license", collector, "root package license");
}

function runTsc(command, args) {
    return spawnSync(command.executable, [...command.args, ...args], { encoding: "utf8" });
}

async function resolveTscCommand() {
    if (process.platform === "win32") {
        const where = spawnSync("where.exe", ["tsc.cmd"], { encoding: "utf8" });
        const tscCmd = where.status === 0 ? where.stdout.split(/\r?\n/u).find((line) => line.trim().length > 0) : undefined;
        if (tscCmd !== undefined) {
            const npmRoot = path.dirname(tscCmd.trim());
            const tscBin = path.join(npmRoot, "node_modules/typescript/bin/tsc");
            if (await exists(tscBin)) {
                const command = { executable: "node", args: [tscBin] };
                if (runTsc(command, ["--version"]).status === 0) {
                    return command;
                }
            }
        }
    }

    const command = { executable: "tsc", args: [] };
    if (runTsc(command, ["--version"]).status === 0) {
        return command;
    }
    return undefined;
}

async function runTscSmoke(rootPackage, collector) {
    const tscCommand = await resolveTscCommand();
    if (tscCommand === undefined) {
        collector.unavailable("npm.types.smoke", "TypeScript is not available for import smoke");
        return;
    }
    if (typeof rootPackage.packageJson.types !== "string") {
        collector.fail("npm.types.smoke", "TypeScript import smoke requires a root types declaration");
        return;
    }

    const smokeRoot = await fs.mkdtemp(path.join(os.tmpdir(), "sloppy-npm-types-smoke-"));
    try {
        const typeMappings = {
            sloppy: [path.resolve(rootPackage.root, rootPackage.packageJson.types)],
        };
        const typesVersions = rootPackage.packageJson.typesVersions?.["*"] ?? {};
        for (const [subpath, targets] of Object.entries(typesVersions)) {
            if (Array.isArray(targets) && typeof targets[0] === "string") {
                typeMappings[`sloppy/${subpath}`] = [path.resolve(rootPackage.root, targets[0])];
            }
        }
        const tsconfig = {
            compilerOptions: {
                target: "ES2022",
                module: "ES2022",
                moduleResolution: "node",
                strict: true,
                noEmit: true,
                baseUrl: ".",
                paths: typeMappings,
            },
            include: ["sample.ts"],
        };
        const sample = `import { Sloppy, Results } from "sloppy";
import { data } from "sloppy/data";
import { File } from "sloppy/fs";
import { Environment } from "sloppy/os";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
void data;
void File;
void Environment;
void sqlite;
`;
        await fs.writeFile(path.join(smokeRoot, "tsconfig.json"), `${JSON.stringify(tsconfig, null, 2)}\n`, "utf8");
        await fs.writeFile(path.join(smokeRoot, "sample.ts"), sample, "utf8");
        const result = runTsc(tscCommand, ["--noEmit", "--project", path.join(smokeRoot, "tsconfig.json")]);
        if (result.status === 0) {
            collector.pass("npm.types.smoke", "TypeScript import smoke compiled public package imports");
        } else {
            collector.fail("npm.types.smoke", "TypeScript import smoke must compile public package imports", {
                stdout: result.stdout,
                stderr: result.stderr,
            });
        }
    } finally {
        await fs.rm(smokeRoot, { recursive: true, force: true });
    }
}

export async function validateReleasePackageSet({ root, fixture, requirePlatformBinary = false, runTypeSmoke = false }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    const packages = await loadPackageSet(root, collector);
    if (packages.size === 0) {
        collector.fail("npm.package-json.exists", "release package set must include package.json files", { root });
        return collector.findings;
    }
    checkNameAndVersion(packages, collector);
    const rootPackage = packages.get(ROOT_PACKAGE_NAME);
    await checkRootPackage(rootPackage, packages, collector);
    if (rootPackage !== undefined) {
        await checkReadmeLicense(rootPackage, collector);
        await checkPlatformPackages(packages, rootPackage.packageJson.version, collector, { requirePlatformBinary });
        collector.pass("npm.version.consistency", "root package version is the package set source of truth", {
            version: rootPackage.packageJson.version,
        });
        if (runTypeSmoke) {
            await runTscSmoke(rootPackage, collector);
        } else {
            collector.unavailable("npm.types.smoke", "TypeScript smoke runs only for local tarball or explicit staged-package validation");
        }
    }
    await checkTarballSafety(root, collector);
    collector.unavailable("npm.install.smoke", "install smoke runs only when local npm tarballs are available");
    return collector.findings;
}

function detectedErrorDetails(rawFindings) {
    return rawFindings
        .filter((finding) => finding.status === "fail" && finding.severity === "error")
        .map((finding) => ({
            invariant: finding.invariant,
            message: finding.message,
            path: finding.path,
            details: finding.details,
        }));
}

function expectedFailureFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture produced expected ${invariant} finding`,
        details: { detected, detectedFindings: detectedErrorDetails(rawFindings) },
    });
}

function failedExpectationFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.missing`,
        status: "fail",
        severity: "error",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture did not produce expected ${invariant} finding`,
        details: { detected, detectedFindings: detectedErrorDetails(rawFindings) },
    });
}

function unexpectedErrorFinding(fixture, invariant, expectedInvariants, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.unexpected`,
        status: "pass",
        severity: "warning",
        subsystem: SUBSYSTEM,
        invariant: `negative.unexpected.${invariant}`,
        fixture,
        message: `broken fixture produced unexpected ${invariant} finding`,
        details: { expectedInvariants, detectedFindings: detectedErrorDetails(rawFindings) },
    });
}

async function collectFixtureFindings(repoRoot) {
    const fixtures = await loadFixtures(path.join(repoRoot, "tests/contracts/release/fixtures"));
    const findings = [];
    for (const fixture of fixtures) {
        const rawFindings = await validateReleasePackageSet({
            root: fixture.root,
            fixture: fixture.name,
            requirePlatformBinary: true,
            runTypeSmoke: false,
        });
        const detected = errorInvariants(rawFindings);
        if (fixture.expected === "fail") {
            const expectedInvariants = new Set(fixture.expectedInvariants);
            for (const invariant of fixture.expectedInvariants) {
                findings.push(
                    detected.includes(invariant)
                        ? expectedFailureFinding(fixture.name, invariant, detected, rawFindings)
                        : failedExpectationFinding(fixture.name, invariant, detected, rawFindings),
                );
            }
            for (const invariant of detected) {
                if (!expectedInvariants.has(invariant)) {
                    findings.push(unexpectedErrorFinding(fixture.name, invariant, fixture.expectedInvariants, rawFindings));
                }
            }
            continue;
        }
        findings.push(...rawFindings);
    }
    return findings;
}

export async function runReleaseContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const findings = await collectFixtureFindings(repoRoot);
    const packageSkeletonRoot = path.join(repoRoot, "packages/npm");
    if (await exists(packageSkeletonRoot)) {
        findings.push(
            ...(await validateReleasePackageSet({
                root: packageSkeletonRoot,
                fixture: "packages-npm-skeleton",
                requirePlatformBinary: false,
                runTypeSmoke: false,
            })),
        );
    }
    const stagedRoot = path.join(repoRoot, "artifacts/npm/stage");
    if (await exists(stagedRoot)) {
        findings.push(
            ...(await validateReleasePackageSet({
                root: stagedRoot,
                fixture: "artifacts-npm-stage",
                requirePlatformBinary: true,
                runTypeSmoke: true,
            })),
        );
    } else {
        findings.push(
            createFinding({
                id: "release.artifacts-npm-stage.npm.install.smoke.unavailable",
                status: "unavailable",
                severity: "info",
                subsystem: SUBSYSTEM,
                invariant: "npm.install.smoke",
                fixture: "artifacts-npm-stage",
                message: "no staged npm package directory is available under artifacts/npm/stage",
            }),
        );
    }

    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
