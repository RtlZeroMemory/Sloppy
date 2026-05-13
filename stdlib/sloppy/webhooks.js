import { isPlainObject } from "./internal/validation.js";
import { Hex, Text } from "./codec.js";
import { Hash, Hmac, Random, Secret } from "./crypto.js";
import { headerValue } from "./internal/headers.js";

const WEBHOOKS_TOKEN_PREFIX = "webhooks";
const DEFAULT_WEBHOOKS_TOKEN = WEBHOOKS_TOKEN_PREFIX;
const DEFAULT_BATCH_SIZE = 100;
const DEFAULT_LEASE_MS = 30000;
const DEFAULT_MAX_PAYLOAD_BYTES = 1024 * 1024;
const DEFAULT_MAX_RESPONSE_PREVIEW_BYTES = 4096;
const DEFAULT_TIMESTAMP_TOLERANCE_MS = 300000;
const EVENT_NAME_PATTERN = /^[a-z][a-z0-9]*(?:\.[a-z][a-z0-9]*)+$/u;
const TOKEN_NAME_PATTERN = /^[A-Za-z][A-Za-z0-9_.-]{0,127}$/u;
const PRIVATE_HOSTS = new Set(["localhost", "127.0.0.1", "::1", "[::1]", "0.0.0.0"]);
const SENSITIVE_HEADER_PATTERN = /^(authorization|cookie|set-cookie|x-api-key|api-key|sloppy-webhook-signature)$/iu;
const TERMINAL_STATUSES = new Set(["delivered", "dead_letter"]);
const RETRY_STATUSES = new Set([408, 425, 429, 500, 502, 503, 504]);
let fallbackIdCounter = 0;

class SloppyWebhookError extends Error {
    constructor(code, message, options = undefined) {
        super(`${code}: ${message}`);
        this.name = "SloppyWebhookError";
        this.code = code;
        if (options?.cause !== undefined) {
            this.cause = options.cause;
        }
        if (options !== undefined) {
            for (const [key, value] of Object.entries(options)) {
                if (key !== "cause") {
                    this[key] = value;
                }
            }
        }
    }
}

function webhookError(code, message, options = undefined) {
    return new SloppyWebhookError(code, message, options);
}

function stableToken(name = undefined) {
    if (name === undefined || name === null || name === "") {
        return DEFAULT_WEBHOOKS_TOKEN;
    }
    if (typeof name !== "string" || !TOKEN_NAME_PATTERN.test(name)) {
        throw new TypeError("Sloppy Webhooks token name must start with a letter and contain only letters, digits, '.', '_', or '-'.");
    }
    return `${WEBHOOKS_TOKEN_PREFIX}.${name}`;
}

function requirePositiveInteger(value, subject, fallback = undefined) {
    if (value === undefined) {
        if (fallback !== undefined) {
            return fallback;
        }
        throw new TypeError(`Sloppy Webhooks ${subject} is required.`);
    }
    if (!Number.isInteger(value) || value <= 0) {
        throw new TypeError(`Sloppy Webhooks ${subject} must be a positive integer.`);
    }
    return value;
}

function requireNonNegativeInteger(value, subject, fallback = undefined) {
    if (value === undefined) {
        return fallback;
    }
    if (!Number.isInteger(value) || value < 0) {
        throw new TypeError(`Sloppy Webhooks ${subject} must be a non-negative integer.`);
    }
    return value;
}

function validateEventName(name) {
    if (typeof name !== "string" || !EVENT_NAME_PATTERN.test(name)) {
        throw new TypeError("Sloppy Webhooks event name must be a stable dotted identifier such as 'order.created'.");
    }
    return name;
}

function isSchema(value) {
    return value !== null && typeof value === "object" && typeof value.validate === "function";
}

function validateSchema(schema, subject) {
    if (!isSchema(schema)) {
        throw new TypeError(`Sloppy Webhooks ${subject} schema must be a Sloppy schema.`);
    }
    return schema;
}

function event(name, options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.event options must be a plain object.");
    }
    const version = requirePositiveInteger(options.version, "event version");
    const schema = validateSchema(options.schema, "event");
    const descriptor = {
        __sloppyWebhookEvent: true,
        name: validateEventName(name),
        version,
        schema,
        validate(payload) {
            const result = schema.validate(payload);
            if (!result.ok) {
                throw webhookError(
                    "SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED",
                    `Webhook event '${descriptor.name}' payload failed validation.`,
                    { issues: result.issues },
                );
            }
            return result.value;
        },
    };
    return Object.freeze(descriptor);
}

function normalizeRetryStatus(values, fallback) {
    const source = values ?? fallback;
    if (!Array.isArray(source)) {
        throw new TypeError("Sloppy Webhooks retry status list must be an array.");
    }
    return Object.freeze(source.map((status) => {
        if (!Number.isInteger(status) || status < 100 || status > 599) {
            throw new TypeError("Sloppy Webhooks retry status values must be valid HTTP statuses.");
        }
        return status;
    }));
}

function retryFixed(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.retry.fixed options must be a plain object.");
    }
    return Object.freeze({
        kind: "fixed",
        maxAttempts: requirePositiveInteger(options.maxAttempts, "retry maxAttempts", 3),
        delayMs: requireNonNegativeInteger(options.delayMs, "retry delayMs", 1000),
        retryOnStatus: normalizeRetryStatus(options.retryOnStatus, [408, 425, 429, 500, 502, 503, 504]),
        jitter: options.jitter !== false,
    });
}

function retryExponential(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.retry.exponential options must be a plain object.");
    }
    const initialDelayMs = requireNonNegativeInteger(options.initialDelayMs, "retry initialDelayMs", 1000);
    const maxDelayMs = requireNonNegativeInteger(options.maxDelayMs, "retry maxDelayMs", 300000);
    if (maxDelayMs < initialDelayMs) {
        throw new TypeError("Sloppy Webhooks retry maxDelayMs must be at least initialDelayMs.");
    }
    return Object.freeze({
        kind: "exponential",
        maxAttempts: requirePositiveInteger(options.maxAttempts, "retry maxAttempts", 8),
        initialDelayMs,
        maxDelayMs,
        retryOnStatus: normalizeRetryStatus(options.retryOnStatus, [408, 425, 429, 500, 502, 503, 504]),
        jitter: options.jitter !== false,
    });
}

function resolveSecretValue(secret, config = undefined, subject = "secret") {
    if (secret === undefined || secret === null) {
        throw webhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", `Webhook ${subject} is required.`);
    }
    if (typeof secret === "string") {
        if (secret.length === 0) {
            throw webhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", `Webhook ${subject} must not be empty.`);
        }
        return secret;
    }
    if (secret?.__sloppyConfigReference === true && typeof secret.key === "string") {
        if (config === undefined || typeof config.require !== "function") {
            throw webhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", `Webhook ${subject} requires app config.`);
        }
        const resolved = config.require(secret.key);
        if (typeof resolved !== "string" || resolved.length === 0) {
            throw webhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", `Webhook ${subject} config value is empty.`);
        }
        return resolved;
    }
    if (typeof secret.bytes === "function") {
        return secret;
    }
    throw new TypeError("Sloppy Webhooks secret must be a string, Secret, or Config.requiredSecret reference.");
}

