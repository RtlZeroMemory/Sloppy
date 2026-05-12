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

function stableId(prefix = "job") {
    const random = Math.random().toString(36).slice(2, 10);
    return `${prefix}_${Date.now().toString(36)}_${random}`;
}

function sleep(ms, signal = undefined) {
    if (ms <= 0) {
        return Promise.resolve();
    }
    if (signal?.aborted) {
        return Promise.resolve();
    }
    return new Promise((resolve) => {
        const timer = setTimeout(resolve, ms);
        if (signal && typeof signal.addEventListener === "function") {
            const onAbort = () => {
                clearTimeout(timer);
                resolve();
            };
            signal.addEventListener("abort", onAbort);
        }
    });
}

async function runWithTimeout(handler, context, input, timeoutMs, parentSignal) {
    if (timeoutMs === null || timeoutMs === undefined) {
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
        clearTimeout(timer);
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

function sqlForProvider(provider, sql) {
    if (provider !== "postgres" || typeof sql !== "string" || !sql.includes("?")) {
        return sql;
    }
    let parameter = 1;
    let inSingleQuote = false;
    let text = "";
    for (let index = 0; index < sql.length; index += 1) {
        const ch = sql[index];
        if (ch === "'") {
            text += ch;
            if (inSingleQuote && sql[index + 1] === "'") {
                text += sql[index + 1];
                index += 1;
            } else {
                inSingleQuote = !inSingleQuote;
            }
            continue;
        }
        if (ch === "?" && !inSingleQuote) {
            text += `$${parameter}`;
            parameter += 1;
            continue;
        }
        text += ch;
    }
    return text;
}

function adaptProvider(db, provider) {
    if (db?.__sloppyJobsProvider === provider) {
        return db;
    }
    if (db === null || typeof db !== "object") {
        throw new TypeError("Sloppy Jobs storage requires a data connection object.");
    }
    return Object.freeze({
        __sloppyJobsProvider: provider,
        query(sql, params = [], options = undefined) {
            return db.query(sqlForProvider(provider, sql), params, options);
        },
        queryOne(sql, params = [], options = undefined) {
            if (typeof db.queryOne === "function") {
                return db.queryOne(sqlForProvider(provider, sql), params, options);
            }
            return Promise.resolve(db.query(sqlForProvider(provider, sql), params, options)).then((rows) =>
                compactRows(rows)[0] ?? null);
        },
        exec(sql, params = [], options = undefined) {
            const text = sqlForProvider(provider, sql);
            if (typeof db.exec === "function") {
                return db.exec(text, params, options);
            }
            return db.query(text, params, options);
        },
        transaction(callback) {
            if (typeof db.transaction !== "function") {
                return callback(adaptProvider(db, provider));
            }
            return db.transaction((tx) => callback(adaptProvider(tx, provider)));
        },
        __debug() {
            if (typeof db.__debug === "function") {
                return db.__debug();
            }
            return Object.freeze({ kind: `${provider}-connection` });
        },
    });
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

async function queryOne(db, sql, params = []) {
    if (typeof db.queryOne === "function") {
        return await db.queryOne(sql, params);
    }
    const rows = compactRows(await db.query(sql, params));
    return rows[0] ?? null;
}

async function execSql(db, sql, params = []) {
    if (typeof db.exec === "function") {
        return await db.exec(sql, params);
    }
    return await db.query(sql, params);
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

function claimSelectSql(provider, queueCount) {
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
        return `select top (${limitPlaceholder}) * from sloppy_jobs with (updlock, readpast, rowlock) ${where} ${order}`;
    }
    return `select * from sloppy_jobs ${where} ${order} limit ${limitPlaceholder}`;
}

function limitSql(provider, sql, limitPlaceholder) {
    if (provider === "sqlserver") {
        return sql.replace(/^select /iu, `select top (${limitPlaceholder}) `);
    }
    return `${sql} limit ${limitPlaceholder}`;
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
    return Math.max(1, Math.floor(capped * (0.5 + Math.random() * 0.5)));
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
        : current.delayMs !== undefined
            ? addMsIso(nowIso(), nonNegativeInteger(current.delayMs, "Sloppy Jobs delayMs"))
            : nowIso();
    const retry = normalizeRetryPolicy(current.retries ?? current.retry ?? definition?.retries);
    return Object.freeze({
        queue: optionalName(current.queue, "Sloppy Jobs queue", definition?.queue ?? DEFAULT_QUEUE),
        priority: integer(current.priority, "Sloppy Jobs priority", 0),
        runAt,
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
        this.db = adaptProvider(db, this.provider);
        this.clock = opts.clock;
        Object.freeze(this);
    }

    async init() {
        return await inTransaction(this.db, async (tx) => {
            for (const statement of schemaSql(this.provider)) {
                await execSql(tx, statement);
            }
            const row = await queryOne(tx, "select version from sloppy_job_schema");
            if (row === null || row === undefined) {
                await execSql(tx, "insert into sloppy_job_schema (version, updated_at) values (?, ?)", [
                    SCHEMA_VERSION,
                    nowIso(this.clock),
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
            if (record.idempotencyKey !== undefined) {
                const existing = await queryOne(tx, "select * from sloppy_jobs where idempotency_key = ?", [record.idempotencyKey]);
                if (existing !== null && existing !== undefined) {
                    return normalizeJob(existing);
                }
            }
            await execSql(
                tx,
                "insert into sloppy_jobs (id, name, queue, status, payload_json, payload_schema, priority, run_at, created_at, updated_at, locked_by, locked_until, attempt_count, max_attempts, retry_policy_json, next_retry_at, last_error_code, last_error_message, diagnostic_id, correlation_id, idempotency_key, timeout_ms, metadata_json) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                [
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
                ],
            );
            await this.addEvent(tx, record.id, "enqueued", null, record.status, record.createdAt, null, "job enqueued", {
                queue: record.queue,
                runAt: record.runAt,
            });
            return normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [record.id]));
        });
    }

    async getJob(id) {
        return normalizeJob(await queryOne(this.db, "select * from sloppy_jobs where id = ?", [id]));
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
                where.push(`${column} = ?`);
                params.push(current[field]);
            }
        }
        if (current.failed === true) {
            where.push("status in ('failed', 'dead')");
        }
        if (current.createdFrom !== undefined) {
            where.push("created_at >= ?");
            params.push(toIso(current.createdFrom, "createdFrom"));
        }
        if (current.createdTo !== undefined) {
            where.push("created_at <= ?");
            params.push(toIso(current.createdTo, "createdTo"));
        }
        if (current.runAtFrom !== undefined) {
            where.push("run_at >= ?");
            params.push(toIso(current.runAtFrom, "runAtFrom"));
        }
        if (current.runAtTo !== undefined) {
            where.push("run_at <= ?");
            params.push(toIso(current.runAtTo, "runAtTo"));
        }
        const clause = where.length === 0 ? "" : ` where ${where.join(" and ")}`;
        const rows = compactRows(await this.db.query(
            `select * from sloppy_jobs${clause} order by created_at desc, id desc limit ? offset ?`,
            [...params, pageSize, offset],
        ));
        return Object.freeze(rows.map(normalizeJob));
    }

    async statusCounts() {
        const rows = compactRows(await this.db.query("select status, count(*) as count from sloppy_jobs group by status", []));
        const counts = {};
        for (const status of Object.keys(JOB_STATUSES)) {
            counts[status] = 0;
        }
        for (const row of rows) {
            if (JOB_STATUSES[row.status] === true) {
                counts[row.status] = Number(row.count ?? row["count(*)"] ?? 0);
            }
        }
        return Object.freeze(counts);
    }

    async attempts(jobId) {
        const rows = compactRows(await this.db.query(
            "select * from sloppy_job_attempts where job_id = ? order by attempt_number",
            [jobId],
        ));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row })));
    }

    async events(jobId) {
        const rows = compactRows(await this.db.query(
            "select * from sloppy_job_events where job_id = ? order by created_at, id",
            [jobId],
        ));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row, data: jsonParse(row.data_json, {}) })));
    }

    async transition(id, to, context = undefined) {
        const ctx = context === undefined ? {} : context;
        if (JOB_STATUSES[to] !== true) {
            throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "target job status is invalid", { to });
        }
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [id]));
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
            const timestamp = nowIso(this.clock);
            await execSql(tx, "update sloppy_jobs set status = ?, updated_at = ? where id = ?", [to, timestamp, id]);
            await this.addEvent(tx, id, ctx.eventType ?? "transition", job.status, to, timestamp, ctx.workerId ?? null, ctx.message ?? null, ctx.data ?? {});
            return normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [id]));
        });
    }

    async registerWorker(worker) {
        const timestamp = nowIso(this.clock);
        const existing = await queryOne(this.db, "select * from sloppy_job_workers where id = ?", [worker.id]);
        if (existing !== null && existing !== undefined) {
            await execSql(
                this.db,
                "update sloppy_job_workers set worker_name = ?, host = ?, pid = ?, queues = ?, started_at = ?, last_heartbeat_at = ?, status = ? where id = ?",
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
            "insert into sloppy_job_workers (id, worker_name, host, pid, queues, started_at, last_heartbeat_at, status) values (?, ?, ?, ?, ?, ?, ?, ?)",
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
        await execSql(this.db, "update sloppy_job_workers set last_heartbeat_at = ?, status = ? where id = ?", [
            nowIso(this.clock),
            "running",
            workerId,
        ]);
    }

    async stopWorker(workerId) {
        await execSql(this.db, "update sloppy_job_workers set last_heartbeat_at = ?, status = ? where id = ?", [
            nowIso(this.clock),
            "stopped",
            workerId,
        ]);
    }

    async listWorkers() {
        const rows = compactRows(await this.db.query("select * from sloppy_job_workers order by started_at desc"));
        return Object.freeze(rows.map((row) => Object.freeze({
            ...row,
            queues: jsonParse(row.queues, []),
        })));
    }

    async claim(worker, options = undefined) {
        const opts = options === undefined ? {} : options;
        const limit = positiveInteger(opts.limit, "Sloppy Jobs claim limit", worker.concurrency ?? 1);
        const queues = Object.freeze((opts.queues ?? worker.queues ?? [DEFAULT_QUEUE]).map((queue) => requireName(queue, "Sloppy Jobs queue")));
        const timestamp = nowIso(this.clock);
        const lockedUntil = addMsIso(timestamp, positiveInteger(opts.leaseMs, "Sloppy Jobs leaseMs", DEFAULT_WORKER.leaseMs));
        return await inTransaction(this.db, async (tx) => {
            await execSql(tx, "update sloppy_jobs set status = 'queued', locked_by = null, locked_until = null, updated_at = ? where status = 'processing' and locked_until <= ?", [
                timestamp,
                timestamp,
            ]);
            await execSql(tx, "update sloppy_jobs set status = 'queued', updated_at = ? where status in ('scheduled', 'retrying') and run_at <= ?", [
                timestamp,
                timestamp,
            ]);
            const rows = compactRows(await tx.query(
                claimSelectSql(this.provider, queues.length),
                [...queues, timestamp, limit],
            ));
            const claimed = [];
            for (const row of rows) {
                const job = normalizeJob(row);
                const attemptNumber = job.attemptCount + 1;
                const result = await execSql(
                    tx,
                    "update sloppy_jobs set status = 'processing', locked_by = ?, locked_until = ?, attempt_count = ?, updated_at = ? where id = ? and status = 'queued'",
                    [worker.id, lockedUntil, attemptNumber, timestamp, job.id],
                );
                void result;
                const refreshed = normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [job.id]));
                if (refreshed?.status === "processing" && refreshed.lockedBy === worker.id) {
                    const attemptId = stableId("attempt");
                    await execSql(
                        tx,
                        "insert into sloppy_job_attempts (id, job_id, worker_id, attempt_number, started_at, finished_at, status, duration_ms, error_code, error_message, diagnostic_id) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
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
            const job = normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
            if (job === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job was not found", { jobId });
            }
            if (job.status !== "processing") {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only processing jobs can fail", {
                    jobId,
                    status: job.status,
                });
            }
            const timestamp = nowIso(this.clock);
            const policy = retryPolicy ?? jsonParse(job.retryPolicyJson, { maxAttempts: job.maxAttempts, backoff: "none", initialDelayMs: 0, maxDelayMs: 0 });
            const exhausted = job.attemptCount >= job.maxAttempts;
            const next = exhausted ? "dead" : "retrying";
            const nextRunAt = exhausted ? null : addMsIso(timestamp, retryDelayMs(policy, job.attemptCount));
            await execSql(
                tx,
                "update sloppy_jobs set status = ?, locked_by = null, locked_until = null, updated_at = ?, next_retry_at = ?, run_at = coalesce(?, run_at), last_error_code = ?, last_error_message = ?, diagnostic_id = ? where id = ?",
                [next, timestamp, nextRunAt, nextRunAt, code, message, stableId("diag"), jobId],
            );
            await execSql(
                tx,
                "update sloppy_job_attempts set finished_at = ?, status = ?, error_code = ?, error_message = ?, diagnostic_id = ? where job_id = ? and attempt_number = ?",
                [timestamp, next, code, message, stableId("diag"), jobId, job.attemptCount],
            );
            await this.addEvent(tx, jobId, exhausted ? "dead" : "retrying", "processing", next, timestamp, workerId, message, {
                errorCode: code,
                exhausted,
                nextRunAt,
            });
            return normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
        });
    }

    async finishProcessing(jobId, workerId, status, error = undefined, result = undefined) {
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
            if (job === null) {
                throw jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job was not found", { jobId });
            }
            if (job.status !== "processing") {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only processing jobs can be completed", {
                    jobId,
                    status: job.status,
                });
            }
            const timestamp = nowIso(this.clock);
            await execSql(
                tx,
                "update sloppy_jobs set status = ?, locked_by = null, locked_until = null, updated_at = ? where id = ?",
                [status, timestamp, jobId],
            );
            await execSql(
                tx,
                "update sloppy_job_attempts set finished_at = ?, status = ?, duration_ms = ?, error_code = ?, error_message = ? where job_id = ? and attempt_number = ?",
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
            return normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
        });
    }

    async extendLease(jobId, workerId, leaseMs) {
        const lockedUntil = addMsIso(nowIso(this.clock), positiveInteger(leaseMs, "Sloppy Jobs leaseMs"));
        await execSql(
            this.db,
            "update sloppy_jobs set locked_until = ?, updated_at = ? where id = ? and locked_by = ? and status = 'processing'",
            [lockedUntil, nowIso(this.clock), jobId, workerId],
        );
        return lockedUntil;
    }

    async manualRetry(jobId) {
        return await inTransaction(this.db, async (tx) => {
            const job = normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
            if (job === null || !["dead", "failed"].includes(job.status)) {
                throw jobsError("SLOPPY_E_JOBS_TRANSITION_INVALID", "only failed or dead jobs can be manually retried", {
                    jobId,
                    status: job?.status,
                });
            }
            const timestamp = nowIso(this.clock);
            await execSql(tx, "update sloppy_jobs set status = 'queued', locked_by = null, locked_until = null, run_at = ?, updated_at = ? where id = ?", [
                timestamp,
                timestamp,
                jobId,
            ]);
            await this.addEvent(tx, jobId, "manual-retry", job.status, "queued", timestamp, null, "job manually retried", {});
            return normalizeJob(await queryOne(tx, "select * from sloppy_jobs where id = ?", [jobId]));
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
            "insert into sloppy_job_events (id, job_id, event_type, from_status, to_status, created_at, worker_id, message, data_json) values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            [stableId("event"), jobId, eventType, from, to, createdAt, workerId, message, jsonStringify(data ?? {}, "job event data")],
        );
    }

    async upsertRecurring(config) {
        const timestamp = nowIso(this.clock);
        const existing = await queryOne(this.db, "select * from sloppy_recurring_jobs where name = ?", [config.name]);
        if (existing === null || existing === undefined) {
            await execSql(
                this.db,
                "insert into sloppy_recurring_jobs (id, name, job_name, queue, cron, timezone, payload_json, enabled, misfire_policy, last_run_at, next_run_at, created_at, updated_at, metadata_json) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
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
                "update sloppy_recurring_jobs set job_name = ?, queue = ?, cron = ?, timezone = ?, payload_json = ?, enabled = ?, misfire_policy = ?, next_run_at = ?, updated_at = ?, metadata_json = ? where name = ?",
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
        return normalizeRecurring(await queryOne(this.db, "select * from sloppy_recurring_jobs where name = ?", [name]));
    }

    async listRecurring() {
        const rows = compactRows(await this.db.query("select * from sloppy_recurring_jobs order by name"));
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
            ),
            [now, pageSize],
        ));
        return Object.freeze(rows.map(normalizeRecurring));
    }

    async markRecurringTicked(name, lastRunAt, nextRunAt) {
        await execSql(this.db, "update sloppy_recurring_jobs set last_run_at = ?, next_run_at = ?, updated_at = ? where name = ?", [
            lastRunAt,
            nextRunAt,
            nowIso(this.clock),
            name,
        ]);
        return await this.getRecurring(name);
    }

    async setRecurringEnabled(name, enabled) {
        await execSql(this.db, "update sloppy_recurring_jobs set enabled = ?, updated_at = ? where name = ?", [
            enabled ? 1 : 0,
            nowIso(this.clock),
            name,
        ]);
        return await this.getRecurring(name);
    }

    async acquireLock(name, owner, ttlMs = DEFAULT_LOCK_TTL_MS) {
        const now = nowIso(this.clock);
        const lockedUntil = addMsIso(now, ttlMs);
        return await inTransaction(this.db, async (tx) => {
            const existing = await queryOne(tx, "select * from sloppy_job_locks where name = ?", [name]);
            if (existing !== null && existing !== undefined && existing.locked_until > now && existing.owner !== owner) {
                return false;
            }
            if (existing === null || existing === undefined) {
                await execSql(tx, "insert into sloppy_job_locks (name, owner, locked_until, updated_at) values (?, ?, ?, ?)", [
                    name,
                    owner,
                    lockedUntil,
                    now,
                ]);
            } else {
                await execSql(tx, "update sloppy_job_locks set owner = ?, locked_until = ?, updated_at = ? where name = ?", [
                    owner,
                    lockedUntil,
                    now,
                    name,
                ]);
            }
            return true;
        });
    }

    async releaseLock(name, owner) {
        const existing = await queryOne(this.db, "select * from sloppy_job_locks where name = ?", [name]);
        if (existing === null || existing === undefined) {
            return false;
        }
        if (existing.owner !== owner) {
            throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is owned by another owner", { name });
        }
        await execSql(this.db, "delete from sloppy_job_locks where name = ? and owner = ?", [name, owner]);
        return true;
    }

    async extendLock(name, owner, ttlMs = DEFAULT_LOCK_TTL_MS) {
        const lockedUntil = addMsIso(nowIso(this.clock), ttlMs);
        const existing = await queryOne(this.db, "select * from sloppy_job_locks where name = ?", [name]);
        if (existing === null || existing === undefined || existing.owner !== owner) {
            throw jobsError("SLOPPY_E_JOBS_LOCK_CONFLICT", "lock is not owned by caller", { name });
        }
        await execSql(this.db, "update sloppy_job_locks set locked_until = ?, updated_at = ? where name = ? and owner = ?", [
            lockedUntil,
            nowIso(this.clock),
            name,
            owner,
        ]);
        return lockedUntil;
    }

    async listLocks() {
        const rows = compactRows(await this.db.query("select * from sloppy_job_locks order by name"));
        return Object.freeze(rows.map((row) => Object.freeze({ ...row })));
    }

    async cleanup(options = undefined) {
        const current = options === undefined ? {} : requirePlainObject(options, "Sloppy Jobs cleanup options");
        const batchSize = Math.min(MAX_PAGE_SIZE, positiveInteger(current.batchSize, "Sloppy Jobs cleanup batchSize", DEFAULT_PAGE_SIZE));
        const terminalStatuses = Object.freeze(["succeeded", "dead", "cancelled"]);
        const now = nowIso(this.clock);
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
            ),
            [succeededBefore, deadBefore, cancelledBefore, batchSize],
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

