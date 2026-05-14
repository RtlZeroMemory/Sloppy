import fs from "node:fs/promises";
import path from "node:path";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import {
    exists,
    isAbsolutePackagePath,
    isSafeRelativePackagePath,
    packagePath,
    readJson,
    sha256File,
    toPackagePath,
} from "../runner/artifact-utils.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { data, sql } from "../../../stdlib/sloppy/data.js";

const SUBSYSTEM = "data";
const KNOWN_PROVIDER_KINDS = new Set(["sqlite", "postgres", "sqlserver"]);
const SQLITE_PLACEHOLDER = "question";
const POSTGRES_PLACEHOLDER = "postgres";
const SQLSERVER_PLACEHOLDER = "question";
const SECRET_RE = /secret|pwd\s*=\s*(?!<redacted>)|password\s*=\s*(?!<redacted>)|token\s*=\s*(?!<redacted>)|postgres:\/\/[^:\s]+:(?!<redacted>)/iu;

async function loadDataFixtures(fixtureRoot) {
    const entries = await fs.readdir(fixtureRoot, { withFileTypes: true });
    const fixtures = [];
    for (const entry of entries.sort((left, right) => left.name.localeCompare(right.name))) {
        if (!entry.isDirectory()) {
            continue;
        }
        const root = path.join(fixtureRoot, entry.name);
        const configPath = path.join(root, "contract-fixture.json");
        let config = {
            mode: "artifact",
            expected: "pass",
            expectedInvariants: [],
        };
        try {
            config = {
                ...config,
                ...JSON.parse(await fs.readFile(configPath, "utf8")),
            };
        } catch (error) {
            if (error.code !== "ENOENT") {
                throw error;
            }
        }
        fixtures.push({
            name: entry.name,
            root,
            mode: config.mode ?? "artifact",
            scenario: config.scenario ?? "",
            expected: config.expected ?? "pass",
            expectedInvariants: config.expectedInvariants ?? [],
        });
    }
    return fixtures;
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

function providerKind(provider) {
    return provider?.providerKind ?? provider?.provider;
}

function providerMap(plan) {
    const providers = new Map();
    if (!Array.isArray(plan?.dataProviders)) {
        return providers;
    }
    for (const provider of plan.dataProviders) {
        if (provider !== null && typeof provider === "object" && typeof provider.token === "string") {
            providers.set(provider.token, provider);
        }
    }
    return providers;
}

function collectEffectReferences(value, refs = []) {
    if (Array.isArray(value)) {
        for (const entry of value) {
            collectEffectReferences(entry, refs);
        }
        return refs;
    }
    if (value === null || typeof value !== "object") {
        return refs;
    }
    if (Array.isArray(value.effects)) {
        for (const effect of value.effects) {
            if (effect !== null && typeof effect === "object" && typeof effect.provider === "string") {
                refs.push(effect);
            }
        }
    }
    for (const entry of Object.values(value)) {
        collectEffectReferences(entry, refs);
    }
    return refs;
}

function checkProviderDeclarations(plan, collector) {
    const providers = providerMap(plan);
    if (providers.size > 0) {
        collector.pass("data.provider.declared", "Plan declares data providers", {
            providers: [...providers.keys()],
        });
    }

    for (const [token, provider] of providers) {
        if (/^data\.[A-Za-z0-9_.-]+$/u.test(token)) {
            collector.pass("data.provider.declared", "provider token uses stable data.* form", { token });
        } else {
            collector.fail("data.provider.declared", "provider token must use stable data.* form", { token });
        }

        const kind = providerKind(provider);
        if (KNOWN_PROVIDER_KINDS.has(kind)) {
            collector.pass("data.provider.kind-known", "provider kind is known", { token, providerKind: kind });
        } else {
            collector.fail("data.provider.kind-known", "provider kind must be sqlite, postgres, or sqlserver", {
                token,
                providerKind: kind,
            });
        }

        if (provider.readOnly !== undefined) {
            const readOnlyMatchesAccess = provider.readOnly === true
                ? provider.access === undefined || provider.access === "read"
                : provider.access === undefined || provider.access === "readwrite" || provider.access === "write";
            if (readOnlyMatchesAccess) {
                collector.pass("data.readonly.rejects-write", "read-only provider metadata is consistent", {
                    token,
                    access: provider.access,
                    readOnly: provider.readOnly,
                });
            } else {
                collector.fail("data.readonly.rejects-write", "read-only provider metadata conflicts with access", {
                    token,
                    access: provider.access,
                    readOnly: provider.readOnly,
                });
            }
        }
    }

    const effects = collectEffectReferences(plan);
    const missing = effects.filter((effect) => !providers.has(effect.provider));
    if (missing.length === 0) {
        collector.pass("data.provider.effect-resolution", "route effects resolve to declared providers");
    } else {
        collector.fail("data.provider.effect-resolution", "route effects must reference declared providers", {
            missing: missing.map((effect) => effect.provider),
        });
    }
}

async function checkSqlitePath(root, provider, collector) {
    if (providerKind(provider) !== "sqlite") {
        return;
    }
    const database = provider.database ?? provider.path;
    if (database === ":memory:") {
        collector.pass("data.sqlite.path-safe", "SQLite :memory: database path is safe");
        return;
    }
    if (typeof database !== "string" || database.length === 0) {
        collector.fail("data.sqlite.path-safe", "SQLite provider must declare a database path");
        return;
    }
    if (isAbsolutePackagePath(database)) {
        collector.fail("data.sqlite.path-safe", "SQLite package database paths must be relative", { database });
        return;
    }
    if (!isSafeRelativePackagePath(database)) {
        collector.fail("data.sqlite.path-safe", "SQLite package database path must not escape the package", {
            database,
        });
        return;
    }
    const parent = toPackagePath(path.posix.dirname(toPackagePath(database)));
    if (parent !== "." && !(await exists(packagePath(root, parent)))) {
        collector.fail("data.sqlite.path-safe", "SQLite database parent directory must be included in the package", {
            database,
            parent,
        });
        return;
    }
    collector.pass("data.sqlite.path-safe", "SQLite database path is safe and relocatable", { database });
}

async function checkMigrations(root, manifest, providers, collector) {
    const migrations = manifest?.migrations;
    if (migrations === undefined) {
        collector.pass("data.migration.file-exists", "manifest has no migration claim");
        return;
    }
    if (!Array.isArray(migrations)) {
        collector.fail("data.migration.file-exists", "manifest migrations must be an array");
        return;
    }

    for (const [index, migration] of migrations.entries()) {
        const providerName = migration?.provider;
        const providerToken = typeof providerName === "string" && providerName.startsWith("data.")
            ? providerName
            : `data.${providerName}`;
        const configured = providers.get(providerToken);
        const configuredKind = configured === undefined ? undefined : providerKind(configured);
        if (configured === undefined) {
            collector.fail("data.provider.declared", "migration provider must map to a configured provider", {
                provider: providerName,
                providerToken,
            });
        }
        if (configuredKind === migration?.providerKind) {
            collector.pass("data.provider.kind-known", "migration provider kind matches configured provider kind", {
                provider: providerName,
                providerKind: migration.providerKind,
            });
        } else {
            collector.fail("data.provider.kind-known", "migration provider kind must match configured provider kind", {
                provider: providerName,
                expected: configuredKind,
                actual: migration?.providerKind,
            });
        }

        if (!Array.isArray(migration?.files)) {
            collector.fail("data.migration.file-exists", "migration entry must list package files", { index });
            continue;
        }
        const filePaths = migration.files.map((file) => file?.path).filter((filePath) => typeof filePath === "string");
        const sorted = [...filePaths].sort((left, right) => left.localeCompare(right));
        if (JSON.stringify(filePaths) === JSON.stringify(sorted)) {
            collector.pass("data.migration.order-deterministic", "migration files are listed in deterministic order", {
                files: filePaths,
            });
        } else {
            collector.fail("data.migration.order-deterministic", "migration files must be sorted lexically", {
                files: filePaths,
                expected: sorted,
            });
        }

        for (const file of migration.files) {
            if (typeof file?.path !== "string" || file.path.length === 0) {
                collector.fail("data.migration.file-exists", "migration file path must be present");
                continue;
            }
            if (isAbsolutePackagePath(file.path) || !isSafeRelativePackagePath(file.path)) {
                collector.fail("data.migration.file-exists", "migration file path must stay inside the package", {
                    path: file.path,
                });
                continue;
            }
            const filePath = packagePath(root, file.path);
            if (!(await exists(filePath))) {
                collector.fail("data.migration.file-exists", "migration file must exist in the package", {
                    path: file.path,
                });
                continue;
            }
            collector.pass("data.migration.file-exists", "migration file exists in the package", { path: file.path });
            if (typeof file.sha256 === "string") {
                const actual = await sha256File(filePath);
                if (actual === file.sha256) {
                    collector.pass("data.migration.hash-agreement", "migration hash matches package bytes", {
                        path: file.path,
                    });
                } else {
                    collector.fail("data.migration.hash-agreement", "migration hash must match package bytes", {
                        path: file.path,
                        expected: file.sha256,
                        actual,
                    });
                }
            }
        }
    }
}

function checkProviderSqlSemantics(collector) {
    const sqlite = sql.lower(["select * from users where id = ", ""], [1], {
        placeholderStyle: SQLITE_PLACEHOLDER,
    });
    const postgres = sql.lower(["select * from users where id = ", ""], [1], {
        placeholderStyle: POSTGRES_PLACEHOLDER,
    });
    const sqlserver = sql.lower(["select * from users where id = ", ""], [1], {
        placeholderStyle: SQLSERVER_PLACEHOLDER,
    });
    if (sqlite.text.includes("?") && postgres.text.includes("$1") && sqlserver.text.includes("?")) {
        collector.pass("data.provider.kind-known", "provider placeholder styles are provider-specific", {
            sqlite: sqlite.text,
            postgres: postgres.text,
            sqlserver: sqlserver.text,
        });
    } else {
        collector.fail("data.provider.kind-known", "provider placeholder styles must match provider contracts", {
            sqlite: sqlite.text,
            postgres: postgres.text,
            sqlserver: sqlserver.text,
        });
    }
}

export async function validateDataArtifacts({ root, fixture }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    const manifestPath = path.join(root, "manifest.json");
    const planPath = path.join(root, "artifacts", "app.plan.json");
    if (!(await exists(manifestPath))) {
        collector.fail("data.provider.declared", "manifest.json must exist");
        return collector.findings;
    }
    if (!(await exists(planPath))) {
        collector.fail("data.provider.declared", "artifacts/app.plan.json must exist");
        return collector.findings;
    }

    const manifest = await readJsonArtifact(manifestPath, "manifest", collector);
    const plan = await readJsonArtifact(planPath, "app.plan.json", collector);
    if (manifest === undefined || plan === undefined) {
        return collector.findings;
    }

    const providers = providerMap(plan);
    checkProviderDeclarations(plan, collector);
    for (const provider of providers.values()) {
        await checkSqlitePath(root, provider, collector);
    }
    await checkMigrations(root, manifest, providers, collector);
    checkProviderSqlSemantics(collector);
    return collector.findings;
}

class ContractSqlite {
    constructor({ readOnly = false, rollbackBroken = false, leakDiagnostics = false } = {}) {
        this.readOnly = readOnly;
        this.rollbackBroken = rollbackBroken;
        this.leakDiagnostics = leakDiagnostics;
        this.users = [];
        this.migrations = [];
        this.nextId = 1;
    }

    clone() {
        const clone = new ContractSqlite({
            readOnly: this.readOnly,
            rollbackBroken: this.rollbackBroken,
            leakDiagnostics: this.leakDiagnostics,
        });
        clone.users = this.users.map((user) => ({ ...user }));
        clone.migrations = this.migrations.map((migration) => ({ ...migration }));
        clone.nextId = this.nextId;
        return clone;
    }

    commitFrom(source) {
        this.users = source.users;
        this.migrations = source.migrations;
        this.nextId = source.nextId;
    }

    assertWriteAllowed(sql) {
        if (!this.readOnly) {
            return;
        }
        if (/^\s*(insert|update|delete|create|drop|alter)\b/iu.test(sql)) {
            throw new Error("sloppy: sqlite provider is read-only; write operation rejected");
        }
    }

    exec(sql, params = []) {
        this.assertWriteAllowed(sql);
        if (/insert into _sloppy_migrations/iu.test(sql)) {
            const [name, hash, appliedAt] = params;
            this.migrations.push({ name, hash, appliedAt: appliedAt ?? "provider-clock" });
            return { affectedRows: 1 };
        }
        if (/insert into users/iu.test(sql)) {
            this.users.push({ id: this.nextId, name: params[0] });
            this.nextId += 1;
            return { affectedRows: 1, lastInsertId: this.nextId - 1 };
        }
        if (/duplicate_secret/iu.test(sql)) {
            const detail = this.leakDiagnostics
                ? "postgres://ada:secret@prod/db Password=secret"
                : "postgres://ada:<redacted>@prod/db Password=<redacted>";
            throw new Error(`sloppy: provider diagnostic ${detail}`);
        }
        return { affectedRows: 0 };
    }

    query(sql, params = [], options = {}) {
        if (/from _sloppy_migrations/iu.test(sql)) {
            return this.migrations.map((migration) => ({ ...migration }));
        }
        if (/from users/iu.test(sql)) {
            const rows = this.users
                .slice()
                .sort((left, right) => left.id - right.id)
                .map((user) => ({ ...user }));
            return Number.isInteger(options.maxRows) ? rows.slice(0, options.maxRows) : rows;
        }
        return [];
    }

    queryOne(sql, params = [], options = {}) {
        return this.query(sql, params, options)[0] ?? null;
    }

    async transaction(callback) {
        const tx = this.clone();
        try {
            const result = await callback(tx);
            this.commitFrom(tx);
            return result;
        } catch (error) {
            if (this.rollbackBroken) {
                this.commitFrom(tx);
            }
            throw error;
        }
    }

    __debug() {
        return Object.freeze({ kind: "sqlite-connection" });
    }
}

async function withFixtureCwd(root, callback) {
    const previous = process.cwd();
    const previousSloppy = globalThis.__sloppy;
    process.chdir(root);
    globalThis.__sloppy = {
        ...(previousSloppy ?? {}),
        fs: {
            async directoryList(relativePath) {
                const entries = await fs.readdir(path.resolve(relativePath), { withFileTypes: true });
                return entries
                    .sort((left, right) => left.name.localeCompare(right.name))
                    .map((entry) => ({
                        name: entry.name,
                        kind: entry.isDirectory() ? "directory" : "file",
                    }));
            },
            readText(relativePath) {
                return fs.readFile(path.resolve(relativePath), "utf8");
            },
        },
    };
    try {
        return await callback();
    } finally {
        process.chdir(previous);
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

async function validateBehavior(root, collector, scenario) {
    await withFixtureCwd(root, async () => {
        const db = new ContractSqlite({
            rollbackBroken: scenario === "rollback-persists",
            leakDiagnostics: scenario === "diagnostic-leak",
        });

        const firstApply = await data.migrations.apply(db, {
            provider: "sqlite",
            path: "migrations/*.sql",
        });
        const secondApply = await data.migrations.apply(db, {
            provider: "sqlite",
            path: "migrations/*.sql",
        });
        const status = await data.migrations.status(db, {
            provider: "sqlite",
            path: "migrations/*.sql",
        });
        if (firstApply.applied === 2 && firstApply.skipped === 0 && secondApply.applied === 0 && status.status === "current") {
            collector.pass("data.migration.status-applied", "SQLite migrations apply and report current status");
        } else {
            collector.fail("data.migration.status-applied", "SQLite migrations must apply once and report current status", {
                firstApply,
                secondApply,
                status,
            });
        }

        await db.transaction(async (tx) => {
            tx.exec("insert into users (name) values (?)", ["Ada"]);
        });
        const committed = db.query("select id, name from users order by id");
        if (committed.some((row) => row.name === "Ada")) {
            collector.pass("data.transaction.commit", "transaction commit persists changes");
        } else {
            collector.fail("data.transaction.commit", "transaction commit must persist changes");
        }

        try {
            await db.transaction(async (tx) => {
                tx.exec("insert into users (name) values (?)", ["Rollback"]);
                throw new Error("force rollback");
            });
        } catch {
            // Expected rollback path.
        }
        const rollbackRows = db.query("select id, name from users order by id");
        if (!rollbackRows.some((row) => row.name === "Rollback")) {
            collector.pass("data.transaction.rollback", "transaction rollback discards changes");
        } else {
            collector.fail("data.transaction.rollback", "transaction rollback must discard changes");
        }

        const readonly = new ContractSqlite({ readOnly: true });
        let readOnlyRejected = false;
        try {
            if (scenario === "readonly-accepts-write") {
                readonly.readOnly = false;
            }
            readonly.exec("insert into users (name) values (?)", ["Grace"]);
        } catch {
            readOnlyRejected = true;
        }
        if (readOnlyRejected) {
            collector.pass("data.readonly.rejects-write", "read-only provider rejects writes");
        } else {
            collector.fail("data.readonly.rejects-write", "read-only provider must reject writes");
        }

        db.exec("insert into users (name) values (?)", ["Ada'); drop table users; --"]);
        const parameterRows = db.query("select id, name from users order by id");
        if (parameterRows.some((row) => typeof row.name === "string" && row.name.includes("drop table users")) && parameterRows.length >= 2) {
            collector.pass("data.provider.effect-resolution", "parameter binding treats input as a value");
        } else {
            collector.fail("data.provider.effect-resolution", "parameter binding must not execute parameter text");
        }

        const paged = db.query("select id, name from users order by id", [], { maxRows: 2 });
        if (paged.length === 2 && paged[0].id < paged[1].id) {
            collector.pass("data.provider.kind-known", "SQLite list pagination uses deterministic order");
        } else {
            collector.fail("data.provider.kind-known", "SQLite list pagination must return deterministic order", { paged });
        }

        let diagnostic = "";
        try {
            db.exec("duplicate_secret", []);
        } catch (error) {
            diagnostic = String(error.message);
        }
        if (!SECRET_RE.test(diagnostic)) {
            collector.pass("data.diagnostics.redacted", "provider diagnostics redact secrets");
        } else {
            collector.fail("data.diagnostics.redacted", "provider diagnostics must not leak connection strings or secrets", {
                diagnostic,
            });
        }
    });
}

async function validateDataBehavior({ root, fixture, scenario }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    await validateBehavior(root, collector, scenario);
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

function unexpectedErrorFinding(fixture, invariant, expectedInvariants, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.unexpected`,
        status: "pass",
        severity: "warning",
        subsystem: SUBSYSTEM,
        invariant: `negative.unexpected.${invariant}`,
        fixture,
        message: `broken fixture produced unexpected ${invariant} finding`,
        details: {
            expectedInvariants,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

export async function runDataContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const fixtures = await loadDataFixtures(path.join(repoRoot, "tests/contracts/data/fixtures"));
    const findings = [];
    for (const fixture of fixtures) {
        const rawFindings = fixture.mode === "behavior"
            ? await validateDataBehavior({
                  root: fixture.root,
                  fixture: fixture.name,
                  scenario: fixture.scenario,
              })
            : await validateDataArtifacts({ root: fixture.root, fixture: fixture.name });
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
                    findings.push(
                        unexpectedErrorFinding(fixture.name, invariant, fixture.expectedInvariants, rawFindings),
                    );
                }
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
