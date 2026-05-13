import { Random } from "./crypto.js";
import { CancellationController } from "./time.js";

const JOB_STATUSES = Object.freeze({
    scheduled: true,
    queued: true,
    processing: true,
    succeeded: true,
    failed: true,
    retrying: true,
    dead: true,
    cancelled: true,
    deleted: true,
});

const VALID_TRANSITIONS = Object.freeze({
    scheduled: Object.freeze(["queued", "cancelled"]),
    queued: Object.freeze(["processing", "cancelled", "deleted"]),
    processing: Object.freeze(["succeeded", "failed", "cancelled"]),
    failed: Object.freeze(["retrying", "dead", "queued"]),
    retrying: Object.freeze(["queued", "cancelled"]),
    dead: Object.freeze(["queued", "deleted"]),
    succeeded: Object.freeze(["deleted"]),
    cancelled: Object.freeze(["deleted"]),
    deleted: Object.freeze([]),
});

const DEFAULT_QUEUE = "default";
const DEFAULT_PAGE_SIZE = 50;
const MAX_PAGE_SIZE = 500;
const DEFAULT_LOCK_TTL_MS = 30000;
const DEFAULT_WORKER = Object.freeze({
    queues: Object.freeze([DEFAULT_QUEUE]),
    concurrency: 1,
    pollIntervalMs: 1000,
    idleBackoffMs: 5000,
    heartbeatIntervalMs: 5000,
    leaseMs: 30000,
    shutdownTimeoutMs: 30000,
});
const REDACT_KEYS = Object.freeze([
    "password",
    "token",
    "secret",
    "authorization",
    "cookie",
    "key",
]);

let jitterCounter = 0;
const PROVIDERS = Object.freeze({
    sqlite: true,
    postgres: true,
    sqlserver: true,
});
const SCHEMA_VERSION = 1;

class SloppyJobsError extends Error {
    constructor(code, message, details = undefined) {
        super(`${code}: ${message}`);
        this.name = "SloppyJobsError";
        this.code = code;
        if (details !== undefined) {
            this.details = Object.freeze({ ...details });
        }
    }
}

function jobsError(code, message, details = undefined) {
    return new SloppyJobsError(code, message, details);
}