function secretForHmac(secret, config, subject) {
    const resolved = resolveSecretValue(secret, config, subject);
    if (typeof resolved === "string") {
        const owned = Secret.fromUtf8(resolved);
        return Object.freeze({
            secret: owned,
            dispose() {
                owned.dispose();
            },
        });
    }
    return Object.freeze({
        secret: resolved,
        dispose() {},
    });
}

function redactedSecretRef(secret) {
    if (typeof secret === "string") {
        return "[redacted]";
    }
    if (secret?.__sloppyConfigReference === true) {
        return `config:${secret.key}`;
    }
    return "[redacted]";
}

function serializeSubscriptionSecret(secret) {
    if (secret === undefined) {
        return null;
    }
    if (typeof secret === "string") {
        if (secret.length === 0) {
            throw webhookError("SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE", "Webhook subscription secret must not be empty.");
        }
        return JSON.stringify({ kind: "literal", value: secret });
    }
    if (secret?.__sloppyConfigReference === true && typeof secret.key === "string") {
        return JSON.stringify({ kind: "config", key: secret.key });
    }
    throw new TypeError("Sloppy Webhooks subscription secret must be a durable string or Config.requiredSecret reference.");
}

function subscriptionSecretFromStored(value, fallback) {
    if (value === undefined || value === null || value === "") {
        return fallback;
    }
    try {
        const parsed = JSON.parse(value);
        if (parsed?.kind === "literal" && typeof parsed.value === "string" && parsed.value.length !== 0) {
            return parsed.value;
        }
        if (parsed?.kind === "config" && typeof parsed.key === "string" && parsed.key.length !== 0) {
            return Object.freeze({ __sloppyConfigReference: true, key: parsed.key });
        }
    } catch {
        return fallback;
    }
    return fallback;
}

function validateHeaderMap(headers, subject) {
    if (headers === undefined) {
        return Object.freeze({});
    }
    if (!isPlainObject(headers)) {
        throw new TypeError(`Sloppy Webhooks ${subject} headers must be a plain object.`);
    }
    const normalized = {};
    for (const [name, value] of Object.entries(headers)) {
        if (!/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u.test(name)) {
            throw new TypeError(`Sloppy Webhooks ${subject} header names must be safe HTTP tokens.`);
        }
        if (SENSITIVE_HEADER_PATTERN.test(name)) {
            throw new TypeError(`Sloppy Webhooks ${subject} headers must not override sensitive delivery headers.`);
        }
        if (typeof value !== "string" || /[\x00-\x08\x0A-\x1F\x7F]/u.test(value)) {
            throw new TypeError(`Sloppy Webhooks ${subject} header values must be safe strings.`);
        }
        normalized[name] = value;
    }
    return Object.freeze(normalized);
}

function isPrivateHostname(hostname) {
    const host = hostname.toLowerCase();
    if (PRIVATE_HOSTS.has(host)) {
        return true;
    }
    const bareHost = host.startsWith("[") && host.endsWith("]") ? host.slice(1, -1) : host;
    if (PRIVATE_HOSTS.has(bareHost)) {
        return true;
    }
    if (/^10\./u.test(host) || /^192\.168\./u.test(host)) {
        return true;
    }
    const matchLoopback = /^127\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/u.exec(host);
    if (matchLoopback !== null) {
        return matchLoopback.slice(1).every((part) => Number(part) <= 255);
    }
    const match172 = /^172\.(\d{1,3})\./u.exec(host);
    if (match172 !== null) {
        const part = Number(match172[1]);
        return part >= 16 && part <= 31;
    }
    return /^169\.254\./u.test(host) ||
        /^fc[0-9a-f]{2}:/u.test(bareHost) ||
        /^fd[0-9a-f]{2}:/u.test(bareHost) ||
        /^fe[89ab][0-9a-f]:/u.test(bareHost);
}

function validateEndpointUrl(url, options = {}) {
    if (typeof url !== "string" || url.length === 0) {
        throw new TypeError("Sloppy Webhooks subscription URL must be a non-empty absolute URL.");
    }
    let parsed;
    try {
        parsed = new URL(url);
    } catch {
        throw new TypeError("Sloppy Webhooks subscription URL must be an absolute http:// or https:// URL.");
    }
    if (parsed.protocol !== "https:" && parsed.protocol !== "http:") {
        throw new TypeError("Sloppy Webhooks subscription URL must use http:// or https://.");
    }
    if (parsed.username.length !== 0 || parsed.password.length !== 0 || parsed.hash.length !== 0) {
        throw new TypeError("Sloppy Webhooks subscription URL must not include userinfo or a fragment.");
    }
    if (options.allowPrivateNetworks !== true && isPrivateHostname(parsed.hostname)) {
        throw webhookError(
            "SLOPPY_E_WEBHOOK_INVALID_OPTIONS",
            "Webhook subscription URL targets a private or loopback network. Set allowPrivateNetworks explicitly for trusted local receivers.",
        );
    }
    return parsed.toString();
}

function providerKind(db, configured = undefined) {
    const debug = typeof db?.__debug === "function" ? db.__debug() : undefined;
    if (configured === "test" || configured === "fake-test") {
        if (debug?.kind === "fake-data-provider") {
            return "sqlite";
        }
        throw webhookError(
            "SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE",
            "Webhook test providerKind can only be used with an explicit fake test provider.",
        );
    }
    if (configured !== undefined) {
        return configured;
    }
    if (debug?.kind === "fake-data-provider") {
        if (debug.webhooksTestProvider === true) {
            return "sqlite";
        }
        throw webhookError(
            "SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE",
            "Webhook outbox requires a real data provider; fake providers must opt in with providerKind: 'test'.",
        );
    }
    if (debug?.kind === "sqlite-connection" || debug?.provider === "sqlite") {
        return "sqlite";
    }
    if (debug?.kind === "postgres-connection" || debug?.provider === "postgres") {
        return "postgres";
    }
    if (debug?.kind === "sqlserver-connection" || debug?.provider === "sqlserver") {
        return "sqlserver";
    }
    if (debug?.kind === "fake-data-provider" && typeof debug.placeholderStyle === "string") {
        return debug.placeholderStyle === "postgres" ? "postgres" : "sqlite";
    }
    throw webhookError(
        "SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE",
        "Webhook outbox requires a sqlite, postgres, or sqlserver data provider.",
    );
}

