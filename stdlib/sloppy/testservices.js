import { Text } from "./codec.js";
import { Random } from "./crypto.js";
import { data, Migrations } from "./data.js";
import { File } from "./fs.js";
import { Process as SloppyProcess } from "./os.js";

const DEFAULT_POSTGRES_IMAGE = "postgres:17";
const DEFAULT_SQLSERVER_IMAGE = "mcr.microsoft.com/mssql/server:2022-latest";
const LOCALHOST = "127.0.0.1";
const POSTGRES_PORT = 5432;
const SQLSERVER_PORT = 1433;
const DEFAULT_STARTUP_TIMEOUT_MS = 30000;
const DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS = 60000;
const DEFAULT_STOP_TIMEOUT_MS = 10000;
const DEFAULT_LOG_TAIL = 120;
const SECRET_REDACTION = "[REDACTED]";
const ASYNC_DISPOSE = Symbol.asyncDispose;

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

function normalizeNonEmptyString(value, fallback, subject) {
    const selected = value ?? fallback;
    if (typeof selected !== "string" || selected.length === 0 || selected.includes("\0")) {
        throw new TypeError(`Sloppy TestServices ${subject} must be a non-empty string without NUL.`);
    }
    return selected;
}

function randomHex(length) {
    return Array.from(Random.bytes(length), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function randomContainerName(kind) {
    return `sloppy-testservices-${kind}-${randomHex(6)}`;
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

async function removeContainer(backend, containerId, options) {
    if (containerId === undefined) {
        return;
    }
    await backend.run(["stop", "--time", String(Math.ceil((options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS) / 1000)), containerId], {
        timeoutMs: options.stopTimeoutMs ?? DEFAULT_STOP_TIMEOUT_MS,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    }).catch(() => {});
    await backend.run(["rm", "--force", containerId], {
        timeoutMs: options.rmTimeoutMs ?? 15000,
        maxStdoutBytes: 64 * 1024,
        maxStderrBytes: 64 * 1024,
    }).catch(() => {});
}

function postgresConnectionString(options, host, port) {
    const user = encodeURIComponent(options.username);
    const password = encodeURIComponent(options.password);
    const database = encodeURIComponent(options.database);
    return `postgresql://${user}:${password}@${host}:${port}/${database}`;
}

function sqlServerConnectionString(options, host, port) {
    return [
        "Driver={ODBC Driver 18 for SQL Server}",
        `Server=${host},${port}`,
        `Database=${options.database}`,
        `UID=${options.username}`,
        `PWD=${options.password}`,
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
        hostPort: normalizePort(options.hostPort, undefined, "PostgreSQL hostPort"),
        startupTimeoutMs: normalizeTimeout(options.startupTimeoutMs, DEFAULT_STARTUP_TIMEOUT_MS, "PostgreSQL startupTimeoutMs"),
        stopTimeoutMs: normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "PostgreSQL stopTimeoutMs"),
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
    const password = options.password === undefined
        ? generatedSqlServerPassword()
        : normalizeNonEmptyString(options.password, undefined, "SQL Server password");
    const database = normalizeNonEmptyString(options.database, "app_test", "SQL Server database");
    const username = normalizeNonEmptyString(options.username, "sa", "SQL Server username");
    const image = normalizeNonEmptyString(options.image, DEFAULT_SQLSERVER_IMAGE, "SQL Server image");
    const name = options.containerName === undefined
        ? randomContainerName("sqlserver")
        : normalizeNonEmptyString(options.containerName, undefined, "SQL Server containerName");
    return Object.freeze({
        kind: "sqlserver",
        image,
        username,
        password,
        database,
        host: LOCALHOST,
        containerName: name,
        hostPort: normalizePort(options.hostPort, undefined, "SQL Server hostPort"),
        startupTimeoutMs: normalizeTimeout(options.startupTimeoutMs, DEFAULT_SQLSERVER_STARTUP_TIMEOUT_MS, "SQL Server startupTimeoutMs"),
        stopTimeoutMs: normalizeTimeout(options.stopTimeoutMs, DEFAULT_STOP_TIMEOUT_MS, "SQL Server stopTimeoutMs"),
        keepContainerOnFailure: options.keepContainerOnFailure === true,
        migrations: options.migrations,
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
        state.readinessAttempts += 1;
        try {
            if (kind === "sqlserver") {
                const masterConnectionString = sqlServerConnectionString({
                    ...options,
                    database: "master",
                }, LOCALHOST, state.port);
                await withDb(kind, masterConnectionString, async (db) => {
                    await db.exec(`if db_id(N'${options.database.replaceAll("'", "''")}') is null create database [${options.database.replaceAll("]", "]]")}]`, []);
                });
            }
            await withDb(kind, connectionString, async (db) => {
                await db.queryOne("select 1 as ok", []);
            });
            state.startupState = "ready";
            state.timings.readyAt = new Date().toISOString();
            return;
        } catch (error) {
            state.lastReadinessError = String(error?.message ?? error);
            await sleep(Math.min(1000, 100 + state.readinessAttempts * 100));
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
    return kind === "postgres" ? "postgres" : "question";
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
    }).sort((left, right) => left.localeCompare(right, "en"));
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
        return "drop schema if exists public cascade; create schema public;";
    }
    return `
declare @sql nvarchar(max) = N'';
select @sql = @sql + N'alter table ' + quotename(object_schema_name(parent_object_id)) + N'.' + quotename(object_name(parent_object_id)) + N' drop constraint ' + quotename(name) + N';'
from sys.foreign_keys;
exec sp_executesql @sql;
set @sql = N'';
select @sql = @sql + N'drop table ' + quotename(schema_name(schema_id)) + N'.' + quotename(name) + N';'
from sys.tables where is_ms_shipped = 0;
exec sp_executesql @sql;`;
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
            disposed = true;
            state.startupState = "disposing";
            for (const provider of ownedProviders) {
                await Promise.resolve(provider.close?.()).catch(() => {});
            }
            ownedProviders.clear();
            await removeContainer(backend, state.containerId, options);
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
            await withDb(kind, connectionString, async (db) => {
                await db.exec(resetSql(kind), []);
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
        const startupError = error?.code === "SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE" ||
            error?.code === "SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE"
            ? error
            : new Error(startupFailureMessage(kind, options, state, error?.message ?? error), { cause: error });
        if (!options.keepContainerOnFailure) {
            await removeContainer(backend, state.containerId, options);
        }
        throw startupError;
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
});

export { DockerCliBackend, TestServices };
