import { Text } from "./codec.js";
import { Random } from "./crypto.js";
import { data, Migrations } from "./data.js";
import { File } from "./fs.js";
import { Process as SloppyProcess } from "./os.js";
import { Redis } from "./redis.js";

const DEFAULT_POSTGRES_IMAGE = "postgres:17";
const DEFAULT_SQLSERVER_IMAGE = "mcr.microsoft.com/mssql/server:2022-latest";
const DEFAULT_REDIS_IMAGE = "redis:7-alpine";
const DEFAULT_SQLSERVER_ODBC_DRIVER = "ODBC Driver 17 for SQL Server";
const LOCALHOST = "127.0.0.1";
const POSTGRES_PORT = 5432;
const SQLSERVER_PORT = 1433;
const REDIS_PORT = 6379;
const DEFAULT_STARTUP_TIMEOUT_MS = 30000;
const DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS = 60000;
const DEFAULT_STOP_TIMEOUT_MS = 10000;
const DEFAULT_LOG_TAIL = 120;
const SECRET_REDACTION = "[REDACTED]";
const ASYNC_DISPOSE = Symbol.asyncDispose;
let nonSecurityNameCounter = 0;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function processOutputText(value) {
    if (value === undefined || value === null) {
        return "";
    }
    if (typeof value === "string") {
        return value;
    }
    if (value instanceof Uint8Array) {
        return Text.utf8.decode(value);
    }
    return String(value);
}

function boundedText(value, max = 12000) {
    const text = String(value ?? "");
    if (text.length <= max) {
        return text;
    }
    return text.slice(text.length - max);
}

function normalizeTimeout(value, fallback, subject) {
    if (value === undefined) {
        return fallback;
    }
    if (!Number.isFinite(value) || value < 1) {
        throw new TypeError(`Sloppy TestServices ${subject} must be a positive finite number.`);
    }
    return Math.ceil(value);
}

function normalizePort(value, fallback, subject) {
    if (value === undefined) {
        return fallback;
    }
    if (!Number.isInteger(value) || value < 1 || value > 65535) {
        throw new TypeError(`Sloppy TestServices ${subject} must be an integer from 1 to 65535.`);
    }
    return value;
}

function normalizeRedisDatabase(value, fallback) {
    const selected = value ?? fallback;
    if (!Number.isInteger(selected) || selected < 0 || selected > 15) {
        throw new TypeError("Sloppy TestServices Redis database must be from 0 to 15.");
    }
    return selected;
}

function normalizeNonEmptyString(value, fallback, subject) {
    const selected = value ?? fallback;
    if (typeof selected !== "string" || selected.length === 0 || selected.includes("\0")) {
        throw new TypeError(`Sloppy TestServices ${subject} must be a non-empty string without NUL.`);
    }
    return selected;
}

function randomHex(length) {
    try {
        return Array.from(Random.bytes(length), (byte) => byte.toString(16).padStart(2, "0")).join("");
    } catch (error) {
        const bytes = new Uint8Array(length);
        const crypto = globalThis.crypto;
        if (crypto?.getRandomValues !== undefined) {
            crypto.getRandomValues(bytes);
            return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
        }
        throw new Error(`SLOPPY_E_TESTSERVICES_SECURE_RANDOM_UNAVAILABLE: ${error?.message ?? error}`);
    }
}

function nonSecuritySuffix() {
    nonSecurityNameCounter += 1;
    return `${Date.now().toString(16)}-${nonSecurityNameCounter.toString(16)}`;
}

function randomContainerName(kind) {
    return `sloppy-testservices-${kind}-${nonSecuritySuffix()}`;
}

function generatedSqlServerPassword() {
    return `Sloppy_${randomHex(10)}_Aa1!`;
}

function redactWithSecrets(value, secrets) {
    let text = String(value ?? "");
    for (const secret of secrets) {
        if (typeof secret === "string" && secret.length > 0) {
            text = text.replaceAll(secret, SECRET_REDACTION);
        }
    }
    text = data.postgres.redactConnectionString(text);
    text = data.sqlserver.redactConnectionString(text);
    text = Redis._redactUrl(text);
    return text;
}

function safeObject(value, secrets) {
    if (value === null || value === undefined) {
        return value;
    }
    if (typeof value === "string") {
        return redactWithSecrets(value, secrets);
    }
    if (typeof value === "number" || typeof value === "boolean") {
        return value;
    }
    if (Array.isArray(value)) {
        return Object.freeze(value.map((entry) => safeObject(entry, secrets)));
    }
    if (isPlainObject(value)) {
        const safe = {};
        for (const [key, entryValue] of Object.entries(value)) {
            if (/password|secret|token|connectionstring|connectionString|pwd/iu.test(key)) {
                safe[key] = SECRET_REDACTION;
            } else {
                safe[key] = safeObject(entryValue, secrets);
            }
        }
        return Object.freeze(safe);
    }
    return redactWithSecrets(value, secrets);
}

function createDiagnosticState(kind, image, name, secrets) {
    return {
        kind,
        image,
        name,
        containerId: undefined,
        host: LOCALHOST,
        port: undefined,
        startupState: "created",
        readinessAttempts: 0,
        lastReadinessError: undefined,
        logTail: "",
        timings: {
            createdAt: new Date().toISOString(),
            startedAt: undefined,
            readyAt: undefined,
            disposedAt: undefined,
        },
        cleanupErrors: [],
        secrets,
    };
}