function terminalPersistenceRace(error) {
    return error instanceof SloppyJobsError &&
        (error.code === "SLOPPY_E_JOBS_TRANSITION_INVALID" || error.code === "SLOPPY_E_JOBS_STALE_LEASE");
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function requirePlainObject(value, subject) {
    if (!isPlainObject(value)) {
        throw new TypeError(`${subject} must be a plain object.`);
    }
    return value;
}

function requireName(value, subject) {
    if (
        typeof value !== "string" ||
        value.length === 0 ||
        value.trim() !== value ||
        value.length > 128 ||
        /[\0\r\n]/u.test(value)
    ) {
        throw new TypeError(`${subject} must be a non-empty stable string.`);
    }
    return value;
}

function optionalName(value, subject, fallback = undefined) {
    if (value === undefined || value === null) {
        return fallback;
    }
    return requireName(value, subject);
}

function integer(value, subject, fallback = undefined) {
    if (value === undefined) {
        if (fallback !== undefined) {
            return fallback;
        }
        throw new TypeError(`${subject} is required.`);
    }
    if (!Number.isInteger(value)) {
        throw new TypeError(`${subject} must be an integer.`);
    }
    return value;
}

function positiveInteger(value, subject, fallback = undefined) {
    const resolved = integer(value, subject, fallback);
    if (resolved <= 0) {
        throw new TypeError(`${subject} must be a positive integer.`);
    }
    return resolved;
}

function nonNegativeInteger(value, subject, fallback = undefined) {
    const resolved = integer(value, subject, fallback);
    if (resolved < 0) {
        throw new TypeError(`${subject} must be a non-negative integer.`);
    }
    return resolved;
}

function nowIso(clock = undefined) {
    const value = clock && typeof clock.now === "function" ? clock.now() : new Date();
    return value instanceof Date ? value.toISOString() : new Date(value).toISOString();
}

function addMsIso(iso, ms) {
    return new Date(new Date(iso).getTime() + ms).toISOString();
}

function toIso(value, subject) {
    if (value === undefined || value === null) {
        return undefined;
    }
    const date = value instanceof Date ? value : new Date(value);
    if (!Number.isFinite(date.getTime())) {
        throw new TypeError(`${subject} must be a valid Date or ISO timestamp.`);
    }
    return date.toISOString();
}

function cryptoUnavailable(error) {
    return String(error?.message ?? error ?? "").includes("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE");
}

function webCryptoRandomHex(byteLength) {
    const cryptoApi = globalThis.crypto;
    if (cryptoApi === undefined || typeof cryptoApi.getRandomValues !== "function") {
        throw jobsError(
            "SLOPPY_E_JOBS_RANDOM_UNAVAILABLE",
            "durable scheduler IDs require Sloppy Random or Web Crypto randomness",
        );
    }
    const bytes = new Uint8Array(byteLength);
    cryptoApi.getRandomValues(bytes);
    return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function randomHex(byteLength) {
    try {
        return Random.hex(byteLength);
    } catch (error) {
        if (!cryptoUnavailable(error)) {
            throw error;
        }
    }
    return webCryptoRandomHex(byteLength);
}

function stableId(prefix = "job") {
    return `${prefix}_${randomHex(16)}`;
}

function databaseClockSql(provider) {
    if (provider === "sqlite") {
        return "select strftime('%Y-%m-%dT%H:%M:%fZ', 'now') as now";
    }
    if (provider === "postgres") {
        return "select to_char(clock_timestamp() at time zone 'utc', 'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"') as now";
    }
    return "select convert(varchar(23), sysutcdatetime(), 126) + 'Z' as now";
}

function databaseTimestamp(row) {
    const value = rowField(row ?? {}, "now", "NOW");
    if (value === undefined || value === null) {
        throw jobsError("SLOPPY_E_JOBS_CLOCK_UNAVAILABLE", "database clock query did not return a timestamp");
    }
    return toIso(value, "Sloppy Jobs database clock");
}

function sleep(ms, signal = undefined) {
    if (ms <= 0) {
        return Promise.resolve();
    }
    if (signal?.aborted) {
        return Promise.resolve();
    }
    return new Promise((resolve) => {
        let timer;
        let onAbort;
        const cleanup = () => {
            if (signal && typeof signal.removeEventListener === "function" && onAbort !== undefined) {
                signal.removeEventListener("abort", onAbort);
            }
        };
        timer = setTimeout(() => {
            cleanup();
            resolve();
        }, ms);
        if (signal && typeof signal.addEventListener === "function") {
            onAbort = () => {
                if (typeof clearTimeout === "function") {
                    clearTimeout(timer);
                }
                cleanup();
                resolve();
            };
            signal.addEventListener("abort", onAbort);
        }
    });
}

async function runWithTimeout(handler, context, input, timeoutMs, parentSignal) {
    if (timeoutMs === null || timeoutMs === undefined || typeof setTimeout !== "function") {
        return await handler(context, input);
    }
    const controller = new CancellationController({ signal: parentSignal });
    let timer;
    const timeout = new Promise((_, reject) => {
        timer = setTimeout(() => {
            const error = jobsError("SLOPPY_E_JOBS_TIMEOUT", "job handler exceeded timeout", {
                jobId: context.id,
                timeoutMs,
            });
            try {
                controller.cancel(error);
            } catch {
                // The timeout rejection below remains authoritative.
            }
            reject(error);
        }, timeoutMs);
    });
    try {
        return await Promise.race([
            Promise.resolve().then(() => handler(Object.freeze({ ...context, signal: controller.signal }), input)),
            timeout,
        ]);
    } finally {
        if (typeof clearTimeout === "function") {
            clearTimeout(timer);
        }
        controller.dispose();
    }
}

function jsonStringify(value, subject) {
    try {
        return JSON.stringify(value ?? null);
    } catch (error) {
        throw jobsError("SLOPPY_E_JOBS_INVALID_PAYLOAD", `${subject} must be JSON-serializable`, {
            cause: String(error?.message ?? error),
        });
    }
}

function jsonParse(text, fallback = null) {
    if (text === undefined || text === null || text === "") {
        return fallback;
    }
    if (typeof text !== "string") {
        return text;
    }
    try {
        return JSON.parse(text);
    } catch {
        return fallback;
    }
}

function redactValue(value, keys = REDACT_KEYS) {
    if (Array.isArray(value)) {
        return Object.freeze(value.map((item) => redactValue(item, keys)));
    }
    if (!isPlainObject(value)) {
        return value;
    }
    const redacted = {};
    for (const [key, item] of Object.entries(value)) {
        const lower = key.toLowerCase();
        redacted[key] = keys.some((candidate) => lower.includes(candidate))
            ? "<redacted>"
            : redactValue(item, keys);
    }
    return Object.freeze(redacted);
}

function payloadPreview(payloadJson, keys = REDACT_KEYS) {
    return redactValue(jsonParse(payloadJson, null), keys);
}

function normalizeProvider(provider) {
    if (PROVIDERS[provider] !== true) {
        throw new TypeError("Sloppy Jobs storage provider must be sqlite, postgres, or sqlserver.");
    }
    return provider;
}

function providerFromConnection(db, explicit = undefined) {
    if (explicit !== undefined) {
        return normalizeProvider(explicit);
    }
    const debug = typeof db?.__debug === "function" ? db.__debug() : undefined;
    if (debug?.kind === "sqlite-connection") {
        return "sqlite";
    }
    if (debug?.kind === "postgres-connection") {
        return "postgres";
    }
    if (debug?.kind === "sqlserver-connection") {
        return "sqlserver";
    }
    throw new TypeError("Sloppy Jobs storage requires a sqlite, postgres, or sqlserver data connection.");
}

function placeholder(provider, index) {
    if (provider === "postgres") {
        return `$${index}`;
    }
    return "?";
}

function placeholders(provider, count, start = 1) {
    return Array.from({ length: count }, (_, index) => placeholder(provider, start + index));
}

function uniqueConflict(error) {
    const code = String(error?.code ?? error?.number ?? error?.sqlState ?? "");
    const message = String(error?.message ?? error ?? "").toLowerCase();
    return code === "23505" ||
        code === "2627" ||
        code === "2601" ||
        code === "SQLITE_CONSTRAINT" ||
        message.includes("unique constraint") ||
        message.includes("duplicate key") ||
        message.includes("unique index");
}

function busyConflict(error) {
    const message = String(error?.message ?? error ?? "").toLowerCase();
    return message.includes("database is locked") ||
        message.includes("database table is locked") ||
        message.includes("busy");
}

async function retryBusy(operation) {
    let attempts = 0;
    for (;;) {
        try {
            return await operation();
        } catch (error) {
            attempts += 1;
            if (attempts >= 25 || !busyConflict(error)) {
                throw error;
            }
            await sleep(0);
        }
    }
}

function insertOrIgnoreSql(provider, table, columns, conflictColumns = undefined) {
    const values = placeholders(provider, columns.length).join(", ");
    const columnList = columns.join(", ");
    if (provider === "sqlite") {
        return `insert or ignore into ${table} (${columnList}) values (${values})`;
    }
    if (provider === "postgres") {
        const conflict = Array.isArray(conflictColumns) && conflictColumns.length > 0
            ? ` on conflict (${conflictColumns.join(", ")})${conflictColumns.includes("idempotency_key") ? " where idempotency_key is not null" : ""} do nothing`
            : " on conflict do nothing";
        return `insert into ${table} (${columnList}) values (${values})${conflict}`;
    }
    return `insert into ${table} (${columnList}) values (${values})`;
}

function compactRows(rows) {
    if (Array.isArray(rows)) {
        return rows;
    }
    if (Array.isArray(rows?.rows)) {
        return rows.rows;
    }
    return [];
}

function rowField(row, ...names) {
    for (const name of names) {
        if (Object.prototype.hasOwnProperty.call(row, name)) {
            return row[name];
        }
    }
    const entries = Object.entries(row);
    for (const name of names) {
        const match = entries.find(([key]) => key.toLowerCase() === name.toLowerCase());
        if (match !== undefined) {
            return match[1];
        }
    }
    return undefined;
}

async function queryOne(db, sql, params = []) {
    if (typeof db.queryOne === "function") {
        return await retryBusy(() => db.queryOne(sql, params));
    }
    const rows = compactRows(await retryBusy(() => db.query(sql, params, { maxRows: 1 })));
    return rows[0] ?? null;
}

async function execSql(db, sql, params = []) {
    if (typeof db.exec === "function") {
        return await retryBusy(() => db.exec(sql, params));
    }
    return await retryBusy(() => db.query(sql, params));
}

async function inTransaction(db, callback) {
    if (typeof db.transaction === "function") {
        return await db.transaction(callback);
    }
    return await callback(db);
}

function schemaSql(provider) {
    const id = "text primary key";
    const ts = "text";
    const text = "text";
    const short = "text";
    const integerType = provider === "postgres" ? "bigint" : "integer";
    if (provider === "sqlserver") {
        return Object.freeze([
            "if object_id(N'dbo.sloppy_job_schema', N'U') is null create table dbo.sloppy_job_schema (version bigint not null, updated_at nvarchar(64) not null)",
            "if object_id(N'dbo.sloppy_jobs', N'U') is null create table dbo.sloppy_jobs (id nvarchar(128) not null primary key, name nvarchar(256) not null, queue nvarchar(256) not null, status nvarchar(64) not null, payload_json nvarchar(max) not null, payload_schema nvarchar(256), priority bigint not null, run_at nvarchar(64) not null, created_at nvarchar(64) not null, updated_at nvarchar(64) not null, locked_by nvarchar(256), locked_until nvarchar(64), attempt_count bigint not null, max_attempts bigint not null, retry_policy_json nvarchar(max) not null, next_retry_at nvarchar(64), last_error_code nvarchar(256), last_error_message nvarchar(max), diagnostic_id nvarchar(256), correlation_id nvarchar(256), idempotency_key nvarchar(256), timeout_ms bigint, metadata_json nvarchar(max) not null)",
            "if object_id(N'dbo.sloppy_job_attempts', N'U') is null create table dbo.sloppy_job_attempts (id nvarchar(128) not null primary key, job_id nvarchar(128) not null, worker_id nvarchar(256), attempt_number bigint not null, started_at nvarchar(64) not null, finished_at nvarchar(64), status nvarchar(64) not null, duration_ms bigint, error_code nvarchar(256), error_message nvarchar(max), diagnostic_id nvarchar(256))",
            "if object_id(N'dbo.sloppy_recurring_jobs', N'U') is null create table dbo.sloppy_recurring_jobs (id nvarchar(128) not null primary key, name nvarchar(256) not null, job_name nvarchar(256) not null, queue nvarchar(256) not null, cron nvarchar(128) not null, timezone nvarchar(128) not null, payload_json nvarchar(max) not null, enabled bigint not null, misfire_policy nvarchar(64) not null, last_run_at nvarchar(64), next_run_at nvarchar(64) not null, created_at nvarchar(64) not null, updated_at nvarchar(64) not null, metadata_json nvarchar(max) not null)",
            "if object_id(N'dbo.sloppy_job_workers', N'U') is null create table dbo.sloppy_job_workers (id nvarchar(128) not null primary key, worker_name nvarchar(256) not null, host nvarchar(256), pid bigint, queues nvarchar(max) not null, started_at nvarchar(64) not null, last_heartbeat_at nvarchar(64) not null, status nvarchar(64) not null)",
            "if object_id(N'dbo.sloppy_job_locks', N'U') is null create table dbo.sloppy_job_locks (name nvarchar(256) not null primary key, owner nvarchar(256) not null, locked_until nvarchar(64) not null, updated_at nvarchar(64) not null)",
            "if object_id(N'dbo.sloppy_job_events', N'U') is null create table dbo.sloppy_job_events (id nvarchar(128) not null primary key, job_id nvarchar(128), event_type nvarchar(256) not null, from_status nvarchar(64), to_status nvarchar(64), created_at nvarchar(64) not null, worker_id nvarchar(256), message nvarchar(max), data_json nvarchar(max) not null)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_jobs_claim' and object_id = object_id(N'dbo.sloppy_jobs')) create index idx_sloppy_jobs_claim on dbo.sloppy_jobs (queue, status, run_at, locked_until, priority)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_jobs_status' and object_id = object_id(N'dbo.sloppy_jobs')) create index idx_sloppy_jobs_status on dbo.sloppy_jobs (status, updated_at)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_jobs_name' and object_id = object_id(N'dbo.sloppy_jobs')) create index idx_sloppy_jobs_name on dbo.sloppy_jobs (name, created_at)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_jobs_worker' and object_id = object_id(N'dbo.sloppy_jobs')) create index idx_sloppy_jobs_worker on dbo.sloppy_jobs (locked_by, locked_until)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_job_attempts_job' and object_id = object_id(N'dbo.sloppy_job_attempts')) create index idx_sloppy_job_attempts_job on dbo.sloppy_job_attempts (job_id, attempt_number)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_job_events_job' and object_id = object_id(N'dbo.sloppy_job_events')) create index idx_sloppy_job_events_job on dbo.sloppy_job_events (job_id, created_at)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_recurring_due' and object_id = object_id(N'dbo.sloppy_recurring_jobs')) create index idx_sloppy_recurring_due on dbo.sloppy_recurring_jobs (enabled, next_run_at)",
            "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_workers_status' and object_id = object_id(N'dbo.sloppy_job_workers')) create index idx_sloppy_workers_status on dbo.sloppy_job_workers (status, last_heartbeat_at)",
            "if not exists (select 1 from sys.indexes where name = N'ux_sloppy_jobs_idempotency' and object_id = object_id(N'dbo.sloppy_jobs')) create unique index ux_sloppy_jobs_idempotency on dbo.sloppy_jobs (idempotency_key) where idempotency_key is not null",
            "if not exists (select 1 from sys.indexes where name = N'ux_sloppy_recurring_name' and object_id = object_id(N'dbo.sloppy_recurring_jobs')) create unique index ux_sloppy_recurring_name on dbo.sloppy_recurring_jobs (name)",
        ]);
    }
    const schema = [
        `create table if not exists sloppy_job_schema (version ${integerType} not null, updated_at ${ts} not null)`,
        `create table if not exists sloppy_jobs (` +
            `id ${id}, name ${short} not null, queue ${short} not null, status ${short} not null, ` +
            `payload_json ${text} not null, payload_schema ${short}, priority ${integerType} not null, ` +
            `run_at ${ts} not null, created_at ${ts} not null, updated_at ${ts} not null, ` +
            `locked_by ${short}, locked_until ${ts}, attempt_count ${integerType} not null, ` +
            `max_attempts ${integerType} not null, retry_policy_json ${text} not null, next_retry_at ${ts}, ` +
            `last_error_code ${short}, last_error_message ${text}, diagnostic_id ${short}, correlation_id ${short}, ` +
            `idempotency_key ${short}, timeout_ms ${integerType}, metadata_json ${text} not null)`,
        `create table if not exists sloppy_job_attempts (` +
            `id ${id}, job_id ${short} not null, worker_id ${short}, attempt_number ${integerType} not null, ` +
            `started_at ${ts} not null, finished_at ${ts}, status ${short} not null, duration_ms ${integerType}, ` +
            `error_code ${short}, error_message ${text}, diagnostic_id ${short})`,
        `create table if not exists sloppy_recurring_jobs (` +
            `id ${id}, name ${short} not null, job_name ${short} not null, queue ${short} not null, cron ${short} not null, ` +
            `timezone ${short} not null, payload_json ${text} not null, enabled ${integerType} not null, ` +
            `misfire_policy ${short} not null, last_run_at ${ts}, next_run_at ${ts} not null, ` +
            `created_at ${ts} not null, updated_at ${ts} not null, metadata_json ${text} not null)`,
        `create table if not exists sloppy_job_workers (` +
            `id ${id}, worker_name ${short} not null, host ${short}, pid ${integerType}, queues ${text} not null, ` +
            `started_at ${ts} not null, last_heartbeat_at ${ts} not null, status ${short} not null)`,
        `create table if not exists sloppy_job_locks (` +
            `name ${id}, owner ${short} not null, locked_until ${ts} not null, updated_at ${ts} not null)`,
        `create table if not exists sloppy_job_events (` +
            `id ${id}, job_id ${short}, event_type ${short} not null, from_status ${short}, to_status ${short}, ` +
            `created_at ${ts} not null, worker_id ${short}, message ${text}, data_json ${text} not null)`,
    ];
    const indexes = [
        "create index if not exists idx_sloppy_jobs_claim on sloppy_jobs (queue, status, run_at, locked_until, priority)",
        "create index if not exists idx_sloppy_jobs_status on sloppy_jobs (status, updated_at)",
        "create index if not exists idx_sloppy_jobs_name on sloppy_jobs (name, created_at)",
        "create index if not exists idx_sloppy_jobs_worker on sloppy_jobs (locked_by, locked_until)",
        "create index if not exists idx_sloppy_job_attempts_job on sloppy_job_attempts (job_id, attempt_number)",
        "create index if not exists idx_sloppy_job_events_job on sloppy_job_events (job_id, created_at)",
        "create index if not exists idx_sloppy_recurring_due on sloppy_recurring_jobs (enabled, next_run_at)",
        "create index if not exists idx_sloppy_workers_status on sloppy_job_workers (status, last_heartbeat_at)",
    ];
    if (provider !== "sqlserver") {
        indexes.push(
            "create unique index if not exists ux_sloppy_jobs_idempotency on sloppy_jobs (idempotency_key) where idempotency_key is not null",
            "create unique index if not exists ux_sloppy_recurring_name on sloppy_recurring_jobs (name)",
        );
    }
    return Object.freeze([...schema, ...indexes]);
}

function claimSelectSql(provider, queueCount, limit = undefined) {
    const queuePlaceholders = placeholders(provider, queueCount);
    const timestampPlaceholder = placeholder(provider, queueCount + 1);
    const limitPlaceholder = placeholder(provider, queueCount + 2);
    const where =
        `where status = 'queued' and queue in (${queuePlaceholders.join(", ")}) ` +
        `and run_at <= ${timestampPlaceholder}`;
    const order = "order by priority desc, run_at asc, created_at asc";
    if (provider === "postgres") {
        return `select * from sloppy_jobs ${where} ${order} for update skip locked limit ${limitPlaceholder}`;
    }
    if (provider === "sqlserver") {
        return `select top (${positiveInteger(limit, "Sloppy Jobs claim limit", DEFAULT_PAGE_SIZE)}) * from sloppy_jobs with (updlock, readpast, rowlock) ${where} ${order}`;
    }
    return `select * from sloppy_jobs ${where} ${order} limit ${limitPlaceholder}`;
}

function limitSql(provider, sql, limitPlaceholder, limit = undefined) {
    if (provider === "sqlserver") {
        return sql.replace(/^select /iu, `select top (${positiveInteger(limit, "Sloppy Jobs query limit", MAX_PAGE_SIZE)}) `);
    }
    return `${sql} limit ${limitPlaceholder}`;
}

function limitParams(provider, params, limit) {
    return provider === "sqlserver" ? params : [...params, limit];
}

function transitionAllowed(from, to) {
    return VALID_TRANSITIONS[from]?.includes(to) === true;
}

function normalizeRetryPolicy(value = undefined) {
    const policy = value === undefined ? {} : requirePlainObject(value, "Sloppy Jobs retry policy");
    const maxAttempts = positiveInteger(policy.maxAttempts, "Sloppy Jobs maxAttempts", 1);
    const backoff = policy.backoff ?? "none";
    if (!["none", "fixed", "exponential"].includes(backoff)) {
        throw new TypeError("Sloppy Jobs retry backoff must be none, fixed, or exponential.");
    }
    const initialDelayMs = nonNegativeInteger(policy.initialDelayMs ?? policy.backoffMs, "Sloppy Jobs initialDelayMs", 0);
    const maxDelayMs = nonNegativeInteger(policy.maxDelayMs, "Sloppy Jobs maxDelayMs", initialDelayMs);
    const jitter = policy.jitter === true;
    return Object.freeze({ maxAttempts, backoff, initialDelayMs, maxDelayMs, jitter });
}

function retryDelayMs(policy, attempt) {
    if (policy.backoff === "none") {
        return 0;
    }
    const base = policy.backoff === "exponential"
        ? policy.initialDelayMs * (2 ** Math.max(0, attempt - 1))
        : policy.initialDelayMs;
    const capped = Math.min(policy.maxDelayMs, base);
    if (!policy.jitter || capped <= 1) {
        return capped;
    }
    jitterCounter = (jitterCounter + 1) % 997;
    const spread = 0.5 + (jitterCounter / 1994);
    return Math.max(1, Math.floor(capped * spread));
}

function validatePayload(schema, payload, operation, jobName) {
    if (schema === undefined || schema === null) {
        return payload;
    }
    if (typeof schema.validate !== "function") {
        throw new TypeError(`Sloppy Jobs definition '${jobName}' input schema must expose validate().`);
    }
    const result = schema.validate(payload);
    if (result?.ok === true) {
        return result.value;
    }
    throw jobsError("SLOPPY_E_JOBS_INVALID_PAYLOAD", `${operation} payload failed validation`, {
        jobName,
        issues: result?.issues ?? [],
    });
}

function normalizeDefinitionOptions(options = undefined) {
    const current = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs definition options");
    const retry = normalizeRetryPolicy(current.retries ?? current.retry);
    return Object.freeze({
        input: current.input,
        queue: optionalName(current.queue, "Sloppy Jobs definition queue", DEFAULT_QUEUE),
        retries: retry,
        timeoutMs: current.timeoutMs === undefined
            ? undefined
            : positiveInteger(current.timeoutMs, "Sloppy Jobs definition timeoutMs"),
        payloadRedactionKeys: Object.freeze([
            ...REDACT_KEYS,
            ...(Array.isArray(current.payloadRedactionKeys) ? current.payloadRedactionKeys.map(String) : []),
        ]),
        metadata: Object.freeze({ ...(current.metadata ?? {}) }),
    });
}

function normalizeEnqueueOptions(definition, options = undefined) {
    const current = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs enqueue options");
    const runAt = current.runAt !== undefined
        ? toIso(current.runAt, "Sloppy Jobs runAt")
        : undefined;
    const delayMs = current.delayMs !== undefined
        ? nonNegativeInteger(current.delayMs, "Sloppy Jobs delayMs")
        : 0;
    const retry = normalizeRetryPolicy(current.retries ?? current.retry ?? definition?.retries);
    return Object.freeze({
        queue: optionalName(current.queue, "Sloppy Jobs queue", definition?.queue ?? DEFAULT_QUEUE),
        priority: integer(current.priority, "Sloppy Jobs priority", 0),
        runAt,
        delayMs,
        idempotencyKey: current.idempotencyKey === undefined
            ? undefined
            : requireName(current.idempotencyKey, "Sloppy Jobs idempotencyKey"),
        retry,
        maxAttempts: positiveInteger(current.maxAttempts, "Sloppy Jobs maxAttempts", retry.maxAttempts),
        timeoutMs: current.timeoutMs === undefined
            ? definition?.timeoutMs
            : positiveInteger(current.timeoutMs, "Sloppy Jobs timeoutMs"),
        correlationId: current.correlationId === undefined ? undefined : requireName(current.correlationId, "Sloppy Jobs correlationId"),
        metadata: Object.freeze({ ...(current.metadata ?? {}) }),
    });
}

class SchedulerStorage {
    constructor(db, options = undefined) {
        const opts = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs storage options");
        this.provider = providerFromConnection(db, opts.provider);
        if (db === null || typeof db !== "object") {
            throw new TypeError("Sloppy Jobs storage requires a data connection object.");
        }
        this.db = db;
        this.clock = opts.clock;
        Object.freeze(this);
    }

    p(index) {
        return placeholder(this.provider, index);
    }

    ps(count, start = 1) {
        return placeholders(this.provider, count, start);
    }

    async now(db = this.db) {
        if (this.clock !== undefined) {
            return nowIso(this.clock);
        }
        return databaseTimestamp(await queryOne(db, databaseClockSql(this.provider)));
    }

    async init() {
        return await inTransaction(this.db, async (tx) => {
            for (const statement of schemaSql(this.provider)) {
                await execSql(tx, statement);
            }
            const row = await queryOne(tx, "select version from sloppy_job_schema");
            if (row === null || row === undefined) {
                await execSql(tx, `insert into sloppy_job_schema (version, updated_at) values (${this.p(1)}, ${this.p(2)})`, [
                    SCHEMA_VERSION,
                    await this.now(tx),
                ]);
            } else if (Number(row.version) !== SCHEMA_VERSION) {
                throw jobsError("SLOPPY_E_JOBS_SCHEMA_VERSION_MISMATCH", "scheduler schema version is incompatible", {
                    expected: SCHEMA_VERSION,
                    actual: Number(row.version),
                });
            }
        });
    }

    async enqueue(record) {
        return await inTransaction(this.db, async (tx) => {
            const columns = [
                "id",
                "name",
                "queue",
                "status",
                "payload_json",
                "payload_schema",
                "priority",
                "run_at",
                "created_at",
                "updated_at",
                "locked_by",
                "locked_until",
                "attempt_count",
                "max_attempts",
                "retry_policy_json",
                "next_retry_at",
                "last_error_code",
                "last_error_message",
                "diagnostic_id",
                "correlation_id",
                "idempotency_key",
                "timeout_ms",
                "metadata_json",
            ];
            const params = [
                record.id,
                record.name,
                record.queue,
                record.status,
                record.payloadJson,
                record.payloadSchema ?? null,
                record.priority,
                record.runAt,
                record.createdAt,
                record.updatedAt,
                null,
                null,
                0,
                record.maxAttempts,
                jsonStringify(record.retryPolicy, "retry policy"),
                null,
                null,
                null,
                null,
                record.correlationId ?? null,
                record.idempotencyKey ?? null,
                record.timeoutMs ?? null,
                jsonStringify(record.metadata, "job metadata"),
            ];
            try {
                if (record.idempotencyKey !== undefined && this.provider === "sqlserver") {
                    await execSql(
                        tx,
                        `if not exists (select 1 from sloppy_jobs with (updlock, holdlock) where idempotency_key = ${this.p(1)}) insert into sloppy_jobs (${columns.join(", ")}) values (${this.ps(columns.length, 2).join(", ")})`,
                        [record.idempotencyKey, ...params],
                    );
                } else {
                    const sql = record.idempotencyKey === undefined
                        ? `insert into sloppy_jobs (${columns.join(", ")}) values (${this.ps(columns.length).join(", ")})`
                        : insertOrIgnoreSql(this.provider, "sloppy_jobs", columns, ["idempotency_key"]);
                    await execSql(tx, sql, params);
                }
            } catch (error) {
                if (record.idempotencyKey === undefined || !uniqueConflict(error)) {
                    throw error;
                }
            }
            const inserted = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [record.id]));
            if (inserted !== null) {
                await this.addEvent(tx, record.id, "enqueued", null, record.status, record.createdAt, null, "job enqueued", {
                    queue: record.queue,
                    runAt: record.runAt,
                });
                return inserted;
            }
            if (record.idempotencyKey !== undefined) {
                return normalizeJob(await queryOne(tx, `select * from sloppy_jobs where idempotency_key = ${this.p(1)}`, [record.idempotencyKey]));
            }
            return null;
        });
    }

    async getJob(id) {
        return normalizeJob(await queryOne(this.db, `select * from sloppy_jobs where id = ${this.p(1)}`, [id]));
    }

    async listJobs(filters = undefined) {
        const current = filters === undefined ? {} : requirePlainObject(filters, "Sloppy Jobs list filters");
        const pageSize = Math.min(MAX_PAGE_SIZE, positiveInteger(current.pageSize, "Sloppy Jobs pageSize", DEFAULT_PAGE_SIZE));
        const offset = nonNegativeInteger(current.offset, "Sloppy Jobs offset", 0);
        const where = [];
        const params = [];
        for (const [field, column] of [
            ["status", "status"],
            ["queue", "queue"],
            ["name", "name"],
            ["worker", "locked_by"],
        ]) {
            if (current[field] !== undefined) {
                where.push(`${column} = ${this.p(params.length + 1)}`);
                params.push(current[field]);
            }
        }
        if (current.failed === true) {
            where.push("status in ('failed', 'dead')");
        }
        if (current.createdFrom !== undefined) {
            where.push(`created_at >= ${this.p(params.length + 1)}`);
            params.push(toIso(current.createdFrom, "createdFrom"));
        }
        if (current.createdTo !== undefined) {
            where.push(`created_at <= ${this.p(params.length + 1)}`);
            params.push(toIso(current.createdTo, "createdTo"));
        }
        if (current.runAtFrom !== undefined) {
            where.push(`run_at >= ${this.p(params.length + 1)}`);
            params.push(toIso(current.runAtFrom, "runAtFrom"));
        }
        if (current.runAtTo !== undefined) {
            where.push(`run_at <= ${this.p(params.length + 1)}`);
            params.push(toIso(current.runAtTo, "runAtTo"));
        }
        const clause = where.length === 0 ? "" : ` where ${where.join(" and ")}`;
        const listSql = this.provider === "sqlserver"
            ? `select * from sloppy_jobs${clause} order by created_at desc, id desc offset ${offset} rows fetch next ${pageSize} rows only`
            : `select * from sloppy_jobs${clause} order by created_at desc, id desc limit ${this.p(params.length + 1)} offset ${this.p(params.length + 2)}`;
        const rows = compactRows(await this.db.query(
            listSql,
            this.provider === "sqlserver" ? params : [...params, pageSize, offset],
            { maxRows: pageSize },
        ));
        return Object.freeze(rows.map(normalizeJob));
    }

    async statusCounts() {
        const rows = compactRows(await this.db.query(
            "select status, count(*) as count from sloppy_jobs group by status",
            [],
            { maxRows: Object.keys(JOB_STATUSES).length },
        ));
        const counts = {};
        for (const status of Object.keys(JOB_STATUSES)) {
            counts[status] = 0;
        }
        for (const row of rows) {
            const status = rowField(row, "status");
            if (JOB_STATUSES[status] === true) {
                counts[status] = Number(rowField(row, "count", "count(*)") ?? 0);
            }
        }
        return Object.freeze(counts);
    }

    async attempts(jobId) {
        const limit = MAX_PAGE_SIZE;
        const sql = limitSql(
            this.provider,
            `select * from sloppy_job_attempts where job_id = ${this.p(1)} order by attempt_number`,
            this.p(2),
            limit,
        );
        const rows = compactRows(await this.db.query(
            sql,
            limitParams(this.provider, [jobId], limit),
            { maxRows: limit },
        ));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row })));
    }

    async events(jobId) {
        const limit = MAX_PAGE_SIZE;
        const sql = limitSql(
            this.provider,
            `select * from sloppy_job_events where job_id = ${this.p(1)} order by created_at, id`,
            this.p(2),
            limit,
        );
        const rows = compactRows(await this.db.query(
            sql,
            limitParams(this.provider, [jobId], limit),
            { maxRows: limit },
        ));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row, data: jsonParse(row.data_json, {}) })));
    }

    async transition(id, to, context = undefined) {
        const ctx = context === undefined ? {} : context;
        if (JOB_STATUSES[to] !== true) {
            throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "target job status is invalid", { to });
        }
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [id]));
            if (job === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job was not found", { id });
            }
            if (job.status === to) {
                return job;
            }
            if (!transitionAllowed(job.status, to)) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job transition is invalid", {
                    id,
                    from: job.status,
                    to,
                });
            }
            const timestamp = await this.now(tx);
            await execSql(
                tx,
                `update sloppy_jobs set status = ${this.p(1)}, updated_at = ${this.p(2)} where id = ${this.p(3)}`,
                [to, timestamp, id],
            );
            await this.addEvent(tx, id, ctx.eventType ?? "transition", job.status, to, timestamp, ctx.workerId ?? null, ctx.message ?? null, ctx.data ?? {});
            return normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [id]));
        });
    }

    async registerWorker(worker) {
        const timestamp = await this.now();
        const existing = await queryOne(this.db, `select * from sloppy_job_workers where id = ${this.p(1)}`, [worker.id]);
        if (existing !== null && existing !== undefined) {
            await execSql(
                this.db,
                `update sloppy_job_workers set worker_name = ${this.p(1)}, host = ${this.p(2)}, pid = ${this.p(3)}, queues = ${this.p(4)}, started_at = ${this.p(5)}, last_heartbeat_at = ${this.p(6)}, status = ${this.p(7)} where id = ${this.p(8)}`,
                [
                    worker.name,
                    worker.host ?? null,
                    worker.pid ?? null,
                    jsonStringify(worker.queues, "worker queues"),
                    timestamp,
                    timestamp,
                    "running",
                    worker.id,
                ],
            );
            return;
        }
        await execSql(
            this.db,
            `insert into sloppy_job_workers (id, worker_name, host, pid, queues, started_at, last_heartbeat_at, status) values (${this.ps(8).join(", ")})`,
            [
                worker.id,
                worker.name,
                worker.host ?? null,
                worker.pid ?? null,
                jsonStringify(worker.queues, "worker queues"),
                timestamp,
                timestamp,
                "running",
            ],
        );
    }

    async heartbeat(workerId) {
        const timestamp = await this.now();
        if (this.provider === "sqlserver") {
            await execSql(this.db, `if exists (select 1 from sloppy_job_workers where id = ${this.p(1)}) update sloppy_job_workers set last_heartbeat_at = ${this.p(2)}, status = ${this.p(3)} where id = ${this.p(4)}`, [
                workerId,
                timestamp,
                "running",
                workerId,
            ]);
            return;
        }
        await execSql(this.db, `update sloppy_job_workers set last_heartbeat_at = ${this.p(1)}, status = ${this.p(2)} where id = ${this.p(3)}`, [
            timestamp,
            "running",
            workerId,
        ]);
    }

    async stopWorker(workerId) {
        const timestamp = await this.now();
        if (this.provider === "sqlserver") {
            await execSql(this.db, `if exists (select 1 from sloppy_job_workers where id = ${this.p(1)}) update sloppy_job_workers set last_heartbeat_at = ${this.p(2)}, status = ${this.p(3)} where id = ${this.p(4)}`, [
                workerId,
                timestamp,
                "stopped",
                workerId,
            ]);
            return;
        }
        await execSql(this.db, `update sloppy_job_workers set last_heartbeat_at = ${this.p(1)}, status = ${this.p(2)} where id = ${this.p(3)}`, [
            timestamp,
            "stopped",
            workerId,
        ]);
    }

    async listWorkers() {
        const rows = compactRows(await this.db.query(
            limitSql(this.provider, "select * from sloppy_job_workers order by started_at desc", this.p(1), MAX_PAGE_SIZE),
            limitParams(this.provider, [], MAX_PAGE_SIZE),
            { maxRows: MAX_PAGE_SIZE },
        ));
        return Object.freeze(rows.map((row) => Object.freeze({
            ...row,
            queues: jsonParse(row.queues, []),
        })));
    }

    async claim(worker, options = undefined) {
        const opts = options === undefined ? {} : options;
        const limit = positiveInteger(opts.limit, "Sloppy Jobs claim limit", worker.concurrency ?? 1);
        const queues = Object.freeze((opts.queues ?? worker.queues ?? [DEFAULT_QUEUE]).map((queue) => requireName(queue, "Sloppy Jobs queue")));
        return await inTransaction(this.db, async (tx) => {
            const timestamp = await this.now(tx);
            const lockedUntil = addMsIso(timestamp, positiveInteger(opts.leaseMs, "Sloppy Jobs leaseMs", DEFAULT_WORKER.leaseMs));
            if (this.provider === "sqlserver") {
                await execSql(tx, `if exists (select 1 from sloppy_jobs where status = 'processing' and locked_until <= ${this.p(1)}) update sloppy_jobs set status = 'queued', locked_by = null, locked_until = null, updated_at = ${this.p(2)} where status = 'processing' and locked_until <= ${this.p(3)}`, [
                    timestamp,
                    timestamp,
                    timestamp,
                ]);
                await execSql(tx, `if exists (select 1 from sloppy_jobs where status in ('scheduled', 'retrying') and run_at <= ${this.p(1)}) update sloppy_jobs set status = 'queued', updated_at = ${this.p(2)} where status in ('scheduled', 'retrying') and run_at <= ${this.p(3)}`, [
                    timestamp,
                    timestamp,
                    timestamp,
                ]);
            } else {
                await execSql(tx, `update sloppy_jobs set status = 'queued', locked_by = null, locked_until = null, updated_at = ${this.p(1)} where status = 'processing' and locked_until <= ${this.p(2)}`, [
                    timestamp,
                    timestamp,
                ]);
                await execSql(tx, `update sloppy_jobs set status = 'queued', updated_at = ${this.p(1)} where status in ('scheduled', 'retrying') and run_at <= ${this.p(2)}`, [
                    timestamp,
                    timestamp,
                ]);
            }
            const rows = compactRows(await tx.query(
                claimSelectSql(this.provider, queues.length, limit),
                this.provider === "sqlserver" ? [...queues, timestamp] : [...queues, timestamp, limit],
                { maxRows: limit },
            ));
            const claimed = [];
            for (const row of rows) {
                const job = normalizeJob(row);
                const attemptNumber = job.attemptCount + 1;
                const claimSql = this.provider === "sqlserver"
                    ? `if exists (select 1 from sloppy_jobs where id = ${this.p(1)} and status = 'queued') update sloppy_jobs set status = 'processing', locked_by = ${this.p(2)}, locked_until = ${this.p(3)}, attempt_count = ${this.p(4)}, updated_at = ${this.p(5)} where id = ${this.p(6)} and status = 'queued'`
                    : `update sloppy_jobs set status = 'processing', locked_by = ${this.p(1)}, locked_until = ${this.p(2)}, attempt_count = ${this.p(3)}, updated_at = ${this.p(4)} where id = ${this.p(5)} and status = 'queued'`;
                const claimParams = this.provider === "sqlserver"
                    ? [job.id, worker.id, lockedUntil, attemptNumber, timestamp, job.id]
                    : [worker.id, lockedUntil, attemptNumber, timestamp, job.id];
                const result = await execSql(tx, claimSql, claimParams);
                void result;
                const refreshed = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [job.id]));
                if (refreshed?.status === "processing" && refreshed.lockedBy === worker.id) {
                    const attemptId = stableId("attempt");
                    await execSql(
                        tx,
                        `insert into sloppy_job_attempts (id, job_id, worker_id, attempt_number, started_at, finished_at, status, duration_ms, error_code, error_message, diagnostic_id) values (${this.ps(11).join(", ")})`,
                        [attemptId, job.id, worker.id, attemptNumber, timestamp, null, "processing", null, null, null, null],
                    );
                    await this.addEvent(tx, job.id, "claimed", "queued", "processing", timestamp, worker.id, "job claimed", {
                        attemptId,
                        lockedUntil,
                    });
                    claimed.push(refreshed);
                }
            }
            return Object.freeze(claimed);
        });
    }

    async complete(jobId, workerId, result = undefined) {
        return await this.finishProcessing(jobId, workerId, "succeeded", undefined, result);
    }

    async fail(jobId, workerId, error, retryPolicy) {
        const code = typeof error?.code === "string" ? error.code : "SLOPPY_E_JOBS_HANDLER_FAILED";
        const message = String(error?.message ?? error ?? "job failed");
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
            if (job === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job was not found", { jobId });
            }
            if (job.status !== "processing") {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only processing jobs can fail", {
                    jobId,
                    status: job.status,
                });
            }
            if (job.lockedBy !== workerId) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job failure lost ownership", {
                    jobId,
                    workerId,
                    lockedBy: job.lockedBy,
                });
            }
            const timestamp = await this.now(tx);
            const policy = retryPolicy ?? jsonParse(job.retryPolicyJson, { maxAttempts: job.maxAttempts, backoff: "none", initialDelayMs: 0, maxDelayMs: 0 });
            const exhausted = job.attemptCount >= job.maxAttempts;
            const next = exhausted ? "dead" : "retrying";
            const nextRunAt = exhausted ? null : addMsIso(timestamp, retryDelayMs(policy, job.attemptCount));
            await execSql(
                tx,
                `update sloppy_jobs set status = ${this.p(1)}, locked_by = null, locked_until = null, updated_at = ${this.p(2)}, next_retry_at = ${this.p(3)}, run_at = coalesce(${this.p(4)}, run_at), last_error_code = ${this.p(5)}, last_error_message = ${this.p(6)}, diagnostic_id = ${this.p(7)} where id = ${this.p(8)} and locked_by = ${this.p(9)} and status = 'processing'`,
                [next, timestamp, nextRunAt, nextRunAt, code, message, stableId("diag"), jobId, workerId],
            );
            const failed = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
            if (failed?.status !== next || failed.lockedBy !== null) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job failure lost ownership", {
                    jobId,
                    workerId,
                    status: failed?.status,
                });
            }
            await execSql(
                tx,
                `update sloppy_job_attempts set finished_at = ${this.p(1)}, status = ${this.p(2)}, error_code = ${this.p(3)}, error_message = ${this.p(4)}, diagnostic_id = ${this.p(5)} where job_id = ${this.p(6)} and attempt_number = ${this.p(7)}`,
                [timestamp, next, code, message, stableId("diag"), jobId, job.attemptCount],
            );
            await this.addEvent(tx, jobId, exhausted ? "dead" : "retrying", "processing", next, timestamp, workerId, message, {
                errorCode: code,
                exhausted,
                nextRunAt,
            });
            return failed;
        });
    }

    async finishProcessing(jobId, workerId, status, error = undefined, result = undefined) {
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
            if (job === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job was not found", { jobId });
            }
            if (job.status !== "processing") {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only processing jobs can be completed", {
                    jobId,
                    status: job.status,
                });
            }
            const timestamp = await this.now(tx);
            await execSql(
                tx,
                `update sloppy_jobs set status = ${this.p(1)}, locked_by = null, locked_until = null, updated_at = ${this.p(2)} where id = ${this.p(3)} and locked_by = ${this.p(4)} and status = 'processing'`,
                [status, timestamp, jobId, workerId],
            );
            const completed = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
            if (completed?.status !== status || completed.lockedBy !== null) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job completion lost ownership", {
                    jobId,
                    workerId,
                    status: completed?.status,
                });
            }
            await execSql(
                tx,
                `update sloppy_job_attempts set finished_at = ${this.p(1)}, status = ${this.p(2)}, duration_ms = ${this.p(3)}, error_code = ${this.p(4)}, error_message = ${this.p(5)} where job_id = ${this.p(6)} and attempt_number = ${this.p(7)}`,
                [
                    timestamp,
                    status,
                    Math.max(0, new Date(timestamp).getTime() - new Date(job.updatedAt).getTime()),
                    error?.code ?? null,
                    error?.message ?? null,
                    jobId,
                    job.attemptCount,
                ],
            );
            await this.addEvent(tx, jobId, status, "processing", status, timestamp, workerId, status, {
                result: result === undefined ? null : result,
            });
            return completed;
        });
    }

    async extendLease(jobId, workerId, leaseMs) {
        const timestamp = await this.now();
        const lockedUntil = addMsIso(timestamp, positiveInteger(leaseMs, "Sloppy Jobs leaseMs"));
        await execSql(
            this.db,
            `update sloppy_jobs set locked_until = ${this.p(1)}, updated_at = ${this.p(2)} where id = ${this.p(3)} and locked_by = ${this.p(4)} and status = 'processing'`,
            [lockedUntil, timestamp, jobId, workerId],
        );
        const job = normalizeJob(await queryOne(this.db, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
        if (job === null || job.lockedBy !== workerId || job.lockedUntil !== lockedUntil || job.status !== "processing") {
            throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job lease extension lost ownership", {
                jobId,
                workerId,
                status: job?.status,
            });
        }
        return lockedUntil;
    }

    async manualRetry(jobId) {
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
            if (job === null || !["dead", "failed"].includes(job.status)) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only failed or dead jobs can be manually retried", {
                    jobId,
                    status: job?.status,
                });
            }
            const timestamp = await this.now(tx);
            await execSql(tx, `update sloppy_jobs set status = 'queued', locked_by = null, locked_until = null, run_at = ${this.p(1)}, updated_at = ${this.p(2)} where id = ${this.p(3)}`, [
                timestamp,
                timestamp,
                jobId,
            ]);
            await this.addEvent(tx, jobId, "manual-retry", job.status, "queued", timestamp, null, "job manually retried", {});
            return normalizeJob(await queryOne(tx, `select * from sloppy_jobs where id = ${this.p(1)}`, [jobId]));
        });
    }

    async cancel(jobId) {
        const job = await this.getJob(jobId);
        if (job === null || !["queued", "scheduled", "processing", "retrying"].includes(job.status)) {
            throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job cannot be cancelled from current state", {
                jobId,
                status: job?.status,
            });
        }
        return await this.transition(jobId, "cancelled", { eventType: "cancelled", message: "job cancelled" });
    }

    async delete(jobId) {
        const job = await this.getJob(jobId);
        if (job === null || !["queued", "dead", "succeeded", "cancelled"].includes(job.status)) {
            throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "job cannot be deleted from current state", {
                jobId,
                status: job?.status,
            });
        }
        return await this.transition(jobId, "deleted", { eventType: "deleted", message: "job deleted" });
    }

    async addEvent(tx, jobId, eventType, from, to, createdAt, workerId, message, data) {
        await execSql(
            tx,
            `insert into sloppy_job_events (id, job_id, event_type, from_status, to_status, created_at, worker_id, message, data_json) values (${this.ps(9).join(", ")})`,
            [stableId("event"), jobId, eventType, from, to, createdAt, workerId, message, jsonStringify(data ?? {}, "job event data")],
        );
    }

    async upsertRecurring(config) {
        const timestamp = await this.now();
        const existing = await queryOne(this.db, `select * from sloppy_recurring_jobs where name = ${this.p(1)}`, [config.name]);
        if (existing === null || existing === undefined) {
            await execSql(
                this.db,
                `insert into sloppy_recurring_jobs (id, name, job_name, queue, cron, timezone, payload_json, enabled, misfire_policy, last_run_at, next_run_at, created_at, updated_at, metadata_json) values (${this.ps(14).join(", ")})`,
                [
                    stableId("recurring"),
                    config.name,
                    config.jobName,
                    config.queue,
                    config.cron,
                    config.timezone,
                    config.payloadJson,
                    config.enabled ? 1 : 0,
                    config.misfirePolicy,
                    null,
                    config.nextRunAt,
                    timestamp,
                    timestamp,
                    jsonStringify(config.metadata, "recurring metadata"),
                ],
            );
        } else {
            await execSql(
                this.db,
                `update sloppy_recurring_jobs set job_name = ${this.p(1)}, queue = ${this.p(2)}, cron = ${this.p(3)}, timezone = ${this.p(4)}, payload_json = ${this.p(5)}, enabled = ${this.p(6)}, misfire_policy = ${this.p(7)}, next_run_at = ${this.p(8)}, updated_at = ${this.p(9)}, metadata_json = ${this.p(10)} where name = ${this.p(11)}`,
                [
                    config.jobName,
                    config.queue,
                    config.cron,
                    config.timezone,
                    config.payloadJson,
                    config.enabled ? 1 : 0,
                    config.misfirePolicy,
                    config.nextRunAt,
                    timestamp,
                    jsonStringify(config.metadata, "recurring metadata"),
                    config.name,
                ],
            );
        }
        return await this.getRecurring(config.name);
    }

    async getRecurring(name) {
        return normalizeRecurring(await queryOne(this.db, `select * from sloppy_recurring_jobs where name = ${this.p(1)}`, [name]));
    }

    async listRecurring() {
        const rows = compactRows(await this.db.query(
            limitSql(this.provider, "select * from sloppy_recurring_jobs order by name", this.p(1), MAX_PAGE_SIZE),
            limitParams(this.provider, [], MAX_PAGE_SIZE),
            { maxRows: MAX_PAGE_SIZE },
        ));
        return Object.freeze(rows.map(normalizeRecurring));
    }

    async dueRecurring(now, limit = DEFAULT_PAGE_SIZE) {
        const pageSize = Math.min(MAX_PAGE_SIZE, positiveInteger(limit, "Sloppy Jobs recurring tick limit", DEFAULT_PAGE_SIZE));
        const nowPlaceholder = placeholder(this.provider, 1);
        const limitPlaceholder = placeholder(this.provider, 2);
        const rows = compactRows(await this.db.query(
            limitSql(
                this.provider,
                `select * from sloppy_recurring_jobs where enabled = 1 and next_run_at <= ${nowPlaceholder} order by next_run_at asc, name asc`,
                limitPlaceholder,
                pageSize,
            ),
            limitParams(this.provider, [now], pageSize),
            { maxRows: pageSize },
        ));
        return Object.freeze(rows.map(normalizeRecurring));
    }

    async markRecurringTicked(name, lastRunAt, nextRunAt) {
        await execSql(this.db, `update sloppy_recurring_jobs set last_run_at = ${this.p(1)}, next_run_at = ${this.p(2)}, updated_at = ${this.p(3)} where name = ${this.p(4)}`, [
            lastRunAt,
            nextRunAt,
            await this.now(),
            name,
        ]);
        return await this.getRecurring(name);
    }

    async setRecurringEnabled(name, enabled) {
        await execSql(this.db, `update sloppy_recurring_jobs set enabled = ${this.p(1)}, updated_at = ${this.p(2)} where name = ${this.p(3)}`, [
            enabled ? 1 : 0,
            await this.now(),
            name,
        ]);
        return await this.getRecurring(name);
    }

    async acquireLock(name, owner, ttlMs = DEFAULT_LOCK_TTL_MS) {
        return await inTransaction(this.db, async (tx) => {
            const now = await this.now(tx);
            const lockedUntil = addMsIso(now, ttlMs);
            try {
                if (this.provider === "sqlserver") {
                    await execSql(
                        tx,
                        `if not exists (select 1 from sloppy_job_locks with (updlock, holdlock) where name = ${this.p(1)}) insert into sloppy_job_locks (name, owner, locked_until, updated_at) values (${this.ps(4, 2).join(", ")})`,
                        [name, name, owner, lockedUntil, now],
                    );
                } else {
                    await execSql(
                        tx,
                        insertOrIgnoreSql(this.provider, "sloppy_job_locks", ["name", "owner", "locked_until", "updated_at"], ["name"]),
                        [name, owner, lockedUntil, now],
                    );
                }
            } catch (error) {
                if (!uniqueConflict(error)) {
                    throw error;
                }
            }
            if (this.provider === "sqlserver") {
                await execSql(
                    tx,
                    `if exists (select 1 from sloppy_job_locks where name = ${this.p(1)} and (owner = ${this.p(2)} or locked_until <= ${this.p(3)})) update sloppy_job_locks set owner = ${this.p(4)}, locked_until = ${this.p(5)}, updated_at = ${this.p(6)} where name = ${this.p(7)} and (owner = ${this.p(8)} or locked_until <= ${this.p(9)})`,
                    [name, owner, now, owner, lockedUntil, now, name, owner, now],
                );
            } else {
                await execSql(
                    tx,
                    `update sloppy_job_locks set owner = ${this.p(1)}, locked_until = ${this.p(2)}, updated_at = ${this.p(3)} where name = ${this.p(4)} and (owner = ${this.p(5)} or locked_until <= ${this.p(6)})`,
                    [owner, lockedUntil, now, name, owner, now],
                );
            }
            const current = await queryOne(tx, `select * from sloppy_job_locks where name = ${this.p(1)}`, [name]);
            return current?.owner === owner && current.locked_until === lockedUntil;
        });
    }

    async releaseLock(name, owner) {
        return await inTransaction(this.db, async (tx) => {
            const existing = await queryOne(tx, `select * from sloppy_job_locks where name = ${this.p(1)}`, [name]);
            if (existing === null || existing === undefined) {
                return false;
            }
            if (existing.owner !== owner) {
                throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is owned by another owner", { name });
            }
            const sql = this.provider === "sqlserver"
                ? `if exists (select 1 from sloppy_job_locks where name = ${this.p(1)} and owner = ${this.p(2)}) delete from sloppy_job_locks where name = ${this.p(3)} and owner = ${this.p(4)}`
                : `delete from sloppy_job_locks where name = ${this.p(1)} and owner = ${this.p(2)}`;
            await execSql(tx, sql, this.provider === "sqlserver" ? [name, owner, name, owner] : [name, owner]);
            const current = await queryOne(tx, `select * from sloppy_job_locks where name = ${this.p(1)}`, [name]);
            if (current === null || current === undefined) {
                return true;
            }
            if (current.owner !== owner) {
                throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is owned by another owner", { name });
            }
            return false;
        });
    }

    async extendLock(name, owner, ttlMs = DEFAULT_LOCK_TTL_MS) {
        return await inTransaction(this.db, async (tx) => {
            const now = await this.now(tx);
            const lockedUntil = addMsIso(now, ttlMs);
            const sql = this.provider === "sqlserver"
                ? `if exists (select 1 from sloppy_job_locks where name = ${this.p(1)} and owner = ${this.p(2)}) update sloppy_job_locks set locked_until = ${this.p(3)}, updated_at = ${this.p(4)} where name = ${this.p(5)} and owner = ${this.p(6)}`
                : `update sloppy_job_locks set locked_until = ${this.p(1)}, updated_at = ${this.p(2)} where name = ${this.p(3)} and owner = ${this.p(4)}`;
            await execSql(tx, sql, this.provider === "sqlserver" ? [name, owner, lockedUntil, now, name, owner] : [
                lockedUntil,
                now,
                name,
                owner,
            ]);
            const current = await queryOne(tx, `select * from sloppy_job_locks where name = ${this.p(1)}`, [name]);
            if (current === null || current === undefined || current.owner !== owner || current.locked_until !== lockedUntil) {
                throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is not owned by caller", { name });
            }
            return lockedUntil;
        });
    }

    async listLocks() {
        const rows = compactRows(await this.db.query(
            limitSql(this.provider, "select * from sloppy_job_locks order by name", this.p(1), MAX_PAGE_SIZE),
            limitParams(this.provider, [], MAX_PAGE_SIZE),
            { maxRows: MAX_PAGE_SIZE },
        ));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row })));
    }

    async cleanup(options = undefined) {
        const current = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs cleanup options");
        const batchSize = Math.min(MAX_PAGE_SIZE, positiveInteger(current.batchSize, "Sloppy Jobs cleanup batchSize", DEFAULT_PAGE_SIZE));
        const terminalStatuses = Object.freeze(["succeeded", "dead", "cancelled"]);
        const now = await this.now();
        const succeededBefore = addMsIso(now, -nonNegativeInteger(current.keepSucceededMs ?? 86400000, "Sloppy Jobs keepSucceededMs"));
        const deadBefore = addMsIso(now, -nonNegativeInteger(current.keepDeadMs ?? 604800000, "Sloppy Jobs keepDeadMs"));
        const cancelledBefore = addMsIso(now, -nonNegativeInteger(current.keepCancelledMs ?? current.keepSucceededMs ?? 86400000, "Sloppy Jobs keepCancelledMs"));
        const statusSql =
            `(status = 'succeeded' and updated_at <= ${placeholder(this.provider, 1)}) ` +
            `or (status = 'dead' and updated_at <= ${placeholder(this.provider, 2)}) ` +
            `or (status = 'cancelled' and updated_at <= ${placeholder(this.provider, 3)})`;
        const rows = compactRows(await this.db.query(
            limitSql(
                this.provider,
                `select * from sloppy_jobs where ${statusSql} order by updated_at asc, id asc`,
                placeholder(this.provider, 4),
                batchSize,
            ),
            limitParams(this.provider, [succeededBefore, deadBefore, cancelledBefore], batchSize),
            { maxRows: batchSize },
        ));
        const deleted = [];
        for (const job of rows.map(normalizeJob)) {
            if (job !== null && terminalStatuses.includes(job.status)) {
                deleted.push(await this.transition(job.id, "deleted", {
                    eventType: "retention-cleanup",
                    message: "job removed by retention cleanup",
                }));
            }
        }
        return Object.freeze({ deleted: deleted.length, jobs: Object.freeze(deleted) });
    }
}

