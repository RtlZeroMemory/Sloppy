import fs from "node:fs/promises";
import path from "node:path";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import {
    exists,
    isAbsolutePackagePath,
    isSafeRelativePackagePath,
    listFiles,
    packagePath,
    readJson,
    sha256File,
    toPackagePath,
} from "../runner/artifact-utils.mjs";
import { createReport, createFinding } from "../runner/contract-report.mjs";
import { loadFixtures } from "../runner/fixture-loader.mjs";

const SUBSYSTEM = "package";
const MANIFEST_SCHEMA = "sloppy.app-package.v1";
const JUNK_FILE_NAMES = new Set([".env", ".npmrc", ".yarnrc", ".pnpmrc", "package-lock.json"]);
const JUNK_PATH_PATTERNS = [/^node_modules\//u, /^\.git\//u, /^build\//u, /^target\//u, /^compiler\/target\//u];
const TOKEN_FILE_PATTERN = /(?:token|secret|password|passwd|credential|apikey|api-key)/iu;

function readString(value, name, collector) {
    if (typeof value === "string" && value.length > 0) {
        collector.pass(`${name}.present`, `${name} is present`);
        return value;
    }
    collector.fail(`${name}.present`, `${name} must be a non-empty string`);
    return undefined;
}

async function readJsonArtifact(filePath, invariant, collector) {
    try {
        const value = await readJson(filePath);
        collector.pass(`${invariant}.parses`, `${invariant} parses as JSON`);
        return value;
    } catch (error) {
        collector.fail(`${invariant}.parses`, `${invariant} must parse as JSON`, {
            path: filePath,
            error: error.message,
        });
        return undefined;
    }
}

async function checkPackagePath(root, relativePath, invariant, collector, { required = true } = {}) {
    if (typeof relativePath !== "string" || relativePath.length === 0) {
        collector.fail(`${invariant}.path`, `${invariant} must be a non-empty package-relative path`);
        return undefined;
    }
    if (isAbsolutePackagePath(relativePath)) {
        collector.fail(`${invariant}.relative`, `${invariant} must not be absolute`, { path: relativePath });
        return undefined;
    }
    if (!isSafeRelativePackagePath(relativePath)) {
        collector.fail(`${invariant}.safe`, `${invariant} must not escape the package root`, { path: relativePath });
        return undefined;
    }
    collector.pass(`${invariant}.relative`, `${invariant} uses a package-relative path`, { path: relativePath });
    const resolved = packagePath(root, relativePath);
    if (await exists(resolved)) {
        collector.pass(`${invariant}.exists`, `${invariant} exists in the package`, { path: relativePath });
    } else if (required) {
        collector.fail(`${invariant}.exists`, `${invariant} points to a missing package artifact`, {
            path: relativePath,
        });
    }
    return resolved;
}

async function checkHash(filePath, expectedHash, invariant, collector) {
    if (typeof expectedHash !== "string" || expectedHash.length === 0) {
        collector.fail(`${invariant}.hash-present`, `${invariant} hash must be present`);
        return;
    }
    const actualHash = await sha256File(filePath);
    if (actualHash === expectedHash) {
        collector.pass(`${invariant}.hash-agreement`, `${invariant} hash matches artifact bytes`);
    } else {
        collector.fail(`${invariant}.hash-agreement`, `${invariant} hash does not match artifact bytes`, {
            expected: expectedHash,
            actual: actualHash,
        });
    }
}

function pathLooksRelocatable(value) {
    return typeof value !== "string" || !isAbsolutePackagePath(value);
}

function inspectRelocatableJson(value, collector, pathStack = []) {
    if (typeof value === "string") {
        const key = pathStack.at(-1) ?? "";
        const pathLike = /^(path|root|dir|directory|cwd)$/iu.test(key);
        if (pathLike && !pathLooksRelocatable(value)) {
            collector.fail("artifact.relocatable-paths", "package artifacts must not contain source-checkout absolute paths", {
                jsonPath: pathStack.join("."),
                value,
            });
        }
        return;
    }
    if (Array.isArray(value)) {
        value.forEach((item, index) => inspectRelocatableJson(item, collector, [...pathStack, String(index)]));
        return;
    }
    if (value !== null && typeof value === "object") {
        for (const [key, entry] of Object.entries(value)) {
            inspectRelocatableJson(entry, collector, [...pathStack, key]);
        }
    }
}

function planReferencesRouteDispatch(plan) {
    return (
        plan !== undefined &&
        plan.routeDispatch !== null &&
        typeof plan.routeDispatch === "object" &&
        plan.routeDispatch.artifact !== null &&
        typeof plan.routeDispatch.artifact === "object" &&
        typeof plan.routeDispatch.artifact.path === "string"
    );
}

async function checkMigrations(root, manifest, collector) {
    const migrations = manifest.migrations;
    if (migrations === undefined) {
        collector.pass("manifest.migrations.optional", "manifest has no migration list");
        return;
    }
    if (!Array.isArray(migrations)) {
        collector.fail("manifest.migrations.shape", "manifest.migrations must be an array when present");
        return;
    }
    collector.pass("manifest.migrations.shape", "manifest.migrations is an array");
    for (const [migrationIndex, migration] of migrations.entries()) {
        if (migration === null || typeof migration !== "object" || !Array.isArray(migration.files)) {
            collector.fail("manifest.migrations.files", "each manifest migration entry must list files", {
                index: migrationIndex,
            });
            continue;
        }
        for (const file of migration.files) {
            await checkPackagePath(root, file.path, "manifest.migrations.file", collector);
            const filePath = packagePath(root, file.path);
            if ((await exists(filePath)) && typeof file.sha256 === "string") {
                await checkHash(filePath, file.sha256, "manifest.migrations.file", collector);
            }
        }
    }
}

async function checkNativeLibraries(root, manifest, collector) {
    const libraries = manifest.native?.libraries;
    if (libraries === undefined) {
        collector.pass("manifest.native.optional", "manifest has no native library list");
        return;
    }
    if (!Array.isArray(libraries)) {
        collector.fail("manifest.native.libraries.shape", "manifest.native.libraries must be an array");
        return;
    }
    collector.pass("manifest.native.libraries.shape", "manifest.native.libraries is an array");
    for (const library of libraries) {
        const filePath = await checkPackagePath(root, library?.path, "manifest.native.library", collector);
        if (filePath !== undefined && (await exists(filePath)) && typeof library.sha256 === "string") {
            await checkHash(filePath, library.sha256, "manifest.native.library", collector);
        }
    }
}

async function checkSQLitePlanPaths(root, plan, collector) {
    const providers = Array.isArray(plan?.dataProviders) ? plan.dataProviders : [];
    for (const provider of providers) {
        if (provider?.provider !== "sqlite" && provider?.providerKind !== "sqlite") {
            continue;
        }
        const database = provider.database;
        if (database === ":memory:") {
            collector.pass("plan.sqlite.database.safe", "SQLite :memory: database path is safe");
            continue;
        }
        if (typeof database !== "string" || database.length === 0) {
            collector.fail("plan.sqlite.database.safe", "SQLite database path must be a non-empty string");
            continue;
        }
        if (isAbsolutePackagePath(database)) {
            collector.fail("plan.sqlite.database.safe", "SQLite database paths in package Plans must be relocatable", {
                database,
            });
            continue;
        }
        if (!isSafeRelativePackagePath(database)) {
            collector.fail("plan.sqlite.database.safe", "SQLite relative database path must not escape the package root", {
                database,
            });
            continue;
        }
        const parent = toPackagePath(path.posix.dirname(toPackagePath(database)));
        if (parent !== "." && !(await exists(packagePath(root, parent)))) {
            collector.fail(
                "plan.sqlite.database.parent-present",
                "SQLite database parent directory must be present in the package",
                { database, parent },
            );
            continue;
        }
        collector.pass("plan.sqlite.database.safe", "SQLite relative database path is package-safe", { database });
    }
}

async function checkJunkFiles(root, collector) {
    const files = await listFiles(root);
    const badFiles = files.filter((file) => {
        const leaf = path.posix.basename(file);
        return (
            JUNK_FILE_NAMES.has(leaf) ||
            TOKEN_FILE_PATTERN.test(leaf) ||
            JUNK_PATH_PATTERNS.some((pattern) => pattern.test(file))
        );
    });
    if (badFiles.length === 0) {
        collector.pass("package.no-local-junk", "package contains no obvious local junk or token files");
    } else {
        collector.fail("package.no-local-junk", "package contains local junk or token-looking files", { files: badFiles });
    }
}

export async function validatePackageArtifacts({ root, fixture }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    const manifestPath = path.join(root, "manifest.json");
    if (!(await exists(manifestPath))) {
        collector.fail("manifest.exists", "manifest.json must exist");
        return collector.findings;
    }
    collector.pass("manifest.exists", "manifest.json exists");

    const manifest = await readJsonArtifact(manifestPath, "manifest", collector);
    if (manifest === undefined) {
        return collector.findings;
    }
    if (manifest.schema === MANIFEST_SCHEMA) {
        collector.pass("manifest.schema", "manifest uses sloppy.app-package.v1 schema");
    } else {
        collector.fail("manifest.schema", "manifest schema must be sloppy.app-package.v1", {
            actual: manifest.schema,
        });
    }

    const entry = readString(manifest.entry, "manifest.entry", collector);
    if (entry !== undefined) {
        await checkPackagePath(root, entry, "manifest.entry", collector);
    }

    const artifacts = manifest.artifacts;
    if (artifacts === null || typeof artifacts !== "object") {
        collector.fail("manifest.artifacts.shape", "manifest.artifacts must be an object");
        return collector.findings;
    }
    collector.pass("manifest.artifacts.shape", "manifest.artifacts is an object");

    const planPath = await checkPackagePath(root, artifacts.plan, "manifest.artifacts.plan", collector);
    const bundlePath = await checkPackagePath(root, artifacts.bundle, "manifest.artifacts.bundle", collector);
    if (artifacts.sourceMap !== undefined) {
        await checkPackagePath(root, artifacts.sourceMap, "manifest.artifacts.sourceMap", collector);
    }
    if (artifacts.routeDispatch !== undefined) {
        await checkPackagePath(root, artifacts.routeDispatch, "manifest.artifacts.routeDispatch", collector);
    }

    let plan;
    if (planPath !== undefined && (await exists(planPath))) {
        plan = await readJsonArtifact(planPath, "app.plan.json", collector);
    }
    if (bundlePath !== undefined && (await exists(bundlePath))) {
        collector.pass("app.js.exists", "app.js exists");
    }

    if (plan !== undefined) {
        inspectRelocatableJson(manifest, collector, ["manifest"]);
        inspectRelocatableJson(plan, collector, ["plan"]);
        if (planReferencesRouteDispatch(plan)) {
            if (artifacts.routeDispatch === undefined) {
                collector.fail(
                    "manifest.routeDispatch.matches-plan",
                    "manifest must include routes.slrt when Plan references route dispatch",
                    { planPath: plan.routeDispatch.artifact.path },
                );
            } else {
                collector.pass("manifest.routeDispatch.matches-plan", "manifest includes Plan route dispatch artifact");
            }
            const routePath = packagePath(root, artifacts.routeDispatch ?? `artifacts/${plan.routeDispatch.artifact.path}`);
            if (await exists(routePath)) {
                await checkHash(routePath, plan.routeDispatch.artifact.hash, "route-artifact", collector);
            }
        }
        await checkSQLitePlanPaths(root, plan, collector);
    }

    await checkMigrations(root, manifest, collector);
    await checkNativeLibraries(root, manifest, collector);
    await checkJunkFiles(root, collector);

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
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
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
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

export async function runPackageContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const fixtures = await loadFixtures(path.join(repoRoot, "tests/contracts/package/fixtures"));
    const findings = [];
    for (const fixture of fixtures) {
        const rawFindings = await validatePackageArtifacts({ root: fixture.root, fixture: fixture.name });
        const detected = errorInvariants(rawFindings);
        if (fixture.expected === "fail") {
            for (const invariant of fixture.expectedInvariants) {
                findings.push(
                    detected.includes(invariant)
                        ? expectedFailureFinding(fixture.name, invariant, detected, rawFindings)
                        : failedExpectationFinding(fixture.name, invariant, detected, rawFindings),
                );
            }
            continue;
        }
        findings.push(...rawFindings);
    }

    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