function dockerUnavailableError(reason) {
    const error = new Error(`SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE: Docker is unavailable for Sloppy TestServices.

Reason:
  ${reason}

Fix:
  Start Docker Desktop or a compatible Docker daemon, ensure the docker CLI is on PATH, then rerun the opt-in TestServices lane.`);
    error.code = "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE";
    return error;
}

function providerUnavailableError(kind) {
    const provider = kind === "postgres" ? "PostgreSQL" : "SQL Server";
    const error = new Error(`SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE: ${provider} TestServices require the matching Sloppy data provider bridge.

Provider:
  ${kind}

Reason:
  The active runtime does not expose the native ${kind} provider bridge, so TestServices cannot prove real database readiness.

Fix:
  Run this lane under a V8/native-provider build, or skip the TestServices container lane with this reason.`);
    error.code = "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE";
    return error;
}

class DockerCliBackend {
    constructor(options = {}) {
        this.command = options.command ?? "docker";
        this.cwd = options.cwd;
        this.env = options.env;
    }

    async run(args, options = {}) {
        const result = await SloppyProcess.run(this.command, args, {
            cwd: options.cwd ?? this.cwd,
            env: options.env ?? this.env,
            capture: "bytes",
            timeoutMs: options.timeoutMs ?? 30000,
            maxStdoutBytes: options.maxStdoutBytes ?? 1024 * 1024,
            maxStderrBytes: options.maxStderrBytes ?? 1024 * 1024,
        });
        return Object.freeze({
            exitCode: result.exitCode,
            stdout: processOutputText(result.stdout),
            stderr: processOutputText(result.stderr),
            timedOut: result.timedOut === true,
        });
    }
}

function dockerBackend(options = undefined) {
    if (options?.dockerBackend !== undefined) {
        return options.dockerBackend;
    }
    return new DockerCliBackend(options?.docker);
}

async function dockerRunOk(backend, args, options = {}) {
    const result = await backend.run(args, options);
    if (result.exitCode !== 0 || result.timedOut === true) {
        const stderr = boundedText(result.stderr || result.stdout);
        throw new Error(`docker ${args[0]} failed with exit code ${result.exitCode}.${stderr.length === 0 ? "" : `\n${stderr}`}`);
    }
    return result;
}