function normalizeJob(row) {
    if (row === null || row === undefined) {
        return null;
    }
    return Object.freeze({
        id: row.id,
        name: row.name,
        queue: row.queue,
        status: row.status,
        payloadJson: row.payload_json,
        payload: jsonParse(row.payload_json, null),
        payloadPreview: payloadPreview(row.payload_json),
        payloadSchema: row.payload_schema ?? null,
        priority: Number(row.priority ?? 0),
        runAt: row.run_at,
        createdAt: row.created_at,
        updatedAt: row.updated_at,
        lockedBy: row.locked_by ?? null,
        lockedUntil: row.locked_until ?? null,
        attemptCount: Number(row.attempt_count ?? 0),
        maxAttempts: Number(row.max_attempts ?? 1),
        retryPolicyJson: row.retry_policy_json,
        retryPolicy: jsonParse(row.retry_policy_json, {}),
        nextRetryAt: row.next_retry_at ?? null,
        lastErrorCode: row.last_error_code ?? null,
        lastErrorMessage: row.last_error_message ?? null,
        diagnosticId: row.diagnostic_id ?? null,
        correlationId: row.correlation_id ?? null,
        idempotencyKey: row.idempotency_key ?? null,
        timeoutMs: row.timeout_ms === null || row.timeout_ms === undefined ? null : Number(row.timeout_ms),
        metadata: jsonParse(row.metadata_json, {}),
    });
}