const SQL = {
    sqlite: Object.freeze({
        placeholders: "question",
        now: "datetime('now')",
        createSubscriptions:
            "create table if not exists sloppy_webhook_subscriptions (" +
            "id text primary key, tenant_id text null, event_name text not null, endpoint_url text not null, " +
            "secret_ref text null, enabled integer not null, headers_json text not null, " +
            "allow_private_networks integer not null default 0, created_at text not null, updated_at text not null)",
        createOutbox:
            "create table if not exists sloppy_webhook_outbox (" +
            "id text primary key, event_name text not null, event_version integer not null, payload_json text not null, " +
            "payload_hash text not null, occurred_at text not null, available_at text not null, status text not null, " +
            "attempt_count integer not null, max_attempts integer not null, next_attempt_at text null, locked_by text null, " +
            "locked_until text null, idempotency_key text null, metadata_json text not null, tenant_id text null, " +
            "created_at text not null, updated_at text not null)",
        createAttempts:
            "create table if not exists sloppy_webhook_delivery_attempts (" +
            "id text primary key, outbox_id text not null, subscription_id text not null, delivery_id text not null, " +
            "attempt_number integer not null, status text not null, status_code integer null, error_code text null, " +
            "error_message_redacted text null, request_headers_redacted_json text not null, response_body_preview text null, " +
            "duration_ms integer not null, attempted_at text not null)",
        createInboundDedup:
            "create table if not exists sloppy_webhook_inbound_dedup (" +
            "id text primary key, provider text not null, delivery_id text not null, seen_at text not null, expires_at text not null)",
        indexes: Object.freeze([
            "create index if not exists idx_sloppy_webhook_subscriptions_event_enabled on sloppy_webhook_subscriptions (event_name, enabled)",
            "create index if not exists idx_sloppy_webhook_outbox_pending on sloppy_webhook_outbox (status, available_at, next_attempt_at, locked_until)",
            "create index if not exists idx_sloppy_webhook_attempts_outbox on sloppy_webhook_delivery_attempts (outbox_id, subscription_id, attempted_at)",
            "create unique index if not exists ux_sloppy_webhook_inbound_dedup_provider_delivery on sloppy_webhook_inbound_dedup (provider, delivery_id)",
            "create unique index if not exists ux_sloppy_webhook_outbox_idempotency on sloppy_webhook_outbox (idempotency_key) where idempotency_key is not null",
        ]),
        insertSubscription: "insert into sloppy_webhook_subscriptions (id, tenant_id, event_name, endpoint_url, secret_ref, enabled, headers_json, allow_private_networks, created_at, updated_at) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        updateSubscription: "update sloppy_webhook_subscriptions set endpoint_url = ?, secret_ref = ?, headers_json = ?, allow_private_networks = ?, updated_at = ? where id = ?",
        setSubscriptionEnabled: "update sloppy_webhook_subscriptions set enabled = ?, updated_at = ? where id = ?",
        deleteSubscription: "delete from sloppy_webhook_subscriptions where id = ?",
        getSubscription: "select id, tenant_id as tenantId, event_name as eventName, endpoint_url as endpointUrl, enabled, headers_json as headersJson, allow_private_networks as allowPrivateNetworks, created_at as createdAt, updated_at as updatedAt from sloppy_webhook_subscriptions where id = ?",
        getSubscriptionSecret: "select secret_ref as secretRef from sloppy_webhook_subscriptions where id = ?",
        listSubscriptions: "select id, tenant_id as tenantId, event_name as eventName, endpoint_url as endpointUrl, enabled, headers_json as headersJson, allow_private_networks as allowPrivateNetworks, created_at as createdAt, updated_at as updatedAt from sloppy_webhook_subscriptions where (? is null or event_name = ?) order by created_at, id",
        insertOutbox: "insert into sloppy_webhook_outbox (id, event_name, event_version, payload_json, payload_hash, occurred_at, available_at, status, attempt_count, max_attempts, next_attempt_at, locked_by, locked_until, idempotency_key, metadata_json, tenant_id, created_at, updated_at) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        selectOutboxByIdempotencyKey: "select id, event_name as eventName, event_version as eventVersion, payload_hash as payloadHash from sloppy_webhook_outbox where idempotency_key = ?",
        selectPendingOutbox: "select id, event_name as eventName, event_version as eventVersion, payload_json as payloadJson, payload_hash as payloadHash, status, attempt_count as attemptCount, max_attempts as maxAttempts, tenant_id as tenantId from sloppy_webhook_outbox where status in ('pending', 'failed') and available_at <= ? and (next_attempt_at is null or next_attempt_at <= ?) and (locked_until is null or locked_until <= ?) order by available_at, id limit ?",
        claimOutbox: "update sloppy_webhook_outbox set status = 'delivering', locked_by = ?, locked_until = ?, updated_at = ? where id = ? and status in ('pending', 'failed') and (locked_until is null or locked_until <= ?)",
        selectClaimedOutbox: "select id, event_name as eventName, event_version as eventVersion, payload_json as payloadJson, payload_hash as payloadHash, status, attempt_count as attemptCount, max_attempts as maxAttempts, tenant_id as tenantId from sloppy_webhook_outbox where id = ? and locked_by = ? and status = 'delivering'",
        subscriptionsForEvent: "select id, tenant_id as tenantId, event_name as eventName, endpoint_url as endpointUrl, secret_ref as secretRef, enabled, headers_json as headersJson from sloppy_webhook_subscriptions where event_name = ? and enabled = 1",
        insertAttempt: "insert into sloppy_webhook_delivery_attempts (id, outbox_id, subscription_id, delivery_id, attempt_number, status, status_code, error_code, error_message_redacted, request_headers_redacted_json, response_body_preview, duration_ms, attempted_at) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        deliveredSubscriptionsForOutbox: "select distinct subscription_id as subscriptionId from sloppy_webhook_delivery_attempts where outbox_id = ? and status = 'delivered'",
        updateOutboxDelivered: "update sloppy_webhook_outbox set status = ?, attempt_count = ?, locked_by = null, locked_until = null, next_attempt_at = ?, updated_at = ? where id = ?",
        insertInboundDedup: "insert into sloppy_webhook_inbound_dedup (id, provider, delivery_id, seen_at, expires_at) values (?, ?, ?, ?, ?)",
    }),
};