async function dockerAvailable(options = {}) {
    const backend = dockerBackend(options);
    try {
        const result = await backend.run(["version", "--format", "{{json .}}"], {
            timeoutMs: options.timeoutMs ?? 5000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        if (result.exitCode !== 0 || result.timedOut === true) {
            const reason = result.timedOut === true
                ? "docker version timed out"
                : boundedText(result.stderr || result.stdout || "docker version failed", 1000);
            return Object.freeze({ ok: false, available: false, reason });
        }
        let version = undefined;
        try {
            version = JSON.parse(result.stdout);
        } catch {
            version = result.stdout.trim();
        }
        return Object.freeze({ ok: true, available: true, reason: undefined, version });
    } catch (error) {
        return Object.freeze({
            ok: false,
            available: false,
            reason: String(error?.message ?? error),
        });
    }
}

async function dockerRequire(options = {}) {
    const available = await dockerAvailable(options);
    if (available.ok) {
        return available;
    }
    throw dockerUnavailableError(available.reason);
}

async function ensureImage(backend, image, options) {
    const inspect = await backend.run(["image", "inspect", image], {
        timeoutMs: options.dockerTimeoutMs ?? 15000,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    });
    if (inspect.exitCode === 0) {
        return;
    }
    await dockerRunOk(backend, ["pull", image], {
        timeoutMs: options.pullTimeoutMs ?? 120000,
        maxStdoutBytes: 256 * 1024,
        maxStderrBytes: 256 * 1024,
    });
}

function parseInspectJson(text) {
    const parsed = JSON.parse(text);
    if (!Array.isArray(parsed) || parsed.length === 0 || parsed[0] === null) {
        throw new Error("docker inspect returned no container metadata.");
    }
    return parsed[0];
}

function mappedPortFromInspect(metadata, internalPort) {
    const ports = metadata?.NetworkSettings?.Ports;
    const entries = ports?.[`${internalPort}/tcp`];
    if (!Array.isArray(entries) || entries.length === 0) {
        throw new Error(`docker inspect did not report a mapped host port for ${internalPort}/tcp.`);
    }
    const hostPort = Number(entries[0].HostPort);
    if (!Number.isInteger(hostPort) || hostPort < 1 || hostPort > 65535) {
        throw new Error(`docker inspect returned an invalid host port for ${internalPort}/tcp.`);
    }
    return hostPort;
}

async function inspectContainer(backend, containerId, internalPort, options) {
    const result = await dockerRunOk(backend, ["inspect", containerId], {
        timeoutMs: options.dockerTimeoutMs ?? 15000,
        maxStdoutBytes: 256 * 1024,
        maxStderrBytes: 64 * 1024,
    });
    const metadata = parseInspectJson(result.stdout);
    return { metadata, port: mappedPortFromInspect(metadata, internalPort) };
}

async function dockerLogs(backend, containerId, tail, options) {
    if (containerId === undefined) {
        return "";
    }
    try {
        const result = await backend.run(["logs", "--tail", String(tail), containerId], {
            timeoutMs: options.dockerTimeoutMs ?? 15000,
            maxStdoutBytes: 256 * 1024,
            maxStderrBytes: 256 * 1024,
        });
        return boundedText(`${result.stdout}${result.stderr}`);
    } catch {
        return "";
    }
}

function cleanupFailure(operation, args, resultOrError, secrets) {
    const details = resultOrError instanceof Error
        ? resultOrError.message
        : `${resultOrError.timedOut === true ? "timed out" : `exit code ${resultOrError.exitCode}`}: ${resultOrError.stderr || resultOrError.stdout}`;
    return Object.freeze({
        operation,
        command: `docker ${args.join(" ")}`,
        message: redactWithSecrets(boundedText(details, 2000), secrets),
    });
}

async function cleanupDockerCommand(backend, args, options, state, required) {
    try {
        const result = await backend.run(args, options);
        if (result.exitCode === 0 && result.timedOut !== true) {
            return;
        }
        const failure = cleanupFailure(args[0], args, result, state.secrets);
        state.cleanupErrors.push(failure);
        if (required) {
            throw new Error(`SLOPPY_E_TESTSERVICES_CLEANUP_FAILED: ${failure.message}`);
        }
    } catch (error) {
        if (String(error?.message ?? error).startsWith("SLOPPY_E_TESTSERVICES_CLEANUP_FAILED:")) {
            throw error;
        }
        const failure = cleanupFailure(args[0], args, error, state.secrets);
        state.cleanupErrors.push(failure);
        if (required) {
            throw new Error(`SLOPPY_E_TESTSERVICES_CLEANUP_FAILED: ${failure.message}`, { cause: error });
        }
    }
}

async function removeContainer(backend, containerId, options, state) {
    if (containerId === undefined) {
        return;
    }
    await cleanupDockerCommand(backend, ["stop", "--time", String(Math.ceil((options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS) / 1000)), containerId], {
        timeoutMs: options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    }, state, false);
    await cleanupDockerCommand(backend, ["rm", "--force", containerId], {
        timeoutMs: options.rmTimeoutMs ?? 15000,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    }, state, options.keepContainerOnFailure !== true);
}

function postgresConnectionString(options, host, port) {
    const user = encodeURIComponent(options.username);
    const password = encodeURIComponent(options.password);
    const database = encodeURIComponent(options.database);
    return `postgresql://${user}:${password}@${host}:${port}/${database}`;
}

function odbcEscapeValue(value) {
    const text = String(value);
    const escaped = text.replaceAll("}", "}}");
    if (/[;{}]/u.test(text) || /^\s|\s$/u.test(text)) {
        return "{" + escaped + "}";
    }
    return text;
}

function odbcBraceValue(value) {
    return "{" + String(value).replaceAll("}", "}}") + "}";
}

function sqlServerConnectionString(options, host, port) {
    return [
        `Driver=${odbcBraceValue(options.driver)}`,
        `Server=${host},${port}`,
        `Database=${odbcEscapeValue(options.database)}`,
        `UID=${odbcEscapeValue(options.username)}`,
        `PWD=${odbcEscapeValue(options.password)}`,
        "Encrypt=yes",
        "TrustServerCertificate=yes",
    ].join(";");
}

function normalizedPostgresOptions(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy TestServices.postgres options must be a plain object.");
    }
    const username = normalizeNonEmptyString(options.username, "sloppy", "PostgreSQL username");
    const password = normalizeNonEmptyString(options.password, "sloppy", "PostgreSQL password");
    const database = normalizeNonEmptyString(options.database, "app_test", "PostgreSQL database");
    const image = normalizeNonEmptyString(options.image, DEFAULT_POSTGRES_IMAGE, "PostgreSQL image");
    const hostPort = normalizePort(options.hostPort, undefined, "PostgreSQL hostPort");
    const startupTimeoutMs = normalizeTimeout(options.startupTimeoutMs, DEFAULT_STARTUP_TIMEOUT_MS, "PostgreSQL startupTimeoutMs");
    const stopTimeoutMs = normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "PostgreSQL stopTimeoutMs");
    const dockerTimeoutMs = normalizeTimeout(options.dockerTimeoutMs, undefined, "PostgreSQL dockerTimeoutMs");
    const pullTimeoutMs = normalizeTimeout(options.pullTimeoutMs, undefined, "PostgreSQL pullTimeoutMs");
    const rmTimeoutMs = normalizeTimeout(options.rmTimeoutMs, undefined, "PostgreSQL rmTimeoutMs");
    const name = options.containerName === undefined
        ? randomContainerName("postgres")
        : normalizeNonEmptyString(options.containerName, undefined, "PostgreSQL containerName");
    return Object.freeze({
        kind: "postgres",
        image,
        username,
        password,
        database,
        host: LOCALHOST,
        containerName: name,
        hostPort,
        startupTimeoutMs,
        stopTimeoutMs,
        dockerTimeoutMs,
        pullTimeoutMs,
        rmTimeoutMs,
        keepContainerOnFailure: options.keepContainerOnFailure === true,
        migrations: options.migrations,
        dockerBackend: options.dockerBackend,
        docker: options.docker,
    });
}