function normalizeRecurring(row) {
    if (row === null || row === undefined) {
        return null;
    }
    return Object.freeze({
        id: row.id,
        name: row.name,
        jobName: row.job_name,
        queue: row.queue,
        cron: row.cron,
        timezone: row.timezone,
        payload: jsonParse(row.payload_json, null),
        payloadPreview: payloadPreview(row.payload_json),
        enabled: row.enabled === true || Number(row.enabled) === 1,
        misfirePolicy: row.misfire_policy,
        lastRunAt: row.last_run_at ?? null,
        nextRunAt: row.next_run_at,
        createdAt: row.created_at,
        updatedAt: row.updated_at,
        metadata: jsonParse(row.metadata_json, {}),
    });
}

function parseCronField(text, min, max) {
    const values = new Set();
    for (const part of text.split(",")) {
        if (part === "*") {
            for (let value = min; value <= max; value += 1) {
                values.add(value);
            }
            continue;
        }
        if (part.startsWith("*/")) {
            const step = positiveInteger(Number(part.slice(2)), "cron step");
            for (let value = min; value <= max; value += step) {
                values.add(value);
            }
            continue;
        }
        const value = Number(part);
        if (!Number.isInteger(value) || value < min || value > max) {
            throw jobsError("SLOPPY_E_JOBS_RECURRING_INVALID_CRON", "cron field is invalid", { field: text });
        }
        values.add(value);
    }
    return values;
}