SQL.postgres = Object.freeze({
    ...SQL.sqlite,
    placeholders: "postgres",
    createSubscriptions:
        "create table if not exists sloppy_webhook_subscriptions (" +
        "id text primary key, tenant_id text null, event_name text not null, endpoint_url text not null, " +
        "secret_ref text null, enabled boolean not null, headers_json text not null, " +
        "allow_private_networks boolean not null default false, created_at text not null, updated_at text not null)",
    insertSubscription: "insert into sloppy_webhook_subscriptions (id, tenant_id, event_name, endpoint_url, secret_ref, enabled, headers_json, allow_private_networks, created_at, updated_at) values ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
    updateSubscription: "update sloppy_webhook_subscriptions set endpoint_url = $1, secret_ref = $2, headers_json = $3, allow_private_networks = $4, updated_at = $5 where id = $6",
    setSubscriptionEnabled: "update sloppy_webhook_subscriptions set enabled = $1, updated_at = $2 where id = $3",
    deleteSubscription: "delete from sloppy_webhook_subscriptions where id = $1",
    getSubscription: "select id, tenant_id as \"tenantId\", event_name as \"eventName\", endpoint_url as \"endpointUrl\", enabled, headers_json as \"headersJson\", allow_private_networks as \"allowPrivateNetworks\", created_at as \"createdAt\", updated_at as \"updatedAt\" from sloppy_webhook_subscriptions where id = $1",
    getSubscriptionSecret: "select secret_ref as \"secretRef\" from sloppy_webhook_subscriptions where id = $1",
    listSubscriptions: "select id, tenant_id as \"tenantId\", event_name as \"eventName\", endpoint_url as \"endpointUrl\", enabled, headers_json as \"headersJson\", allow_private_networks as \"allowPrivateNetworks\", created_at as \"createdAt\", updated_at as \"updatedAt\" from sloppy_webhook_subscriptions where ($1 is null or event_name = $2) order by created_at, id",
    insertOutbox: "insert into sloppy_webhook_outbox (id, event_name, event_version, payload_json, payload_hash, occurred_at, available_at, status, attempt_count, max_attempts, next_attempt_at, locked_by, locked_until, idempotency_key, metadata_json, tenant_id, created_at, updated_at) values ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18)",
    selectOutboxByIdempotencyKey: "select id, event_name as \"eventName\", event_version as \"eventVersion\", payload_hash as \"payloadHash\" from sloppy_webhook_outbox where idempotency_key = $1",
    selectPendingOutbox: "select id, event_name as \"eventName\", event_version as \"eventVersion\", payload_json as \"payloadJson\", payload_hash as \"payloadHash\", status, attempt_count as \"attemptCount\", max_attempts as \"maxAttempts\", tenant_id as \"tenantId\" from sloppy_webhook_outbox where status in ('pending', 'failed') and available_at <= $1 and (next_attempt_at is null or next_attempt_at <= $2) and (locked_until is null or locked_until <= $3) order by available_at, id limit $4",
    claimOutbox: "update sloppy_webhook_outbox set status = 'delivering', locked_by = $1, locked_until = $2, updated_at = $3 where id = $4 and status in ('pending', 'failed') and (locked_until is null or locked_until <= $5)",
    selectClaimedOutbox: "select id, event_name as \"eventName\", event_version as \"eventVersion\", payload_json as \"payloadJson\", payload_hash as \"payloadHash\", status, attempt_count as \"attemptCount\", max_attempts as \"maxAttempts\", tenant_id as \"tenantId\" from sloppy_webhook_outbox where id = $1 and locked_by = $2 and status = 'delivering'",
    subscriptionsForEvent: "select id, tenant_id as \"tenantId\", event_name as \"eventName\", endpoint_url as \"endpointUrl\", secret_ref as \"secretRef\", enabled, headers_json as \"headersJson\" from sloppy_webhook_subscriptions where event_name = $1 and enabled = true",
    insertAttempt: "insert into sloppy_webhook_delivery_attempts (id, outbox_id, subscription_id, delivery_id, attempt_number, status, status_code, error_code, error_message_redacted, request_headers_redacted_json, response_body_preview, duration_ms, attempted_at) values ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13)",
    deliveredSubscriptionsForOutbox: "select distinct subscription_id as \"subscriptionId\" from sloppy_webhook_delivery_attempts where outbox_id = $1 and status = 'delivered'",
    updateOutboxDelivered: "update sloppy_webhook_outbox set status = $1, attempt_count = $2, locked_by = null, locked_until = null, next_attempt_at = $3, updated_at = $4 where id = $5",
    insertInboundDedup: "insert into sloppy_webhook_inbound_dedup (id, provider, delivery_id, seen_at, expires_at) values ($1, $2, $3, $4, $5)",
});

SQL.sqlserver = Object.freeze({
    ...SQL.sqlite,
    placeholders: "question",
    createSubscriptions:
        "if object_id(N'dbo.sloppy_webhook_subscriptions', N'U') is null create table dbo.sloppy_webhook_subscriptions (" +
        "id nvarchar(96) not null primary key, tenant_id nvarchar(128) null, event_name nvarchar(256) not null, " +
        "endpoint_url nvarchar(2048) not null, secret_ref nvarchar(max) null, enabled bit not null, " +
        "headers_json nvarchar(max) not null, allow_private_networks bit not null default 0, " +
        "created_at nvarchar(64) not null, updated_at nvarchar(64) not null)",
    createOutbox:
        "if object_id(N'dbo.sloppy_webhook_outbox', N'U') is null create table dbo.sloppy_webhook_outbox (" +
        "id nvarchar(96) not null primary key, event_name nvarchar(256) not null, event_version int not null, " +
        "payload_json nvarchar(max) not null, payload_hash nvarchar(128) not null, occurred_at nvarchar(64) not null, " +
        "available_at nvarchar(64) not null, status nvarchar(32) not null, attempt_count int not null, " +
        "max_attempts int not null, next_attempt_at nvarchar(64) null, locked_by nvarchar(128) null, " +
        "locked_until nvarchar(64) null, idempotency_key nvarchar(256) null, metadata_json nvarchar(max) not null, " +
        "tenant_id nvarchar(128) null, created_at nvarchar(64) not null, updated_at nvarchar(64) not null)",
    createAttempts:
        "if object_id(N'dbo.sloppy_webhook_delivery_attempts', N'U') is null create table dbo.sloppy_webhook_delivery_attempts (" +
        "id nvarchar(96) not null primary key, outbox_id nvarchar(96) not null, subscription_id nvarchar(96) not null, " +
        "delivery_id nvarchar(96) not null, attempt_number int not null, status nvarchar(32) not null, " +
        "status_code int null, error_code nvarchar(128) null, error_message_redacted nvarchar(2048) null, " +
        "request_headers_redacted_json nvarchar(max) not null, response_body_preview nvarchar(4096) null, " +
        "duration_ms int not null, attempted_at nvarchar(64) not null)",
    createInboundDedup:
        "if object_id(N'dbo.sloppy_webhook_inbound_dedup', N'U') is null create table dbo.sloppy_webhook_inbound_dedup (" +
        "id nvarchar(96) not null primary key, provider nvarchar(128) not null, delivery_id nvarchar(128) not null, " +
        "seen_at nvarchar(64) not null, expires_at nvarchar(64) not null)",
    indexes: Object.freeze([
        "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_webhook_subscriptions_event_enabled' and object_id = object_id(N'dbo.sloppy_webhook_subscriptions')) create index idx_sloppy_webhook_subscriptions_event_enabled on dbo.sloppy_webhook_subscriptions (event_name, enabled)",
        "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_webhook_outbox_pending' and object_id = object_id(N'dbo.sloppy_webhook_outbox')) create index idx_sloppy_webhook_outbox_pending on dbo.sloppy_webhook_outbox (status, available_at, next_attempt_at, locked_until)",
        "if not exists (select 1 from sys.indexes where name = N'idx_sloppy_webhook_attempts_outbox' and object_id = object_id(N'dbo.sloppy_webhook_delivery_attempts')) create index idx_sloppy_webhook_attempts_outbox on dbo.sloppy_webhook_delivery_attempts (outbox_id, subscription_id, attempted_at)",
        "if not exists (select 1 from sys.indexes where name = N'ux_sloppy_webhook_inbound_dedup_provider_delivery' and object_id = object_id(N'dbo.sloppy_webhook_inbound_dedup')) create unique index ux_sloppy_webhook_inbound_dedup_provider_delivery on dbo.sloppy_webhook_inbound_dedup (provider, delivery_id)",
        "if not exists (select 1 from sys.indexes where name = N'ux_sloppy_webhook_outbox_idempotency' and object_id = object_id(N'dbo.sloppy_webhook_outbox')) create unique index ux_sloppy_webhook_outbox_idempotency on dbo.sloppy_webhook_outbox (idempotency_key) where idempotency_key is not null",
    ]),
});

Object.freeze(SQL);

async function ensureSchema(db, kind) {
    const sql = SQL[kind];
    await db.exec(sql.createSubscriptions, []);
    await db.exec(sql.createOutbox, []);
    await db.exec(sql.createAttempts, []);
    await db.exec(sql.createInboundDedup, []);
    for (const indexSql of sql.indexes) {
        await db.exec(indexSql, []);
    }
}

function nowIso(clock = undefined) {
    if (typeof clock?.now === "function") {
        const value = clock.now();
        return value instanceof Date ? value.toISOString() : new Date(value).toISOString();
    }
    return new Date().toISOString();
}