function normalizedSqlServerOptions(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy TestServices.sqlServer options must be a plain object.");
    }
    const database = normalizeNonEmptyString(options.database, "app_test", "SQL Server database");
    const driver = normalizeNonEmptyString(options.driver, DEFAULT_SQLSERVER_ODBC_DRIVER, "SQL Server ODBC driver");
    const username = normalizeNonEmptyString(options.username, "sa", "SQL Server username");
    if (username !== "sa") {
        throw new TypeError('Sloppy TestServices SQL Server currently supports only username "sa".');
    }
    const image = normalizeNonEmptyString(options.image, DEFAULT_SQLSERVER_IMAGE, "SQL Server image");
    const hostPort = normalizePort(options.hostPort, undefined, "SQL Server hostPort");
    const startupTimeoutMs = normalizeTimeout(options.startupTimeoutMs, DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS, "SQL Server startupTimeoutMs");
    const stopTimeoutMs = normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "SQL Server stopTimeoutMs");
    const dockerTimeoutMs = normalizeTimeout(options.dockerTimeoutMs, undefined, "SQL Server dockerTimeoutMs");
    const pullTimeoutMs = normalizeTimeout(options.pullTimeoutMs, undefined, "SQL Server pullTimeoutMs");
    const rmTimeoutMs = normalizeTimeout(options.rmTimeoutMs, undefined, "SQL Server rmTimeoutMs");
    const password = options.password === undefined
        ? generatedSqlServerPassword()
        : normalizeNonEmptyString(options.password, undefined, "SQL Server password");
    const name = options.containerName === undefined
        ? randomContainerName("sqlserver")
        : normalizeNonEmptyString(options.containerName, undefined, "SQL Server containerName");
    return Object.freeze({
        kind: "sqlserver",
        image,
        username,
        password,
        database,
        driver,
        host: LOCALHOST,
        containerName: name,
        hostPort,
        startupTimeoutMs,
        stopTimeoutMs,
        dockerTimeoutMs,
        pullTimeoutMs,
        rmTimeoutMs,
        keepContainerOnFailure: options.keepContainerOnFailure === true,
        migrations: options.migrations,
        dockerBackend: options.dockerBackend,
        docker: options.docker,
    });
}

function normalizedRedisOptions(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy TestServices.redis options must be a plain object.");
    }
    const image = normalizeNonEmptyString(options.image, DEFAULT_REDIS_IMAGE, "Redis image");
    const hostPort = normalizePort(options.hostPort, undefined, "Redis hostPort");
    const startupTimeoutMs = normalizeTimeout(options.startupTimeoutMs, DEFAULT_STARTUP_TIMEOUT_MS, "Redis startupTimeoutMs");
    const readinessTimeoutMs = normalizeTimeout(options.readinessTimeoutMs, startupTimeoutMs, "Redis readinessTimeoutMs");
    const stopTimeoutMs = normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "Redis stopTimeoutMs");
    const dockerTimeoutMs = normalizeTimeout(options.dockerTimeoutMs, undefined, "Redis dockerTimeoutMs");
    const pullTimeoutMs = normalizeTimeout(options.pullTimeoutMs, undefined, "Redis pullTimeoutMs");
    const rmTimeoutMs = normalizeTimeout(options.rmTimeoutMs, undefined, "Redis rmTimeoutMs");
    const database = normalizeRedisDatabase(options.database, 0);
    const password = options.password === undefined
        ? undefined
        : normalizeNonEmptyString(options.password, undefined, "Redis password");
    const name = options.containerName === undefined
        ? randomContainerName("redis")
        : normalizeNonEmptyString(options.containerName, undefined, "Redis containerName");
    return Object.freeze({
        kind: "redis",
        image,
        database,
        password,
        host: LOCALHOST,
        containerName: name,
        hostPort,
        startupTimeoutMs,
        readinessTimeoutMs,
        stopTimeoutMs,
        dockerTimeoutMs,
        pullTimeoutMs,
        rmTimeoutMs,
        keepContainerOnFailure: options.keepContainerOnFailure === true,
        dockerBackend: options.dockerBackend,
        docker: options.docker,
    });
}

function providerBridgeAvailable(kind) {
    return kind === "postgres"
        ? data.postgres.supports.nativeStdlibBridge === true
        : data.sqlserver.supports.nativeStdlibBridge === true;
}

function openProvider(kind, connectionString) {
    if (!providerBridgeAvailable(kind)) {
        throw providerUnavailableError(kind);
    }
    return kind === "postgres"
        ? data.postgres.open({ connectionString })
        : data.sqlserver.open({ connectionString });
}

async function withDb(kind, connectionString, callback) {
    const db = openProvider(kind, connectionString);
    try {
        return await callback(db);
    } finally {
        await Promise.resolve(db.close?.()).catch(() => {});
    }
}