function nextCronRun(cron, from = new Date()) {
    const parts = cron.trim().split(/\s+/u);
    if (parts.length !== 5) {
        throw jobsError("SLOPPY_E_JOBS_RECURRING_INVALID_CRON", "cron expression must have five fields");
    }
    const [minutes, hours, days, months, dows] = [
        parseCronField(parts[0], 0, 59),
        parseCronField(parts[1], 0, 23),
        parseCronField(parts[2], 1, 31),
        parseCronField(parts[3], 1, 12),
        parseCronField(parts[4], 0, 6),
    ];
    const cursor = new Date(from.getTime());
    cursor.setUTCSeconds(0, 0);
    cursor.setUTCMinutes(cursor.getUTCMinutes() + 1);
    for (let attempts = 0; attempts < 366 * 24 * 60; attempts += 1) {
        if (
            minutes.has(cursor.getUTCMinutes()) &&
            hours.has(cursor.getUTCHours()) &&
            days.has(cursor.getUTCDate()) &&
            months.has(cursor.getUTCMonth() + 1) &&
            dows.has(cursor.getUTCDay())
        ) {
            return cursor.toISOString();
        }
        cursor.setUTCMinutes(cursor.getUTCMinutes() + 1);
    }
    throw jobsError("SLOPPY_E_JOBS_RECURRING_INVALID_CRON", "cron expression has no run in the next year");
}