function addMs(iso, ms) {
    return new Date(new Date(iso).getTime() + ms).toISOString();
}

function randomId(prefix) {
    try {
        return `${prefix}_${Random.uuid()}`;
    } catch {
        fallbackIdCounter += 1;
        return `${prefix}_${Date.now().toString(36)}_${fallbackIdCounter.toString(36)}`;
    }
}

async function sha256Hex(value) {
    return Hash.sha256Hex(value);
}

function assertSerializable(payload) {
    let text;
    try {
        text = JSON.stringify(payload);
    } catch (error) {
        throw webhookError("SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED", "Webhook payload must be JSON-serializable.", { cause: error });
    }
    if (text === undefined) {
        throw webhookError("SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED", "Webhook payload must be JSON-serializable.");
    }
    if (Text.utf8.encode(text).byteLength > DEFAULT_MAX_PAYLOAD_BYTES) {
        throw webhookError("SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED", "Webhook payload exceeds the maximum supported size.");
    }
    return text;
}

function parseHeadersJson(value) {
    if (typeof value !== "string" || value.length === 0) {
        return Object.freeze({});
    }
    try {
        const parsed = JSON.parse(value);
        return isPlainObject(parsed) ? Object.freeze(parsed) : Object.freeze({});
    } catch {
        return Object.freeze({});
    }
}

function subscriptionFromRow(row) {
    if (row === null || row === undefined) {
        return null;
    }
    return Object.freeze({
        id: row.id,
        tenantId: row.tenantId ?? null,
        event: row.eventName,
        url: row.endpointUrl,
        enabled: row.enabled === true || row.enabled === 1,
        headers: parseHeadersJson(row.headersJson),
        allowPrivateNetworks: row.allowPrivateNetworks === true || row.allowPrivateNetworks === 1,
        createdAt: row.createdAt,
        updatedAt: row.updatedAt,
    });
}

function retryDelayMs(policy, attemptNumber, response = undefined) {
    const retryAfter = response?.headers?.get?.("retry-after");
    if (retryAfter !== undefined && retryAfter !== null) {
        const seconds = Number(retryAfter);
        if (Number.isFinite(seconds) && seconds >= 0) {
            return seconds * 1000;
        }
        const dateMs = Date.parse(retryAfter);
        if (Number.isFinite(dateMs)) {
            return Math.max(0, dateMs - Date.now());
        }
    }
    if (policy.kind === "fixed") {
        return policy.jitter ? randomDelayMs(policy.delayMs) : policy.delayMs;
    }
    const base = Math.min(policy.maxDelayMs, policy.initialDelayMs * (2 ** Math.max(0, attemptNumber - 1)));
    return policy.jitter ? randomDelayMs(base) : base;
}

function randomDelayMs(maxInclusive) {
    if (maxInclusive <= 0) {
        return 0;
    }
    const bytes = Random.bytes(4);
    const value = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
    return (value >>> 0) % (maxInclusive + 1);
}

function shouldRetryStatus(policy, status) {
    return policy.retryOnStatus.includes(status);
}

function redactHeaders(headers) {
    const redacted = {};
    for (const [name, value] of Object.entries(headers)) {
        redacted[name] = SENSITIVE_HEADER_PATTERN.test(name) ? "[REDACTED]" : value;
    }
    return Object.freeze(redacted);
}

function safeEndpointHost(url) {
    try {
        return new URL(url).host;
    } catch {
        return "invalid";
    }
}

async function sign(payload, options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.sign options must be a plain object.");
    }
    const timestamp = String(options.timestamp ?? Math.floor(Date.now() / 1000));
    const id = typeof options.id === "string" && options.id.length > 0 ? options.id : randomId("whdel");
    const eventName = validateEventName(options.event ?? options.eventName);
    const attempt = requirePositiveInteger(options.attempt ?? 1, "signature attempt");
    const body = typeof payload === "string" ? payload : assertSerializable(payload);
    const signingInput = webhookSigningInput(timestamp, Text.utf8.encode(body));
    const prepared = secretForHmac(options.secret, options.config, "signing secret");
    try {
        const digest = await Hmac.sha256(prepared.secret, signingInput);
        const signature = `v1=${Hex.encode(digest)}`;
        return Object.freeze({
            id,
            event: eventName,
            timestamp,
            attempt,
            signature,
            headers: Object.freeze({
                "Sloppy-Webhook-Id": id,
                "Sloppy-Webhook-Event": eventName,
                "Sloppy-Webhook-Timestamp": timestamp,
                "Sloppy-Webhook-Signature": signature,
                "Sloppy-Webhook-Attempt": String(attempt),
            }),
        });
    } finally {
        prepared.dispose();
    }
}

async function requestBodyBytes(ctxOrRequest) {
    const request = ctxOrRequest?.request ?? ctxOrRequest;
    if (typeof request?.bytes === "function") {
        return request.bytes();
    }
    if (typeof request?.text === "function") {
        return Text.utf8.encode(await request.text());
    }
    if (typeof ctxOrRequest?.bytes === "function") {
        return ctxOrRequest.bytes();
    }
    throw new TypeError("Sloppy Webhooks.verify requires a request or context with request bytes.");
}

function parseSignatureHeader(value) {
    if (typeof value !== "string" || value.length === 0) {
        return [];
    }
    return value.split(",")
        .map((part) => part.trim())
        .filter((part) => part.startsWith("v1="))
        .map((part) => part.slice(3).toLowerCase());
}

function constantTimeHexEquals(left, right) {
    if (left.length !== right.length) {
        return false;
    }
    let diff = 0;
    for (let index = 0; index < left.length; index += 1) {
        diff |= left.charCodeAt(index) ^ right.charCodeAt(index);
    }
    return diff === 0;
}

function webhookSigningInput(timestamp, bodyBytes) {
    const prefix = Text.utf8.encode(`${timestamp}.`);
    const bytes = new Uint8Array(prefix.byteLength + bodyBytes.byteLength);
    bytes.set(prefix, 0);
    bytes.set(bodyBytes, prefix.byteLength);
    return bytes;
}