async function waitForReady(kind, state, connectionString, options) {
    const startedAt = Date.now();
    state.startupState = "waiting";
    while (Date.now() - startedAt < options.startupTimeoutMs) {
        const remainingMs = options.startupTimeoutMs - (Date.now() - startedAt);
        state.readinessAttempts += 1;
        try {
            if (kind === "sqlserver") {
                const masterConnectionString = sqlServerConnectionString({
                    ...options,
                    database: "master",
                }, LOCALHOST, state.port);
                await withDb(kind, masterConnectionString, async (db) => {
                    await db.exec(`if db_id(N'${options.database.replaceAll("'", "''")}') is null create database [${options.database.replaceAll("]", "]]")}]`, [], { timeoutMs: remainingMs });
                });
            }
            await withDb(kind, connectionString, async (db) => {
                await db.queryOne("select 1 as ok", [], { timeoutMs: remainingMs });
            });
            state.startupState = "ready";
            state.timings.readyAt = new Date().toISOString();
            return;
        } catch (error) {
            state.lastReadinessError = String(error?.message ?? error);
            const retryDelayMs = Math.min(1000, 100 + state.readinessAttempts * 100, Math.max(0, options.startupTimeoutMs - (Date.now() - startedAt)));
            if (retryDelayMs > 0) {
                await sleep(retryDelayMs);
            }
        }
    }
    throw new Error(`readiness timed out after ${options.startupTimeoutMs}ms: ${state.lastReadinessError ?? "no readiness result"}`);
}

function containerCreateArgs(kind, options) {
    const port = kind === "postgres" ? POSTGRES_PORT : SQLSERVER_PORT;
    const publish = options.hostPort === undefined
        ? `${LOCALHOST}::${port}`
        : `${LOCALHOST}:${options.hostPort}:${port}`;
    if (kind === "postgres") {
        return [
            "create",
            "--name",
            options.containerName,
            "-e",
            `POSTGRES_USER=${options.username}`,
            "-e",
            `POSTGRES_PASSWORD=${options.password}`,
            "-e",
            `POSTGRES_DB=${options.database}`,
            "-p",
            publish,
            options.image,
        ];
    }
    return [
        "create",
        "--name",
        options.containerName,
        "-e",
        "ACCEPT_EULA=Y",
        "-e",
        `MSSQL_SA_PASSWORD=${options.password}`,
        "-e",
        "MSSQL_PID=Developer",
        "-p",
        publish,
        options.image,
    ];
}

function providerPlaceholder(kind) {
    return kind === "postgres" ? "postgres" : "named";
}

function envWithPrefix(entries, prefix) {
    if (prefix === undefined || prefix === "") {
        return Object.freeze(entries);
    }
    const normalizedPrefix = String(prefix).replace(/_$/u, "");
    return Object.freeze(Object.fromEntries(
        Object.entries(entries).map(([key, value]) => [`${normalizedPrefix}_${key}`, value]),
    ));
}

function normalizeMigrationList(pathOrGlob) {
    if (typeof pathOrGlob === "string") {
        if (pathOrGlob.length === 0 || pathOrGlob.includes("\0")) {
            throw new TypeError("Sloppy TestServices migration path must be a non-empty string without NUL.");
        }
        return [pathOrGlob];
    }
    if (!Array.isArray(pathOrGlob) || pathOrGlob.length === 0) {
        throw new TypeError("Sloppy TestServices migrate expects a path string or non-empty path array.");
    }
    return pathOrGlob.map((entry) => {
        if (typeof entry !== "string" || entry.length === 0 || entry.includes("\0")) {
            throw new TypeError("Sloppy TestServices migration paths must be non-empty strings without NUL.");
        }
        return entry;
    });
}

function isSqlGlob(path) {
    return /(?:^|[\\/])\*\.sql$/u.test(path);
}

async function applyMigrationPath(db, kind, path) {
    if (isSqlGlob(path)) {
        await Migrations.apply(db, { provider: kind, path });
        return;
    }
    if (!path.endsWith(".sql")) {
        throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_PATH: migration path must be a .sql file or directory glob ending in *.sql.

Path:
  ${path}`);
    }
    let sqlText;
    try {
        sqlText = await File.readText(path);
    } catch (error) {
        throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_MISSING: migration file is missing or unreadable.

Migration:
  ${path}`, { cause: error });
    }
    try {
        await db.exec(sqlText, []);
    } catch (error) {
        throw new Error(`SLOPPY_E_TESTSERVICES_MIGRATION_FAILED: migration failed.

Migration:
  ${path}

Reason:
  ${String(error?.message ?? error)}`, { cause: error });
    }
}

function resetSql(kind) {
    if (kind === "postgres") {
        return Object.freeze([
            "drop schema if exists public cascade",
            "create schema public",
        ]);
    }
    throw new Error("SQL Server reset uses database recreation from master.");
}

function sqlServerIdentifier(value) {
    return `[${String(value).replaceAll("]", "]]")}]`;
}

function sqlServerStringLiteral(value) {
    return `N'${String(value).replaceAll("'", "''")}'`;
}

function resetSqlServerDatabaseSql(database) {
    const name = sqlServerIdentifier(database);
    const literal = sqlServerStringLiteral(database);
    return `
if db_id(${literal}) is not null
begin
    alter database ${name} set single_user with rollback immediate;
    drop database ${name};
end;
create database ${name};`;
}