function normalizeRecurringOptions(name, jobName, payload, options = undefined) {
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
        nextRunAt: nextCronRun(cron),
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
        const timestamp = nowIso(this.storage.clock);
        const status = enqueue.runAt > timestamp ? "scheduled" : "queued";
        return await this.storage.enqueue({
            id: stableId("job"),
            name: jobName,
            queue: enqueue.queue,
            status,
            payloadJson: jsonStringify(validatedPayload, "job payload"),
            payloadSchema: definition.input?.metadata?.name ?? definition.input?.kind ?? null,
            priority: enqueue.priority,
            runAt: enqueue.runAt,
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
        const config = normalizeRecurringOptions(name, jobName, payload, options);
        return await this.storage.upsertRecurring(config);
    }

    async tickRecurring(options = undefined) {
        const owner = options?.owner ?? stableId("recurring");
        const acquired = await this.storage.acquireLock("sloppy.jobs.recurring.tick", owner, options?.ttlMs ?? DEFAULT_LOCK_TTL_MS);
        if (!acquired) {
            return Object.freeze([]);
        }
        try {
            const now = nowIso(this.storage.clock);
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
        Object.seal(this);
    }

    async start() {
        if (this._running) {
            return this;
        }
        await this.runtime.storage.registerWorker(this);
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
        return this;
    }

    async runOnce() {
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
            await this.runtime.storage.fail(job.id, this.id, jobsError("SLOPPY_E_JOBS_UNKNOWN_JOB", "job definition is missing at worker runtime"), job.retryPolicy);
            return;
        }
        let input;
        try {
            input = validatePayload(definition.input, job.payload, "execute", job.name);
        } catch (error) {
            await this.runtime.storage.fail(job.id, this.id, error, job.retryPolicy);
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
            await this.runtime.storage.complete(job.id, this.id, result);
        } catch (error) {
            await this.runtime.storage.fail(job.id, this.id, error, job.retryPolicy);
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