async function verify(ctxOrRequest, options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.verify options must be a plain object.");
    }
    const request = ctxOrRequest?.request ?? ctxOrRequest;
    const headers = request?.headers;
    const timestamp = String(headerValue(headers, options.timestampHeader ?? "Sloppy-Webhook-Timestamp") ?? "");
    const signatureHeader = String(headerValue(headers, options.signatureHeader ?? "Sloppy-Webhook-Signature") ?? "");
    const deliveryId = String(headerValue(headers, options.idHeader ?? "Sloppy-Webhook-Id") ?? "");
    const eventName = String(headerValue(headers, options.eventHeader ?? "Sloppy-Webhook-Event") ?? "");
    if (timestamp.length === 0 || signatureHeader.length === 0) {
        throw webhookError("SLOPPY_E_WEBHOOK_SIGNATURE_INVALID", "Webhook signature headers are missing.");
    }
    const timestampMs = Number(timestamp) * 1000;
    if (!Number.isFinite(timestampMs)) {
        throw webhookError("SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE", "Webhook timestamp is invalid.");
    }
    const toleranceMs = requireNonNegativeInteger(options.toleranceMs, "timestamp toleranceMs", DEFAULT_TIMESTAMP_TOLERANCE_MS);
    const now = options.nowMs ?? Date.now();
    if (Math.abs(now - timestampMs) > toleranceMs) {
        throw webhookError("SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE", "Webhook timestamp is outside the accepted tolerance.");
    }
    const body = await requestBodyBytes(ctxOrRequest);
    if (body.byteLength > (options.maxBodyBytes ?? DEFAULT_MAX_PAYLOAD_BYTES)) {
        throw webhookError("SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED", "Webhook body exceeds the configured limit.");
    }
    const bodyText = Text.utf8.decode(body);
    const signingInput = webhookSigningInput(timestamp, body);
    const secrets = Array.isArray(options.secrets) ? options.secrets : [options.secret];
    const expected = [];
    for (const secret of secrets) {
        const prepared = secretForHmac(secret, options.config ?? ctxOrRequest?.config, "verification secret");
        try {
            const digest = await Hmac.sha256(prepared.secret, signingInput);
            expected.push(Hex.encode(digest).toLowerCase());
        } finally {
            prepared.dispose();
        }
    }
    const provided = parseSignatureHeader(signatureHeader);
    const ok = provided.some((candidate) => expected.some((digest) => constantTimeHexEquals(candidate, digest)));
    if (!ok) {
        throw webhookError("SLOPPY_E_WEBHOOK_SIGNATURE_INVALID", "Webhook signature is invalid.");
    }
    if (options.dedupe !== undefined && deliveryId.length > 0) {
        if (typeof options.dedupe.seen !== "function" || typeof options.dedupe.mark !== "function") {
            throw new TypeError("Sloppy Webhooks.verify dedupe must expose seen() and mark().");
        }
        if (await options.dedupe.seen(deliveryId, options.provider ?? "sloppy")) {
            throw webhookError("SLOPPY_E_WEBHOOK_REPLAY_DETECTED", "Webhook delivery was already processed.");
        }
    }
    let payload;
    try {
        payload = bodyText.length === 0 ? null : JSON.parse(bodyText);
    } catch {
        payload = bodyText;
    }
    if (options.event !== undefined) {
        options.event.validate(payload);
    }
    if (options.dedupe !== undefined && deliveryId.length > 0) {
        await options.dedupe.mark(deliveryId, options.provider ?? "sloppy", {
            timestamp,
            expiresAt: new Date(timestampMs + toleranceMs).toISOString(),
        });
    }
    return Object.freeze({
        id: deliveryId,
        event: eventName,
        timestamp,
        payload,
        body: bodyText,
        headers,
    });
}

function normalizeOutboxOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Webhooks.outbox options must be a plain object.");
    }
    if (options.signingSecret === undefined || (typeof options.signingSecret === "string" && options.signingSecret.length === 0)) {
        throw new TypeError("Sloppy Webhooks.outbox signingSecret is required and must not be empty.");
    }
    const provider = typeof options.provider === "string" && options.provider.length > 0
        ? options.provider
        : "main";
    const client = typeof options.delivery?.client === "string" && options.delivery.client.length > 0
        ? options.delivery.client
        : "webhooks";
    const retry = options.delivery?.retry === undefined
        ? retryExponential()
        : normalizeRetryPolicy(options.delivery.retry);
    return Object.freeze({
        token: stableToken(options.name),
        provider,
        providerKind: options.providerKind,
        signingSecret: options.signingSecret,
        delivery: Object.freeze({
            client,
            retry,
            leaseMs: requirePositiveInteger(options.delivery?.leaseMs, "delivery leaseMs", DEFAULT_LEASE_MS),
            batchSize: requirePositiveInteger(options.delivery?.batchSize, "delivery batchSize", DEFAULT_BATCH_SIZE),
            maxResponsePreviewBytes: requirePositiveInteger(
                options.delivery?.maxResponsePreviewBytes,
                "delivery maxResponsePreviewBytes",
                DEFAULT_MAX_RESPONSE_PREVIEW_BYTES,
            ),
        }),
        retention: Object.freeze({
            deadLetterAfterDays: requirePositiveInteger(options.retention?.deadLetterAfterDays, "retention deadLetterAfterDays", 30),
        }),
    });
}

function normalizeRetryPolicy(policy) {
    if (!isPlainObject(policy) || (policy.kind !== "fixed" && policy.kind !== "exponential")) {
        throw new TypeError("Sloppy Webhooks retry policy must come from Webhooks.retry.");
    }
    return policy;
}

function serviceProviderToken(provider) {
    return provider.includes(".") ? provider : `data.${provider}`;
}

function httpClientToken(client) {
    return client.includes(".") ? client : `http.${client}`;
}