async function resetSqlServerDatabase(options, port) {
    const masterConnectionString = sqlServerConnectionString({
        ...options,
        database: "master",
    }, LOCALHOST, port);
    await withDb("sqlserver", masterConnectionString, (db) =>
        db.exec(resetSqlServerDatabaseSql(options.database), []));
}

function createService(kind, options, backend, state, connectionString, port) {
    const ownedProviders = new Set();
    const migrations = [];
    let disposed = false;
    state.port = port;

    const service = {
        kind,
        id: state.containerId,
        image: options.image,
        host: LOCALHOST,
        port,
        connectionString,
        async start() {
            return service;
        },
        async stop() {
            await service.dispose();
        },
        async dispose() {
            if (disposed) {
                return;
            }
            state.startupState = "disposing";
            for (const provider of ownedProviders) {
                await Promise.resolve(provider.close?.()).catch(() => {});
            }
            ownedProviders.clear();
            await removeContainer(backend, state.containerId, options, state);
            disposed = true;
            state.startupState = "disposed";
            state.timings.disposedAt = new Date().toISOString();
        },
        exec(sql, params = []) {
            if (typeof sql !== "string" || sql.length === 0) {
                throw new TypeError("Sloppy TestServices exec SQL must be a non-empty string.");
            }
            if (!Array.isArray(params)) {
                throw new TypeError("Sloppy TestServices exec params must be an array.");
            }
            return withDb(kind, connectionString, (db) => db.exec(sql, params));
        },
        async migrate(pathOrGlob) {
            const paths = normalizeMigrationList(pathOrGlob);
            await withDb(kind, connectionString, async (db) => {
                for (const path of paths) {
                    await applyMigrationPath(db, kind, path);
                    migrations.push(path);
                }
            });
        },
        async seed(fn) {
            if (typeof fn !== "function") {
                throw new TypeError("Sloppy TestServices seed callback must be a function.");
            }
            await withDb(kind, connectionString, (db) => fn(db));
        },
        async reset(resetOptions = {}) {
            if (!isPlainObject(resetOptions)) {
                throw new TypeError("Sloppy TestServices reset options must be a plain object.");
            }
            const rerun = resetOptions.rerunMigrations === true || resetOptions.migrate === true;
            const selectedMigrations = resetOptions.migrations === undefined
                ? migrations
                : normalizeMigrationList(resetOptions.migrations);
            if (kind === "sqlserver") {
                await resetSqlServerDatabase(options, port);
                if (rerun) {
                    await withDb(kind, connectionString, async (db) => {
                        for (const path of selectedMigrations) {
                            await applyMigrationPath(db, kind, path);
                        }
                    });
                }
                return;
            }
            await withDb(kind, connectionString, async (db) => {
                for (const sqlText of resetSql(kind)) {
                    await db.exec(sqlText, []);
                }
                if (rerun) {
                    for (const path of selectedMigrations) {
                        await applyMigrationPath(db, kind, path);
                    }
                }
            });
        },
        provider() {
            const provider = openProvider(kind, connectionString);
            ownedProviders.add(provider);
            return provider;
        },
        env(prefix = undefined) {
            if (kind === "postgres") {
                return envWithPrefix({
                    POSTGRES_HOST: LOCALHOST,
                    POSTGRES_PORT: String(port),
                    POSTGRES_USER: options.username,
                    POSTGRES_PASSWORD: options.password,
                    POSTGRES_DB: options.database,
                    DATABASE_URL: connectionString,
                }, prefix);
            }
            return envWithPrefix({
                SQLSERVER_HOST: LOCALHOST,
                SQLSERVER_PORT: String(port),
                SQLSERVER_USER: options.username,
                SQLSERVER_PASSWORD: options.password,
                SQLSERVER_DATABASE: options.database,
                SQLSERVER_DRIVER: options.driver,
                SQLSERVER_CONNECTION_STRING: connectionString,
            }, prefix);
        },
        async logs(logOptions = {}) {
            const tail = normalizePort(logOptions.tail, DEFAULT_LOG_TAIL, "logs tail");
            const logs = await dockerLogs(backend, state.containerId, tail, options);
            state.logTail = redactWithSecrets(logs, state.secrets);
            return state.logTail;
        },
        diagnostics() {
            return safeObject({
                kind,
                image: options.image,
                containerId: state.containerId?.slice(0, 12),
                containerName: options.containerName,
                host: LOCALHOST,
                port,
                startupState: state.startupState,
                readinessAttempts: state.readinessAttempts,
                lastReadinessError: state.lastReadinessError,
                cleanupErrors: state.cleanupErrors,
                logTail: state.logTail,
                timings: state.timings,
                provider: {
                    kind,
                    placeholderStyle: providerPlaceholder(kind),
                    nativeStdlibBridge: providerBridgeAvailable(kind),
                },
            }, state.secrets);
        },
    };
    if (ASYNC_DISPOSE !== undefined) {
        service[ASYNC_DISPOSE] = service.dispose;
    }
    return Object.freeze(service);
}