function normalizeRecurringOptions(name, jobName, payload, options = undefined, now = undefined) {
    const current = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs recurring options");
    const timezone = current.timezone ?? "UTC";
    if (timezone !== "UTC") {
        throw jobsError("SLOPPY_E_JOBS_RECURRING_INVALID_CRON", "recurring timezone support currently requires UTC", {
            timezone,
        });
    }
    const cron = typeof current.cron === "string" ? current.cron : undefined;
    if (cron === undefined) {
        throw new TypeError("Sloppy Jobs recurring cron is required.");
    }
    const misfirePolicy = current.misfirePolicy ?? "run-once";
    if (!["ignore", "run-once", "catch-up-limited"].includes(misfirePolicy)) {
        throw new TypeError("Sloppy Jobs misfirePolicy must be ignore, run-once, or catch-up-limited.");
    }
    const catchUpLimit = misfirePolicy === "catch-up-limited"
        ? positiveInteger(current.catchUpLimit, "Sloppy Jobs catchUpLimit", 10)
        : undefined;
    return Object.freeze({
        name: requireName(name, "Sloppy Jobs recurring name"),
        jobName: requireName(jobName, "Sloppy Jobs recurring job name"),
        queue: optionalName(current.queue, "Sloppy Jobs recurring queue", DEFAULT_QUEUE),
        cron,
        timezone,
        payloadJson: jsonStringify(payload, "recurring payload"),
        enabled: current.enabled !== false,
        misfirePolicy,
        nextRunAt: nextCronRun(cron, now === undefined ? undefined : new Date(now)),
        metadata: Object.freeze({
            ...(current.metadata ?? {}),
            ...(catchUpLimit === undefined ? {} : { catchUpLimit }),
        }),
    });
}