function createWebhooksService(config, scope) {
    let initialized = false;
    let closed = false;
    let cachedDb;
    let cachedKind;

    function assertOpen() {
        if (closed) {
            throw webhookError("SLOPPY_E_WEBHOOK_CLOSED", "Webhook service is closed.");
        }
    }

    function db() {
        assertOpen();
        if (cachedDb === undefined) {
            cachedDb = scope.get(serviceProviderToken(config.provider));
            cachedKind = providerKind(cachedDb, config.providerKind);
        }
        return cachedDb;
    }

    async function init() {
        assertOpen();
        const connection = db();
        if (!initialized) {
            await ensureSchema(connection, cachedKind);
            initialized = true;
        }
        return service;
    }

    async function createSubscription(options) {
        assertOpen();
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy Webhooks subscription options must be a plain object.");
        }
        await init();
        const eventName = typeof options.event === "string" ? validateEventName(options.event) : validateEventName(options.event?.name);
        const url = validateEndpointUrl(options.url, options);
        const headers = validateHeaderMap(options.headers, "subscription");
        const id = options.id ?? randomId("whsub");
        const time = nowIso(options.clock);
        const secretRef = serializeSubscriptionSecret(options.secret);
        await db().exec(SQL[cachedKind].insertSubscription, [
            id,
            options.tenantId ?? null,
            eventName,
            url,
            secretRef,
            cachedKind === "postgres" ? options.enabled !== false : options.enabled === false ? 0 : 1,
            JSON.stringify(headers),
            cachedKind === "postgres" ? options.allowPrivateNetworks === true : options.allowPrivateNetworks === true ? 1 : 0,
            time,
            time,
        ]);
        return subscriptionFromRow({
            id,
            tenantId: options.tenantId ?? null,
            eventName,
            endpointUrl: url,
            enabled: options.enabled === false ? 0 : 1,
            headersJson: JSON.stringify(headers),
            allowPrivateNetworks: options.allowPrivateNetworks === true ? 1 : 0,
            createdAt: time,
            updatedAt: time,
        });
    }

    async function listSubscriptions(options = {}) {
        assertOpen();
        await init();
        const eventFilter = options.event === undefined ? null : validateEventName(typeof options.event === "string" ? options.event : options.event.name);
        const rows = await db().query(SQL[cachedKind].listSubscriptions, [eventFilter, eventFilter]);
        return Object.freeze(rows.map(subscriptionFromRow));
    }

    async function getSubscription(id) {
        assertOpen();
        await init();
        const row = await db().queryOne(SQL[cachedKind].getSubscription, [id]);
        return subscriptionFromRow(row);
    }

    async function updateSubscription(id, options) {
        assertOpen();
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy Webhooks subscription update options must be a plain object.");
        }
        await init();
        const current = await getSubscription(id);
        if (current === null) {
            return null;
        }
        const url = options.url === undefined ? current.url : validateEndpointUrl(options.url, options);
        const headers = options.headers === undefined ? current.headers : validateHeaderMap(options.headers, "subscription");
        const currentSecret = await db().queryOne(SQL[cachedKind].getSubscriptionSecret, [id]);
        const secretRef = options.secret === undefined ? currentSecret?.secretRef ?? null : serializeSubscriptionSecret(options.secret);
        const allowPrivateNetworks = options.allowPrivateNetworks === undefined
            ? current.allowPrivateNetworks
            : options.allowPrivateNetworks === true;
        const time = nowIso(options.clock);
        await db().exec(SQL[cachedKind].updateSubscription, [
            url,
            secretRef,
            JSON.stringify(headers),
            cachedKind === "postgres" ? allowPrivateNetworks : allowPrivateNetworks ? 1 : 0,
            time,
            id,
        ]);
        return getSubscription(id);
    }

    async function setEnabled(id, enabled) {
        assertOpen();
        await init();
        await db().exec(SQL[cachedKind].setSubscriptionEnabled, [cachedKind === "postgres" ? enabled : enabled ? 1 : 0, nowIso(), id]);
        return getSubscription(id);
    }

    async function deleteSubscription(id) {
        assertOpen();
        await init();
        await db().exec(SQL[cachedKind].deleteSubscription, [id]);
        return true;
    }

    async function publish(dbOrTx, eventDescriptor, payload, options = {}) {
        assertOpen();
        if (dbOrTx === undefined || dbOrTx === null || typeof dbOrTx.exec !== "function") {
            throw webhookError("SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE", "Webhook publish requires a data provider or transaction.");
        }
        await init();
        if (eventDescriptor?.__sloppyWebhookEvent !== true) {
            throw new TypeError("Sloppy Webhooks.publish event must come from Webhooks.event.");
        }
        const value = eventDescriptor.validate(payload);
        const payloadJson = assertSerializable(value);
        const time = nowIso(options.clock);
        const availableAt = options.delayMs === undefined ? time : addMs(time, requireNonNegativeInteger(options.delayMs, "publish delayMs"));
        const idempotencyKey = options.idempotencyKey ?? null;
        if (idempotencyKey !== null) {
            if (typeof idempotencyKey !== "string" || idempotencyKey.length === 0) {
                throw new TypeError("Sloppy Webhooks publish idempotencyKey must be a non-empty string.");
            }
            const existing = await dbOrTx.queryOne?.(SQL[providerKind(db(), config.providerKind)].selectOutboxByIdempotencyKey, [idempotencyKey]);
            if (existing !== undefined && existing !== null) {
                return Object.freeze({
                    id: existing.id,
                    event: existing.eventName,
                    version: existing.eventVersion,
                    payloadHash: existing.payloadHash,
                    idempotent: true,
                });
            }
        }
        const id = options.id ?? randomId("whevt");
        const retry = options.retry === undefined ? config.delivery.retry : normalizeRetryPolicy(options.retry);
        const payloadHash = await sha256Hex(payloadJson);
        await dbOrTx.exec(SQL[providerKind(db(), config.providerKind)].insertOutbox, [
            id,
            eventDescriptor.name,
            eventDescriptor.version,
            payloadJson,
            payloadHash,
            time,
            availableAt,
            "pending",
            0,
            retry.maxAttempts,
            null,
            null,
            null,
            idempotencyKey,
            JSON.stringify(options.metadata ?? {}),
            options.tenantId ?? null,
            time,
            time,
        ]);
        return Object.freeze({ id, event: eventDescriptor.name, version: eventDescriptor.version, payloadHash, idempotent: false });
    }

    async function recordAttempt(row, subscription, attemptNumber, result) {
        const time = nowIso();
        await db().exec(SQL[cachedKind].insertAttempt, [
            randomId("whatt"),
            row.id,
            subscription.id,
            result.deliveryId,
            attemptNumber,
            result.status,
            result.statusCode ?? null,
            result.errorCode ?? null,
            result.errorMessage ?? null,
            JSON.stringify(redactHeaders(result.requestHeaders ?? {})),
            result.responsePreview ?? null,
            result.durationMs ?? 0,
            time,
        ]);
    }

    async function sendDelivery(row, subscription, attemptNumber, options) {
        const endpoint = new URL(subscription.endpointUrl);
        const client = options.clientForOrigin?.(endpoint.origin)
            ?? options.clientFactory?.get?.(endpoint.origin)
            ?? options.client
            ?? scope.get(httpClientToken(config.delivery.client));
        const body = row.payloadJson;
        const deliveryId = randomId("whdel");
        const signature = await sign(body, {
            secret: subscriptionSecretFromStored(subscription.secretRef, options.signingSecret ?? config.signingSecret),
            config: scope.config,
            id: deliveryId,
            event: row.eventName,
            attempt: attemptNumber,
        });
        const headers = {
            ...subscription.headers,
            ...signature.headers,
            "Content-Type": "application/json; charset=utf-8",
            "User-Agent": "Sloppy-Webhooks/1",
        };
        const started = Date.now();
        try {
            const response = await client.post(endpoint.href, {
                headers,
                retry: { kind: "none", maxAttempts: 1 },
            }).textBody(body).send();
            const text = await response.text();
            const preview = text.slice(0, options.maxResponsePreviewBytes ?? config.delivery.maxResponsePreviewBytes);
            const delivered = response.status >= 200 && response.status < 300;
            const retryable = !delivered && shouldRetryStatus(config.delivery.retry, response.status);
            return Object.freeze({
                deliveryId,
                status: delivered ? "delivered" : "failed",
                statusCode: response.status,
                requestHeaders: headers,
                responsePreview: preview,
                retryAfterMs: retryable ? retryDelayMs(config.delivery.retry, attemptNumber, response) : undefined,
                retryable,
                durationMs: Math.max(0, Date.now() - started),
            });
        } catch (error) {
            return Object.freeze({
                deliveryId,
                status: "failed",
                errorCode: error?.code ?? "SLOPPY_E_WEBHOOK_DELIVERY_FAILED",
                errorMessage: String(error?.message ?? "delivery failed").replace(String(config.signingSecret ?? ""), "[redacted]"),
                requestHeaders: headers,
                retryable: true,
                durationMs: Math.max(0, Date.now() - started),
            });
        }
    }

    async function deliverPending(options = {}) {
        assertOpen();
        await init();
        const batchSize = requirePositiveInteger(options.batchSize, "deliverPending batchSize", config.delivery.batchSize);
        const workerId = options.workerId ?? randomId("whworker");
        const time = nowIso(options.clock);
        const lockedUntil = addMs(time, options.leaseMs ?? config.delivery.leaseMs);
        const rows = await db().query(SQL[cachedKind].selectPendingOutbox, [time, time, time, batchSize]);
        const summary = {
            claimed: 0,
            delivered: 0,
            failed: 0,
            deadLetter: 0,
            skipped: 0,
        };
        for (const row of rows) {
            await db().exec(SQL[cachedKind].claimOutbox, [workerId, lockedUntil, time, row.id, time]);
            const claimedRow = await db().queryOne(SQL[cachedKind].selectClaimedOutbox, [row.id, workerId]);
            if (claimedRow === null || claimedRow === undefined) {
                summary.skipped += 1;
                continue;
            }
            summary.claimed += 1;
            const subscriptions = await db().query(SQL[cachedKind].subscriptionsForEvent, [claimedRow.eventName]);
            const enabled = subscriptions.filter((subscription) => subscription.enabled === true || subscription.enabled === 1);
            if (enabled.length === 0) {
                await db().exec(SQL[cachedKind].updateOutboxDelivered, ["delivered", claimedRow.attemptCount, null, nowIso(), claimedRow.id]);
                summary.skipped += 1;
                continue;
            }
            let rowDelivered = true;
            let rowDeadLetter = false;
            let retryAfterMs = 0;
            const nextAttempt = claimedRow.attemptCount + 1;
            const deliveredSubscriptionRows = await db().query(SQL[cachedKind].deliveredSubscriptionsForOutbox, [claimedRow.id]);
            const deliveredSubscriptionIds = new Set(deliveredSubscriptionRows.map((entry) => entry.subscriptionId));
            for (const subscriptionRow of enabled) {
                if (deliveredSubscriptionIds.has(subscriptionRow.id)) {
                    summary.skipped += 1;
                    continue;
                }
                const subscription = Object.freeze({
                    id: subscriptionRow.id,
                    endpointUrl: subscriptionRow.endpointUrl,
                    secretRef: subscriptionRow.secretRef,
                    headers: parseHeadersJson(subscriptionRow.headersJson),
                });
                const result = await sendDelivery(claimedRow, subscription, nextAttempt, options);
                await recordAttempt(claimedRow, subscription, nextAttempt, result);
                if (result.status === "delivered") {
                    summary.delivered += 1;
                    continue;
                }
                rowDelivered = false;
                if (result.retryable === false || nextAttempt >= claimedRow.maxAttempts) {
                    rowDeadLetter = true;
                    summary.deadLetter += 1;
                } else {
                    retryAfterMs = Math.max(retryAfterMs, result.retryAfterMs ?? retryDelayMs(config.delivery.retry, nextAttempt));
                    summary.failed += 1;
                }
            }
            const status = rowDelivered ? "delivered" : rowDeadLetter ? "dead_letter" : "failed";
            const nextAttemptAt = status === "failed" ? addMs(nowIso(), retryAfterMs) : null;
            await db().exec(SQL[cachedKind].updateOutboxDelivered, [status, nextAttempt, nextAttemptAt, nowIso(), claimedRow.id]);
        }
        return Object.freeze(summary);
    }

    function status() {
        assertOpen();
        return Object.freeze({
            provider: config.provider,
            providerKind: cachedKind ?? config.providerKind ?? "unknown",
            client: config.delivery.client,
            initialized,
            closed,
        });
    }

    async function health() {
        try {
            await init();
            return Object.freeze({ status: "healthy", provider: config.provider });
        } catch (error) {
            return Object.freeze({
                status: initialized ? "degraded" : "unhealthy",
                provider: config.provider,
                errorCode: error?.code ?? "SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE",
            });
        }
    }

    const service = Object.freeze({
        init,
        subscriptions: Object.freeze({
            create: createSubscription,
            list: listSubscriptions,
            get: getSubscription,
            update: updateSubscription,
            enable: (id) => setEnabled(id, true),
            disable: (id) => setEnabled(id, false),
            delete: deleteSubscription,
        }),
        publish,
        deliverPending,
        jobs: Object.freeze({
            deliverPending(jobOptions = {}) {
                return Object.freeze({
                    run(ctx = {}) {
                        return deliverPending({ ...jobOptions, ...ctx });
                    },
                    handler(ctx = {}) {
                        return deliverPending({ ...jobOptions, ...ctx });
                    },
                });
            },
        }),
        status,
        health,
        metrics() {
            return Object.freeze({
                "webhooks.outbox.initialized": initialized ? 1 : 0,
            });
        },
        async dispose() {
            closed = true;
        },
    });
    return service;
}