function startupFailureMessage(kind, options, state, reason) {
    const provider = kind === "postgres" ? "PostgreSQL" : "SQL Server";
    const logTail = redactWithSecrets(state.logTail, state.secrets);
    const lastReadinessError = redactWithSecrets(state.lastReadinessError ?? reason, state.secrets);
    return `SLOPPY_E_TESTSERVICES_STARTUP_FAILED: ${provider} TestServices container did not become ready.

Image:
  ${options.image}

Container:
  ${state.containerId === undefined ? options.containerName : `${options.containerName} (${state.containerId.slice(0, 12)})`}

Mapped port:
  ${state.port ?? "unknown"}

Reason:
  ${lastReadinessError}

Cleanup failures:
${state.cleanupErrors.length === 0 ? "  <none>" : state.cleanupErrors.map((entry) => `  - ${entry.operation}: ${entry.message}`).join("\n")}

Docker logs tail:
${logTail.length === 0 ? "  <empty>" : logTail}

Suggested checks:
  - Docker is running.
  - The image can be pulled.
  - The mapped port is not blocked.
  - SQL Server startup can be slow on cold machines.`;
}

async function startService(kind, rawOptions) {
    const options = kind === "postgres"
        ? normalizedPostgresOptions(rawOptions)
        : normalizedSqlServerOptions(rawOptions);
    const backend = dockerBackend(options);
    const state = createDiagnosticState(kind, options.image, options.containerName, [options.password]);
    let connectionString = undefined;
    try {
        await dockerRequire({ dockerBackend: backend });
        if (!providerBridgeAvailable(kind)) {
            throw providerUnavailableError(kind);
        }
        await ensureImage(backend, options.image, options);
        const create = await dockerRunOk(backend, containerCreateArgs(kind, options), {
            timeoutMs: options.dockerTimeoutMs ?? 30000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        state.containerId = create.stdout.trim();
        state.startupState = "starting";
        state.timings.startedAt = new Date().toISOString();
        await dockerRunOk(backend, ["start", state.containerId], {
            timeoutMs: options.dockerTimeoutMs ?? 30000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        const inspected = await inspectContainer(
            backend,
            state.containerId,
            kind === "postgres" ? POSTGRES_PORT : SQLSERVER_PORT,
            options,
        );
        state.port = inspected.port;
        connectionString = kind === "postgres"
            ? postgresConnectionString(options, LOCALHOST, inspected.port)
            : sqlServerConnectionString(options, LOCALHOST, inspected.port);
        await waitForReady(kind, state, connectionString, options);
        if (options.migrations !== undefined) {
            const pendingService = createService(kind, options, backend, state, connectionString, inspected.port);
            await pendingService.migrate(options.migrations);
            return pendingService;
        }
        return createService(kind, options, backend, state, connectionString, inspected.port);
    } catch (error) {
        state.startupState = "failed";
        state.lastReadinessError = String(error?.message ?? error);
        state.logTail = await dockerLogs(backend, state.containerId, DEFAULT_LOG_TAIL, options);
        if (!options.keepContainerOnFailure) {
            await removeContainer(backend, state.containerId, options, state).catch(() => {});
        }
        const startupError = error?.code === "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE" ||
            error?.code === "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE"
            ? error
            : new Error(startupFailureMessage(kind, options, state, error?.message ?? error), { cause: error });
        throw startupError;
    }
}

function redisUrl(options, host, port) {
    const auth = options.password === undefined ? "" : `:${encodeURIComponent(options.password)}@`;
    return `redis://${auth}${host}:${port}/${options.database}`;
}

function redisContainerCreateArgs(options) {
    const publish = options.hostPort === undefined
        ? `${LOCALHOST}::${REDIS_PORT}`
        : `${LOCALHOST}:${options.hostPort}:${REDIS_PORT}`;
    const args = [
        "create",
        "--name",
        options.containerName,
        "-p",
        publish,
        options.image,
    ];
    if (options.password !== undefined) {
        args.push("redis-server", "--requirepass", options.password);
    }
    return args;
}

async function waitForRedisReady(state, url, options) {
    const startedAt = Date.now();
    state.startupState = "waiting";
    while (Date.now() - startedAt < options.readinessTimeoutMs) {
        const remainingMs = options.readinessTimeoutMs - (Date.now() - startedAt);
        state.readinessAttempts += 1;
        let client;
        try {
            client = Redis.client("testservices-readiness", {
                url,
                database: options.database,
                connectTimeoutMs: Math.min(1000, Math.max(1, remainingMs)),
                commandTimeoutMs: Math.min(1000, Math.max(1, remainingMs)),
                pool: { maxConnections: 1, pendingQueueLimit: 1 },
            });
            await client.ping();
            state.startupState = "ready";
            state.timings.readyAt = new Date().toISOString();
            return;
        } catch (error) {
            state.lastReadinessError = String(error?.message ?? error);
            const retryDelayMs = Math.min(500, Math.max(0, options.readinessTimeoutMs - (Date.now() - startedAt)));
            if (retryDelayMs > 0) {
                await sleep(retryDelayMs);
            }
        } finally {
            await client?.dispose?.().catch(() => {});
        }
    }
    throw new Error(`Redis readiness timed out after ${options.readinessTimeoutMs}ms: ${state.lastReadinessError ?? "no readiness result"}`);
}

function createRedisService(options, backend, state, url, port) {
    const ownedClients = new Set();
    let disposed = false;
    state.port = port;
    const service = {
        kind: "redis",
        id: state.containerId,
        image: options.image,
        host: LOCALHOST,
        port,
        url,
        connectionString: url,
        async start() {
            return service;
        },
        async stop() {
            await service.dispose();
        },
        client(name = "test") {
            const client = Redis.client(name, {
                url,
                database: options.database,
                connectTimeoutMs: 2000,
                commandTimeoutMs: 2000,
                pool: { maxConnections: 2, pendingQueueLimit: 8 },
            });
            ownedClients.add(client);
            return client;
        },
        async flush() {
            const client = service.client("testservices-flush");
            try {
                await client.command("FLUSHDB");
            } finally {
                await client.dispose();
                ownedClients.delete(client);
            }
        },
        async reset() {
            await service.flush();
        },
        env(prefix = undefined) {
            const values = {
                REDIS_HOST: LOCALHOST,
                REDIS_PORT: String(port),
                REDIS_DATABASE: String(options.database),
                REDIS_URL: url,
                "Redis:Url": url,
                "Sloppy__Redis__main__url": url,
            };
            if (options.password !== undefined) {
                values.REDIS_PASSWORD = options.password;
            }
            return envWithPrefix(values, prefix);
        },
        async logs(logOptions = {}) {
            const tail = normalizePort(logOptions.tail, DEFAULT_LOG_TAIL, "logs tail");
            const logs = await dockerLogs(backend, state.containerId, tail, options);
            state.logTail = redactWithSecrets(logs, state.secrets);
            return state.logTail;
        },
        diagnostics() {
            return safeObject({
                kind: "redis",
                image: options.image,
                containerId: state.containerId?.slice(0, 12),
                containerName: options.containerName,
                host: LOCALHOST,
                port,
                url,
                database: options.database,
                startupState: state.startupState,
                readinessAttempts: state.readinessAttempts,
                lastReadinessError: state.lastReadinessError,
                cleanupErrors: state.cleanupErrors,
                logTail: state.logTail,
                timings: state.timings,
            }, state.secrets);
        },
        async dispose() {
            if (disposed) {
                return;
            }
            state.startupState = "disposing";
            for (const client of ownedClients) {
                await client.dispose().catch(() => {});
            }
            ownedClients.clear();
            await removeContainer(backend, state.containerId, options, state);
            disposed = true;
            state.startupState = "disposed";
            state.timings.disposedAt = new Date().toISOString();
        },
    };
    if (ASYNC_DISPOSE !== undefined) {
        service[ASYNC_DISPOSE] = service.dispose;
    }
    return Object.freeze(service);
}

async function startRedisService(rawOptions) {
    const options = normalizedRedisOptions(rawOptions);
    const backend = dockerBackend(options);
    const state = createDiagnosticState("redis", options.image, options.containerName, [options.password].filter(Boolean));
    try {
        await dockerRequire({ dockerBackend: backend });
        await ensureImage(backend, options.image, options);
        const create = await dockerRunOk(backend, redisContainerCreateArgs(options), {
            timeoutMs: options.dockerTimeoutMs ?? 30000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        state.containerId = create.stdout.trim();
        state.startupState = "starting";
        state.timings.startedAt = new Date().toISOString();
        await dockerRunOk(backend, ["start", state.containerId], {
            timeoutMs: options.dockerTimeoutMs ?? 30000,
            maxStdoutBytes: 64 * 1024,
            maxStderrBytes: 64 * 1024,
        });
        const inspected = await inspectContainer(backend, state.containerId, REDIS_PORT, options);
        const url = redisUrl(options, LOCALHOST, inspected.port);
        await waitForRedisReady(state, url, options);
        return createRedisService(options, backend, state, url, inspected.port);
    } catch (error) {
        state.startupState = "failed";
        state.lastReadinessError = String(error?.message ?? error);
        state.logTail = await dockerLogs(backend, state.containerId, DEFAULT_LOG_TAIL, options);
        if (!options.keepContainerOnFailure) {
            await removeContainer(backend, state.containerId, options, state).catch(() => {});
        }
        if (error?.code === "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE") {
            throw error;
        }
        throw new Error(`SLOPPY_E_TESTSERVICES_STARTUP_FAILED: Redis TestServices container did not become ready.

Image:
  ${options.image}

Container:
  ${state.containerId === undefined ? options.containerName : `${options.containerName} (${state.containerId.slice(0, 12)})`}

Mapped port:
  ${state.port ?? "unknown"}

Reason:
  ${redactWithSecrets(state.lastReadinessError, state.secrets)}

Docker logs tail:
${state.logTail.length === 0 ? "  <empty>" : redactWithSecrets(state.logTail, state.secrets)}`, { cause: error });
    }
}

const TestServices = Object.freeze({
    docker: Object.freeze({
        available: dockerAvailable,
        require: dockerRequire,
    }),
    postgres(options = {}) {
        return startService("postgres", options);
    },
    sqlServer(options = {}) {
        return startService("sqlserver", options);
    },
    redis(options = {}) {
        return startRedisService(options);
    },
});

export { DockerCliBackend, TestServices };