class JobsRuntime {
    constructor(options = undefined) {
        const opts = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs options");
        if (!(opts.storage instanceof SchedulerStorage)) {
            throw new TypeError("Sloppy Jobs requires storage created by Jobs.storage.*().");
        }
        this.storage = opts.storage;
        this.definitions = new Map();
        Object.seal(this);
    }

    define(name, options, handler) {
        const jobName = requireName(name, "Sloppy Jobs definition name");
        if (this.definitions.has(jobName)) {
            throw jobsError("SLOPPY_E_JOBS_DUPLICATE_JOB", "job definition already exists", { jobName });
        }
        if (typeof handler !== "function") {
            throw new TypeError("Sloppy Jobs define handler must be a function.");
        }
        const normalized = normalizeDefinitionOptions(options);
        this.definitions.set(jobName, Object.freeze({
            name: jobName,
            ...normalized,
            handler,
        }));
        return this;
    }

    definition(name) {
        return this.definitions.get(name);
    }

    async enqueue(name, payload = null, options = undefined) {
        const jobName = requireName(name, "Sloppy Jobs enqueue name");
        const definition = this.definitions.get(jobName);
        if (definition === undefined) {
            throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job definition is not registered", { jobName });
        }
        const enqueue = normalizeEnqueueOptions(definition, options);
        const validatedPayload = validatePayload(definition.input, payload, "enqueue", jobName);
        const timestamp = await this.storage.now();
        const runAt = enqueue.runAt ?? addMsIso(timestamp, enqueue.delayMs);
        const status = runAt > timestamp ? "scheduled" : "queued";
        return await this.storage.enqueue({
            id: stableId("job"),
            name: jobName,
            queue: enqueue.queue,
            status,
            payloadJson: jsonStringify(validatedPayload, "job payload"),
            payloadSchema: definition.input?.metadata?.name ?? definition.input?.kind ?? null,
            priority: enqueue.priority,
            runAt,
            createdAt: timestamp,
            updatedAt: timestamp,
            retryPolicy: enqueue.retry,
            maxAttempts: enqueue.maxAttempts,
            correlationId: enqueue.correlationId,
            idempotencyKey: enqueue.idempotencyKey,
            timeoutMs: enqueue.timeoutMs,
            metadata: enqueue.metadata,
        });
    }

    async enqueueDelayed(name, payload, delayMs, options = undefined) {
        return await this.enqueue(name, payload, { ...(options ?? {}), delayMs });
    }

    async enqueueAt(name, payload, runAt, options = undefined) {
        return await this.enqueue(name, payload, { ...(options ?? {}), runAt });
    }