function outbox(options) {
    const config = normalizeOutboxOptions(options);
    const descriptor = {
        __sloppyWebhooksOutboxRegistration: true,
        token: config.token,
        provider: config.provider,
        delivery: config.delivery,
        createService(scope) {
            return createWebhooksService(config, scope);
        },
        __sloppyPlanMetadata() {
            return Object.freeze({
                enabled: true,
                provider: config.provider,
                client: config.delivery.client,
                retry: config.delivery.retry.kind,
                signing: config.signingSecret === undefined ? "missing" : "configured",
            });
        },
    };
    return Object.freeze(descriptor);
}

function jobsDeliverPending(options = {}) {
    return Object.freeze({
        run(ctx = {}) {
            const service = ctx.webhooks ?? ctx.services?.get?.(DEFAULT_WEBHOOKS_TOKEN);
            if (service === undefined) {
                throw webhookError("SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE", "Webhook job requires a registered webhook service.");
            }
            return service.deliverPending(options);
        },
        handler(ctx = {}) {
            return this.run(ctx);
        },
    });
}

class TestWebhookReceiver {
    constructor() {
        this.expected = [];
        this.received = [];
        this.status = 200;
        this.body = { ok: true };
    }

    expect(eventName) {
        this.expected.push(validateEventName(eventName));
        return this;
    }

    reply(status, body = undefined) {
        this.status = status;
        this.body = body;
        return this;
    }

    async handler(ctx) {
        const verified = await verify(ctx, {
            secret: this.secret ?? "test-secret",
            toleranceMs: DEFAULT_TIMESTAMP_TOLERANCE_MS,
        });
        this.received.push(verified);
        return {
            __sloppyResult: true,
            kind: "json",
            status: this.status,
            body: this.body,
            contentType: "application/json; charset=utf-8",
        };
    }

    assertDelivered(eventName) {
        validateEventName(eventName);
        if (!this.received.some((entry) => entry.event === eventName)) {
            throw webhookError("SLOPPY_E_WEBHOOK_DELIVERY_FAILED", `Expected webhook event '${eventName}' to be delivered.`);
        }
        return this;
    }

    assertNoUnexpectedDeliveries() {
        const allowed = new Set(this.expected);
        const unexpected = this.received.filter((entry) => !allowed.has(entry.event));
        if (unexpected.length !== 0) {
            throw webhookError("SLOPPY_E_WEBHOOK_DELIVERY_FAILED", "Unexpected webhook deliveries were observed.");
        }
        return this;
    }
}

const TestWebhooks = Object.freeze({
    receiver(options = {}) {
        const receiver = new TestWebhookReceiver();
        receiver.secret = options.secret ?? "test-secret";
        return receiver;
    },
});

const Webhooks = Object.freeze({
    event,
    outbox,
    token: stableToken,
    sign,
    verify,
    receiver: (options) => TestWebhooks.receiver(options),
    jobs: Object.freeze({
        deliverPending: jobsDeliverPending,
    }),
    retry: Object.freeze({
        fixed: retryFixed,
        exponential: retryExponential,
    }),
    sql(provider = "sqlite") {
        if (!Object.prototype.hasOwnProperty.call(SQL, provider)) {
            throw new TypeError("Sloppy Webhooks.sql provider must be sqlite, postgres, or sqlserver.");
        }
        return SQL[provider];
    },
    isTerminalStatus(status) {
        return TERMINAL_STATUSES.has(status);
    },
});

export { SloppyWebhookError, TestWebhooks, Webhooks };