    async recurring(name, jobName, payload = null, options = undefined) {
        if (!this.definitions.has(jobName)) {
            throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "recurring job definition is not registered", { jobName });
        }
        const config = normalizeRecurringOptions(name, jobName, payload, options, await this.storage.now());
        return await this.storage.upsertRecurring(config);
    }

    async tickRecurring(options = undefined) {
        const owner = options?.owner ?? stableId("recurring");
        const acquired = await this.storage.acquireLock("sloppy.jobs.recurring.tick", owner, options?.ttlMs ?? DEFAULT_LOCK_TTL_MS);
        if (!acquired) {
            return Object.freeze([]);
        }
        try {
            const now = await this.storage.now();
            const due = await this.storage.dueRecurring(now, options?.limit ?? DEFAULT_PAGE_SIZE);
            const enqueued = [];
            for (const schedule of due) {
                let next = nextCronRun(schedule.cron, new Date(now));
                if (schedule.misfirePolicy === "run-once") {
                    enqueued.push(await this._enqueueRecurringOccurrence(schedule, schedule.nextRunAt));
                } else if (schedule.misfirePolicy === "catch-up-limited") {
                    const catchUpLimit = positiveInteger(
                        options?.catchUpLimit ?? schedule.metadata.catchUpLimit,
                        "Sloppy Jobs catchUpLimit",
                        10,
                    );
                    let occurrence = schedule.nextRunAt;
                    for (let count = 0; count < catchUpLimit && occurrence <= now; count += 1) {
                        enqueued.push(await this._enqueueRecurringOccurrence(schedule, occurrence));
                        occurrence = nextCronRun(schedule.cron, new Date(occurrence));
                    }
                    next = occurrence;
                }
                await this.storage.markRecurringTicked(schedule.name, now, next);
            }
            return Object.freeze(enqueued);
        } finally {
            await this.storage.releaseLock("sloppy.jobs.recurring.tick", owner);
        }
    }

    async _enqueueRecurringOccurrence(schedule, occurrenceAt) {
        const occurrence = occurrenceAt.replace(/[-:.TZ]/gu, "");
        return await this.enqueue(schedule.jobName, schedule.payload, {
            queue: schedule.queue,
            idempotencyKey: `${schedule.name}:${occurrence}`,
            metadata: { recurring: schedule.name, occurrence: occurrenceAt },
        });
    }

    createWorker(options = undefined) {
        return new SchedulerWorker(this, options);
    }

    admin() {
        return createAdminService(this);
    }

    locks(owner = stableId("locks")) {
        return createLockService(this.storage, owner);
    }

    __sloppyPlanMetadata() {
        return Object.freeze({
            kind: "durableScheduler",
            definitions: Object.freeze(Array.from(this.definitions.values()).map((definition) => Object.freeze({
                name: definition.name,
                queue: definition.queue,
                retry: definition.retries,
                timeoutMs: definition.timeoutMs ?? null,
                payloadSchema: definition.input?.metadata?.name ?? definition.input?.kind ?? null,
            }))),
        });
    }
}

class SchedulerWorker {
    constructor(runtime, options = undefined) {
        const opts = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs worker options");
        this.runtime = runtime;
        this.id = optionalName(opts.id, "Sloppy Jobs worker id", stableId("worker"));
        this.name = optionalName(opts.name, "Sloppy Jobs worker name", this.id);
        this.host = opts.host ?? null;
        this.pid = opts.pid ?? null;
        this.queues = Object.freeze((opts.queues ?? DEFAULT_WORKER.queues).map((queue) => requireName(queue, "Sloppy Jobs worker queue")));
        this.concurrency = positiveInteger(opts.concurrency, "Sloppy Jobs worker concurrency", DEFAULT_WORKER.concurrency);
        this.pollIntervalMs = positiveInteger(opts.pollIntervalMs, "Sloppy Jobs worker pollIntervalMs", DEFAULT_WORKER.pollIntervalMs);
        this.idleBackoffMs = positiveInteger(opts.idleBackoffMs, "Sloppy Jobs worker idleBackoffMs", DEFAULT_WORKER.idleBackoffMs);
        this.heartbeatIntervalMs = positiveInteger(opts.heartbeatIntervalMs, "Sloppy Jobs worker heartbeatIntervalMs", DEFAULT_WORKER.heartbeatIntervalMs);
        this.leaseMs = positiveInteger(opts.leaseMs, "Sloppy Jobs worker leaseMs", DEFAULT_WORKER.leaseMs);
        this.shutdownTimeoutMs = positiveInteger(opts.shutdownTimeoutMs, "Sloppy Jobs worker shutdownTimeoutMs", DEFAULT_WORKER.shutdownTimeoutMs);
        this._controller = new CancellationController();
        this._running = false;
        this._inFlight = new Set();
        this._loop = null;
        this._registered = false;
        Object.seal(this);
    }

    async _ensureRegistered() {
        if (this._registered) {
            return;
        }
        await this.runtime.storage.registerWorker(this);
        this._registered = true;
    }

    async start() {
        if (this._running) {
            return this;
        }
        await this._ensureRegistered();
        this._running = true;
        this._loop = this._run();
        return this;
    }

    async stop(reason = "worker shutdown") {
        this._controller.cancel(reason);
        this._running = false;
        const deadline = Date.now() + this.shutdownTimeoutMs;
        while (this._inFlight.size > 0 && Date.now() < deadline) {
            await sleep(10, this._controller.signal);
        }
        if (this._loop !== null) {
            await this._loop.catch(() => {});
        }
        await this.runtime.storage.stopWorker(this.id);
        this._registered = false;
        return this;
    }

    async runOnce() {
        await this._ensureRegistered();
        await this.runtime.storage.heartbeat(this.id);
        const jobs = await this.runtime.storage.claim(this, {
            queues: this.queues,
            limit: Math.max(1, this.concurrency - this._inFlight.size),
            leaseMs: this.leaseMs,
        });
        await Promise.all(jobs.map((job) => this._execute(job)));
        return jobs.length;
    }

    async _run() {
        while (this._running && !this._controller.signal.aborted) {
            const count = await this.runOnce();
            await sleep(count === 0 ? this.idleBackoffMs : this.pollIntervalMs, this._controller.signal);
        }
    }

    async _execute(job) {
        const task = this._executeInner(job);
        this._inFlight.add(task);
        try {
            await task;
        } finally {
            this._inFlight.delete(task);
        }
    }

    async _executeInner(job) {
        const definition = this.runtime.definition(job.name);
        if (definition === undefined) {
            await this._persistFailure(job, jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job definition is missing at worker runtime"));
            return;
        }
        let input;
        try {
            input = validatePayload(definition.input, job.payload, "execute", job.name);
        } catch (error) {
            await this._persistFailure(job, error);
            return;
        }
        try {
            const context = Object.freeze({
                id: job.id,
                name: job.name,
                queue: job.queue,
                attempt: job.attemptCount,
                signal: this._controller.signal,
                extendLease: (leaseMs = this.leaseMs) => this.runtime.storage.extendLease(job.id, this.id, leaseMs),
            });
            const result = await runWithTimeout(
                definition.handler,
                context,
                input,
                job.timeoutMs,
                this._controller.signal,
            );
            await this._persistCompletion(job, result);
        } catch (error) {
            await this._persistFailure(job, error);
        }
    }

    async _persistCompletion(job, result) {
        try {
            await this.runtime.storage.complete(job.id, this.id, result);
        } catch (error) {
            if (!terminalPersistenceRace(error)) {
                throw error;
            }
        }
    }

    async _persistFailure(job, error) {
        try {
            await this.runtime.storage.fail(job.id, this.id, error, job.retryPolicy);
        } catch (failureError) {
            if (!terminalPersistenceRace(failureError)) {
                throw failureError;
            }
        }
    }
}

function createAdminService(runtime) {
    return Object.freeze({
        async overview() {
            return Object.freeze({
                jobs: await runtime.storage.statusCounts(),
                workers: (await runtime.storage.listWorkers()).length,
                recurring: (await runtime.storage.listRecurring()).length,
                locks: (await runtime.storage.listLocks()).length,
            });
        },
        listJobs: (filters) => runtime.storage.listJobs(filters),
        getJob: (id) => runtime.storage.getJob(requireName(id, "Sloppy Jobs job id")),
        attempts: (id) => runtime.storage.attempts(requireName(id, "Sloppy Jobs job id")),
        events: (id) => runtime.storage.events(requireName(id, "Sloppy Jobs job id")),
        retry: (id) => runtime.storage.manualRetry(requireName(id, "Sloppy Jobs job id")),
        cancel: (id) => runtime.storage.cancel(requireName(id, "Sloppy Jobs job id")),
        delete: (id) => runtime.storage.delete(requireName(id, "Sloppy Jobs job id")),
        listRecurring: () => runtime.storage.listRecurring(),
        pauseRecurring: (name) => runtime.storage.setRecurringEnabled(requireName(name, "Sloppy Jobs recurring name"), false),
        resumeRecurring: (name) => runtime.storage.setRecurringEnabled(requireName(name, "Sloppy Jobs recurring name"), true),
        triggerRecurring: async (name) => {
            const recurring = await runtime.storage.getRecurring(requireName(name, "Sloppy Jobs recurring name"));
            if (recurring === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "recurring job was not found", { name });
            }
            return await runtime.enqueue(recurring.jobName, recurring.payload, {
                queue: recurring.queue,
                metadata: { recurring: recurring.name, triggered: true },
            });
        },
        listWorkers: () => runtime.storage.listWorkers(),
        listLocks: () => runtime.storage.listLocks(),
        cleanup: (options) => runtime.storage.cleanup(options),
    });
}

function createLockService(storage, owner) {
    return Object.freeze({
        acquire: (name, options = undefined) =>
            storage.acquireLock(requireName(name, "Sloppy Jobs lock name"), owner, options?.ttlMs ?? DEFAULT_LOCK_TTL_MS),
        release: (name) => storage.releaseLock(requireName(name, "Sloppy Jobs lock name"), owner),
        extend: (name, options = undefined) =>
            storage.extendLock(requireName(name, "Sloppy Jobs lock name"), owner, options?.ttlMs ?? DEFAULT_LOCK_TTL_MS),
        async with(name, options, callback) {
            if (typeof options === "function") {
                callback = options;
                options = undefined;
            }
            if (typeof callback !== "function") {
                throw new TypeError("Sloppy Jobs lock callback must be a function.");
            }
            const lockName = requireName(name, "Sloppy Jobs lock name");
            if (!await storage.acquireLock(lockName, owner, options?.ttlMs ?? DEFAULT_LOCK_TTL_MS)) {
                throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is already held", { lockName });
            }
            try {
                return await callback();
            } finally {
                await storage.releaseLock(lockName, owner);
            }
        },
    });
}

function createStorageFactory(provider) {
    return (db, options = undefined) => new SchedulerStorage(db, { ...(options ?? {}), provider });
}

const Jobs = Object.freeze({
    create(options) {
        return new JobsRuntime(options);
    },
    storage: Object.freeze({
        sqlite: createStorageFactory("sqlite"),
        postgres: createStorageFactory("postgres"),
        sqlserver: createStorageFactory("sqlserver"),
        from(db, options = undefined) {
            return new SchedulerStorage(db, options);
        },
    }),
    nextCronRun,
    redactPayload: payloadPreview,
    statuses: Object.freeze(Object.keys(JOB_STATUSES)),
    transitions: VALID_TRANSITIONS,
    schemaVersion: SCHEMA_VERSION,
});

export { Jobs, SloppyJobsError };
