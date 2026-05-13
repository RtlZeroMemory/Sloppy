import { serializeJson } from "./results.js";
import { isSchema } from "./schema.js";
import { Text } from "./codec.js";

const CACHE_MARKER = Symbol("SloppyCache");
const DEFAULT_MEMORY_MAX_ENTRIES = 1024;
const DEFAULT_KEY_MAX_LENGTH = 512;
const DEFAULT_TAG_MAX_LENGTH = 128;
const DEFAULT_VALUE_MAX_BYTES = 1024 * 1024;
const DEFAULT_DISTRIBUTED_TABLE = "sloppy_cache_entries";
const IDENTIFIER_PATTERN = /^[A-Za-z_][0-9A-Za-z_]{0,62}$/u;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function nowMs(clock = undefined) {
    if (clock !== undefined && typeof clock.monotonicNowMs === "function") {
        return clock.monotonicNowMs();
    }
    return Date.now();
}

function nowDate(clock = undefined) {
    if (clock !== undefined && typeof clock.now === "function") {
        return clock.now();
    }
    return new Date();
}

function cloneJsonValue(value, subject = "cache value") {
    let text;
    try {
        text = serializeJson(value);
    } catch (error) {
        throw new TypeError(`Sloppy ${subject} must be JSON-serializable.`, { cause: error });
    }
    if (text === undefined) {
        throw new TypeError(`Sloppy ${subject} must be JSON-serializable.`);
    }
    return JSON.parse(text);
}

function jsonBytes(text) {
    return Text.utf8.encode(text).byteLength;
}

function stableHash(value) {
    const text = String(value);
    let hash = 0x811c9dc5;
    for (let index = 0; index < text.length; index += 1) {
        hash ^= text.charCodeAt(index) & 0xff;
        hash = Math.imul(hash, 0x01000193) >>> 0;
    }
    return `fnv1a32:${hash.toString(16).padStart(8, "0")}`;
}

function normalizeName(name, subject = "cache name") {
    if (typeof name !== "string" || name.trim().length === 0 || name.length > 128 || /[\x00-\x1F\x7F]/u.test(name)) {
        throw new TypeError(`Sloppy ${subject} must be a non-empty stable string at most 128 characters.`);
    }
    return name;
}

function normalizeTokenName(name, subject = "cache token name") {
    if (typeof name !== "string") {
        throw new TypeError(`Sloppy ${subject} must be a non-empty stable token name.`);
    }
    const normalized = name.trim().toLowerCase().replace(/\s+/gu, "-");
    if (normalized.length === 0 || normalized.length > 128 || !/^[a-z0-9][a-z0-9._-]*$/u.test(normalized)) {
        throw new TypeError(`Sloppy ${subject} must start with a letter or digit and contain only letters, digits, '.', '_', or '-'.`);
    }
    return normalized;
}

function normalizeKey(key, options = {}) {
    const maxLength = options.maxKeyLength ?? DEFAULT_KEY_MAX_LENGTH;
    if (!Number.isInteger(maxLength) || maxLength < 1 || maxLength > 4096) {
        throw new TypeError("Sloppy cache maxKeyLength must be an integer from 1 to 4096.");
    }
    if (typeof key !== "string" || key.length === 0 || key.length > maxLength || /[\x00-\x1F\x7F]/u.test(key)) {
        throw new TypeError(`Sloppy cache key must be a non-empty string at most ${maxLength} characters without control characters.`);
    }
    return key;
}

function normalizeTag(tag, options = {}) {
    const maxLength = options.maxTagLength ?? DEFAULT_TAG_MAX_LENGTH;
    if (!Number.isInteger(maxLength) || maxLength < 1 || maxLength > 1024) {
        throw new TypeError("Sloppy cache maxTagLength must be an integer from 1 to 1024.");
    }
    if (typeof tag !== "string" || tag.length === 0 || tag.length > maxLength || /[\x00-\x1F\x7F]/u.test(tag)) {
        throw new TypeError(`Sloppy cache tag must be a non-empty string at most ${maxLength} characters without control characters.`);
    }
    return tag;
}

function normalizeTags(tags, options = {}) {
    if (tags === undefined) {
        return Object.freeze([]);
    }
    if (!Array.isArray(tags)) {
        throw new TypeError("Sloppy cache tags must be an array.");
    }
    return Object.freeze([...new Set(tags.map((tag) => normalizeTag(tag, options)))]);
}

function normalizeTtlMs(value, subject = "ttlMs") {
    if (value === undefined) {
        return undefined;
    }
    if (!Number.isInteger(value) || value < 0 || value > 0x7fffffff) {
        throw new TypeError(`Sloppy cache ${subject} must be an integer from 0 to 2147483647.`);
    }
    return value;
}

function normalizeAbsoluteExpiration(value) {
    if (value === undefined) {
        return undefined;
    }
    const date = value instanceof Date ? value : new Date(value);
    const time = date.getTime();
    if (!Number.isFinite(time)) {
        throw new TypeError("Sloppy cache absoluteExpiration must be a valid Date or ISO timestamp.");
    }
    return time;
}

function normalizeSchema(value) {
    if (value === undefined) {
        return undefined;
    }
    if (!isSchema(value)) {
        throw new TypeError("Sloppy cache schema must be a Schema value.");
    }
    return value;
}

function normalizeEntryOptions(options = {}) {
    if (options === undefined) {
        return Object.freeze({});
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy cache entry options must be a plain object.");
    }
    return Object.freeze({
        ttlMs: normalizeTtlMs(options.ttlMs),
        absoluteExpiration: normalizeAbsoluteExpiration(options.absoluteExpiration),
        slidingExpirationMs: normalizeTtlMs(options.slidingExpirationMs, "slidingExpirationMs"),
        tags: normalizeTags(options.tags, options),
        schema: normalizeSchema(options.schema),
        staleWhileRevalidateMs: normalizeTtlMs(options.staleWhileRevalidateMs, "staleWhileRevalidateMs"),
        stampedeProtection: options.stampedeProtection !== false,
        namespace: options.namespace === undefined ? undefined : normalizeName(options.namespace, "cache namespace"),
        cacheNull: options.cacheNull !== false,
        signal: options.signal,
    });
}

function expiresAtFromOptions(options, clock = undefined) {
    const base = nowMs(clock);
    let expiresAt = options.ttlMs === undefined ? undefined : base + options.ttlMs;
    if (options.absoluteExpiration !== undefined) {
        expiresAt = expiresAt === undefined ? options.absoluteExpiration : Math.min(expiresAt, options.absoluteExpiration);
    }
    return expiresAt;
}

function isExpired(entry, clock = undefined) {
    return entry.expiresAt !== undefined && nowMs(clock) >= entry.expiresAt;
}

function validateValueWithSchema(value, schema, key) {
    if (schema === undefined) {
        return value;
    }
    const result = schema.validate(value);
    if (result.ok) {
        return result.value;
    }
    throw new SloppyCacheError("SLOPPY_E_CACHE_SCHEMA_MISMATCH", `Sloppy cache value for '${stableHash(key)}' failed schema validation.`, {
        keyHash: stableHash(key),
        issues: result.issues,
    });
}

function cacheToken(name = "default") {
    return `cache.${normalizeTokenName(name, "cache token name")}`;
}

function cacheMetricName(operation) {
    switch (operation) {
    case "gets":
        return "cache.gets.total";
    case "hits":
        return "cache.hits.total";
    case "misses":
        return "cache.misses.total";
    case "sets":
        return "cache.sets.total";
    case "removes":
        return "cache.removes.total";
    case "evictions":
        return "cache.evictions.total";
    case "expired":
        return "cache.expired.total";
    case "tagInvalidations":
        return "cache.tag_invalidations.total";
    case "factoryRuns":
        return "cache.get_or_create.factory.total";
    case "stampedeWaiters":
        return "cache.stampede.waiters.total";
    case "staleHits":
        return "cache.stale_hits.total";
    default:
        return undefined;
    }
}

function recordCacheMetric(cache, operation) {
    const name = cacheMetricName(operation);
    if (name === undefined || cache.metrics === undefined || cache.metrics === null) {
        return;
    }
    const labels = Object.freeze({
        cache: cache.name,
        backend: cache.kind,
        operation,
    });
    try {
        if (typeof cache.metrics.increment === "function") {
            cache.metrics.increment(name, labels);
            return;
        }
        cache.metrics.counter?.(name, {
            description: "Cache operations by cache name, backend, and operation.",
        })?.inc(labels);
    } catch {
        // Metrics must not change cache behavior.
    }
}

class SloppyCacheError extends Error {
    constructor(code, message, details = undefined) {
        super(message);
        this.name = "SloppyCacheError";
        this.code = code;
        this.details = details === undefined ? undefined : Object.freeze({ ...details });
        this.__sloppyCacheError = true;
    }
}

class BaseCache {
    constructor(name, kind, options = {}) {
        this.name = normalizeName(name ?? "default");
        this.kind = kind;
        this.namespace = normalizeName(options.namespace ?? this.name, "cache namespace");
        this.maxKeyLength = options.maxKeyLength ?? DEFAULT_KEY_MAX_LENGTH;
        this.maxTagLength = options.maxTagLength ?? DEFAULT_TAG_MAX_LENGTH;
        this.clock = options.clock;
        this.metrics = options.metrics;
        this.disposed = false;
        this.inflight = new Map();
        this.counters = {
            gets: 0,
            hits: 0,
            misses: 0,
            sets: 0,
            removes: 0,
            evictions: 0,
            expired: 0,
            tagInvalidations: 0,
            factoryRuns: 0,
            stampedeWaiters: 0,
            staleHits: 0,
        };
        Object.defineProperty(this, CACHE_MARKER, { value: true });
        Object.defineProperty(this, "__sloppyCache", { value: true, enumerable: true });
    }

    _assertOpen(operation) {
        if (this.disposed) {
            throw new SloppyCacheError("SLOPPY_E_CACHE_DISPOSED", `Sloppy cache '${this.name}' is disposed.`, { operation });
        }
    }

    _key(key) {
        return normalizeKey(key, this);
    }

    _entryOptions(options = {}) {
        return normalizeEntryOptions(options);
    }

    _record(operation) {
        if (Object.prototype.hasOwnProperty.call(this.counters, operation)) {
            this.counters[operation] += 1;
        }
        recordCacheMetric(this, operation);
    }

    __setMetricsRegistry(metrics) {
        this.metrics = metrics;
        return this;
    }

    async getOrCreate(key, options, factory) {
        this._assertOpen("getOrCreate");
        const normalizedKey = this._key(key);
        const normalizedOptions = this._entryOptions(options);
        if (typeof factory !== "function") {
            throw new TypeError("Sloppy cache getOrCreate factory must be a function.");
        }
        const existing = await this.get(normalizedKey, normalizedOptions);
        if (existing !== undefined) {
            return existing;
        }
        if (normalizedOptions.stampedeProtection === false) {
            return this._runFactory(normalizedKey, normalizedOptions, factory);
        }
        const inflightKey = `${this.namespace}\0${normalizedKey}`;
        const current = this.inflight.get(inflightKey);
        if (current !== undefined) {
            this._record("stampedeWaiters");
            return current;
        }
        const created = this._runFactory(normalizedKey, normalizedOptions, factory)
            .finally(() => {
                this.inflight.delete(inflightKey);
            });
        this.inflight.set(inflightKey, created);
        return created;
    }

    async _runFactory(key, options, factory) {
        this._record("factoryRuns");
        if (options.signal?.aborted === true) {
            options.signal.throwIfAborted?.();
            throw new SloppyCacheError("SLOPPY_E_CACHE_CANCELLED", "Sloppy cache factory was cancelled before it started.", {
                keyHash: stableHash(key),
            });
        }
        const value = await factory(options.signal);
        if (value === null && options.cacheNull === false) {
            return value;
        }
        const validated = validateValueWithSchema(value, options.schema, key);
        await this.set(key, validated, options);
        return validated;
    }

    delete(key) {
        return this.remove(key);
    }

    invalidate(key) {
        return this.remove(key);
    }

    stats() {
        return Object.freeze({
            name: this.name,
            kind: this.kind,
            namespace: this.namespace,
            disposed: this.disposed,
            ...this.counters,
        });
    }

    dispose() {
        this.disposed = true;
        this.inflight.clear();
    }
}

class MemoryCache extends BaseCache {
    constructor(name, options = {}) {
        super(name, "memory", options);
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy memory cache options must be a plain object.");
        }
        this.maxEntries = options.maxEntries ?? DEFAULT_MEMORY_MAX_ENTRIES;
        if (!Number.isInteger(this.maxEntries) || this.maxEntries < 1 || this.maxEntries > 1_000_000) {
            throw new TypeError("Sloppy memory cache maxEntries must be an integer from 1 to 1000000.");
        }
        this.defaultTtlMs = normalizeTtlMs(options.ttlMs);
        this.entries = new Map();
        Object.seal(this);
    }

    _entryOptions(options = {}) {
        const normalized = normalizeEntryOptions(options);
        return Object.freeze({
            ...normalized,
            ttlMs: normalized.ttlMs ?? this.defaultTtlMs,
        });
    }

    _entry(key, value, options) {
        const json = serializeJson(value);
        if (json === undefined) {
            throw new TypeError("Sloppy memory cache value must be JSON-serializable.");
        }
        if (jsonBytes(json) > (options.maxValueBytes ?? DEFAULT_VALUE_MAX_BYTES)) {
            throw new SloppyCacheError("SLOPPY_E_CACHE_VALUE_TOO_LARGE", "Sloppy memory cache value exceeds maxValueBytes.", {
                keyHash: stableHash(key),
            });
        }
        const timestamp = nowMs(this.clock);
        return {
            key,
            valueJson: json,
            tags: options.tags,
            createdAt: timestamp,
            updatedAt: timestamp,
            lastAccessedAt: timestamp,
            expiresAt: expiresAtFromOptions(options, this.clock),
            slidingExpirationMs: options.slidingExpirationMs,
        };
    }

    _deleteExpired() {
        for (const [key, entry] of this.entries) {
            if (isExpired(entry, this.clock)) {
                this.entries.delete(key);
                this._record("expired");
            }
        }
    }

    _evictIfNeeded() {
        if (this.entries.size <= this.maxEntries) {
            return;
        }
        this._deleteExpired();
        while (this.entries.size > this.maxEntries) {
            let oldestKey;
            let oldestAccess = Infinity;
            for (const [key, entry] of this.entries) {
                if (entry.lastAccessedAt < oldestAccess) {
                    oldestAccess = entry.lastAccessedAt;
                    oldestKey = key;
                }
            }
            if (oldestKey === undefined) {
                return;
            }
            this.entries.delete(oldestKey);
            this._record("evictions");
        }
    }

    async get(key, schemaOrOptions = undefined) {
        this._assertOpen("get");
        const normalizedKey = this._key(key);
        const options = isSchema(schemaOrOptions)
            ? Object.freeze({ schema: schemaOrOptions })
            : this._entryOptions(schemaOrOptions ?? {});
        this._record("gets");
        const entry = this.entries.get(normalizedKey);
        if (entry === undefined) {
            this._record("misses");
            return undefined;
        }
        if (isExpired(entry, this.clock)) {
            this.entries.delete(normalizedKey);
            this._record("expired");
            this._record("misses");
            return undefined;
        }
        entry.lastAccessedAt = nowMs(this.clock);
        if (entry.slidingExpirationMs !== undefined) {
            entry.expiresAt = entry.lastAccessedAt + entry.slidingExpirationMs;
        }
        this._record("hits");
        return validateValueWithSchema(JSON.parse(entry.valueJson), options.schema, normalizedKey);
    }

    async has(key) {
        return (await this.get(key)) !== undefined;
    }

    async set(key, value, options = {}) {
        this._assertOpen("set");
        const normalizedKey = this._key(key);
        const normalizedOptions = this._entryOptions(options);
        const validated = validateValueWithSchema(value, normalizedOptions.schema, normalizedKey);
        this.entries.set(normalizedKey, this._entry(normalizedKey, validated, normalizedOptions));
        this._record("sets");
        this._evictIfNeeded();
        return this;
    }

    async remove(key) {
        this._assertOpen("remove");
        const removed = this.entries.delete(this._key(key));
        if (removed) {
            this._record("removes");
        }
        return removed;
    }

    async invalidateTag(tag) {
        return this.invalidateTags([tag]);
    }

    async invalidateTags(tags) {
        this._assertOpen("invalidateTags");
        const normalized = normalizeTags(tags, this);
        let removed = 0;
        for (const [key, entry] of this.entries) {
            if (entry.tags.some((tag) => normalized.includes(tag))) {
                this.entries.delete(key);
                removed += 1;
            }
        }
        this._record("tagInvalidations");
        return removed;
    }

    async clear(options = {}) {
        this._assertOpen("clear");
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy cache clear options must be a plain object.");
        }
        const count = this.entries.size;
        this.entries.clear();
        return count;
    }

    async cleanup() {
        this._assertOpen("cleanup");
        const before = this.entries.size;
        this._deleteExpired();
        return before - this.entries.size;
    }

    stats() {
        return Object.freeze({
            ...super.stats(),
            entries: this.entries.size,
            maxEntries: this.maxEntries,
        });
    }

    dispose() {
        super.dispose();
        this.entries.clear();
    }
}

function providerKind(db, operation) {
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
    const expected = operation === "sqlServer" ? "sqlserver" : operation;
    throw new TypeError(`Sloppy Cache.${operation} requires a real ${expected} connection from sloppy/data.`);
}

function placeholder(kind, index) {
    return kind === "postgres" ? `$${index}` : "?";
}

function validateTableName(value) {
    const table = value ?? DEFAULT_DISTRIBUTED_TABLE;
    if (typeof table !== "string" || !IDENTIFIER_PATTERN.test(table)) {
        throw new TypeError("Sloppy distributed cache table must be a simple SQL identifier.");
    }
    return table;
}

function distributedSql(kind, table) {
    const columns = "namespace, cache_key, value_json, created_at, updated_at, expires_at, sliding_expiration_ms, tags_json";
    const values = Array.from({ length: 8 }, (_, index) => placeholder(kind, index + 1)).join(", ");
    const updateSet = [
        "value_json = excluded.value_json",
        "updated_at = excluded.updated_at",
        "expires_at = excluded.expires_at",
        "sliding_expiration_ms = excluded.sliding_expiration_ms",
        "tags_json = excluded.tags_json",
    ].join(", ");
    if (kind === "postgres") {
        return Object.freeze({
            ensure: `create table if not exists ${table} (` +
                "namespace text not null, cache_key text not null, value_json text not null, " +
                "created_at text not null, updated_at text not null, expires_at text null, " +
                "sliding_expiration_ms integer null, tags_json text not null, primary key (namespace, cache_key))",
            get: `select value_json, expires_at, sliding_expiration_ms, tags_json from ${table} where namespace = $1 and cache_key = $2`,
            selectNamespace: `select cache_key, tags_json from ${table} where namespace = $1`,
            deleteOne: `delete from ${table} where namespace = $1 and cache_key = $2`,
            clearNamespace: `delete from ${table} where namespace = $1`,
            clearAll: `delete from ${table}`,
            cleanup: `delete from ${table} where expires_at is not null and expires_at <= $1`,
            set: `insert into ${table} (${columns}) values (${values}) on conflict (namespace, cache_key) do update set ${updateSet}`,
        });
    }
    if (kind === "sqlserver") {
        return Object.freeze({
            ensure: `if object_id(N'dbo.${table}', N'U') is null begin create table dbo.${table} (` +
                "namespace nvarchar(128) not null, cache_key nvarchar(256) not null, value_json nvarchar(max) not null, " +
                "created_at nvarchar(64) not null, updated_at nvarchar(64) not null, expires_at nvarchar(64) null, " +
                "sliding_expiration_ms int null, tags_json nvarchar(max) not null, constraint " +
                `pk_${table} primary key (namespace, cache_key)) end`,
            get: `select value_json, expires_at, sliding_expiration_ms, tags_json from dbo.${table} where namespace = ? and cache_key = ?`,
            selectNamespace: `select cache_key, tags_json from dbo.${table} where namespace = ?`,
            deleteOne: `delete from dbo.${table} where namespace = ? and cache_key = ?`,
            clearNamespace: `delete from dbo.${table} where namespace = ?`,
            clearAll: `delete from dbo.${table}`,
            cleanup: `delete from dbo.${table} where expires_at is not null and expires_at <= ?`,
            update: `update dbo.${table} set value_json = ?, updated_at = ?, expires_at = ?, sliding_expiration_ms = ?, tags_json = ? where namespace = ? and cache_key = ?`,
            insert: `insert into dbo.${table} (${columns}) values (${values})`,
        });
    }
    return Object.freeze({
        ensure: `create table if not exists ${table} (` +
            "namespace text not null, cache_key text not null, value_json text not null, " +
            "created_at text not null, updated_at text not null, expires_at text null, " +
            "sliding_expiration_ms integer null, tags_json text not null, primary key (namespace, cache_key))",
        get: `select value_json, expires_at, sliding_expiration_ms, tags_json from ${table} where namespace = ? and cache_key = ?`,
        selectNamespace: `select cache_key, tags_json from ${table} where namespace = ?`,
        deleteOne: `delete from ${table} where namespace = ? and cache_key = ?`,
        clearNamespace: `delete from ${table} where namespace = ?`,
        clearAll: `delete from ${table}`,
        cleanup: `delete from ${table} where expires_at is not null and expires_at <= ?`,
        set: `insert into ${table} (${columns}) values (${values}) on conflict(namespace, cache_key) do update set ${updateSet}`,
    });
}

class DistributedCache extends BaseCache {
    constructor(name, db, kind, options = {}) {
        super(name, kind, options);
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy distributed cache options must be a plain object.");
        }
        this.db = db;
        this.provider = kind;
        this.table = validateTableName(options.table);
        this.maxValueBytes = options.maxValueBytes ?? DEFAULT_VALUE_MAX_BYTES;
        if (!Number.isInteger(this.maxValueBytes) || this.maxValueBytes < 1) {
            throw new TypeError("Sloppy distributed cache maxValueBytes must be a positive integer.");
        }
        this.defaultTtlMs = normalizeTtlMs(options.ttlMs);
        this.sql = distributedSql(kind, this.table);
        this.initialized = false;
        Object.seal(this);
    }

    async _ensure() {
        if (this.initialized) {
            return;
        }
        await this.db.exec(this.sql.ensure, []);
        this.initialized = true;
    }

    _entryOptions(options = {}) {
        const normalized = normalizeEntryOptions(options);
        return Object.freeze({
            ...normalized,
            ttlMs: normalized.ttlMs ?? this.defaultTtlMs,
        });
    }

    _iso(timeMs) {
        return timeMs === undefined ? null : new Date(timeMs).toISOString();
    }

    _time(value) {
        if (value === null || value === undefined || value === "") {
            return undefined;
        }
        const time = Date.parse(String(value));
        return Number.isFinite(time) ? time : undefined;
    }

    async get(key, schemaOrOptions = undefined) {
        this._assertOpen("get");
        await this._ensure();
        const normalizedKey = this._key(key);
        const options = isSchema(schemaOrOptions)
            ? Object.freeze({ schema: schemaOrOptions })
            : this._entryOptions(schemaOrOptions ?? {});
        this._record("gets");
        const row = await this.db.queryOne(this.sql.get, [this.namespace, normalizedKey]);
        if (row === null || row === undefined) {
            this._record("misses");
            return undefined;
        }
        const expiresAt = this._time(row.expires_at ?? row.expiresAt);
        if (expiresAt !== undefined && nowMs(this.clock) >= expiresAt) {
            await this.remove(normalizedKey);
            this._record("expired");
            this._record("misses");
            return undefined;
        }
        this._record("hits");
        if (row.sliding_expiration_ms !== null && row.sliding_expiration_ms !== undefined) {
            const value = JSON.parse(row.value_json ?? row.valueJson);
            await this.set(normalizedKey, value, {
                ...options,
                slidingExpirationMs: Number(row.sliding_expiration_ms),
                tags: JSON.parse(row.tags_json ?? row.tagsJson ?? "[]"),
                ttlMs: Number(row.sliding_expiration_ms),
            });
        }
        return validateValueWithSchema(JSON.parse(row.value_json ?? row.valueJson), options.schema, normalizedKey);
    }

    async has(key) {
        return (await this.get(key)) !== undefined;
    }

    async set(key, value, options = {}) {
        this._assertOpen("set");
        await this._ensure();
        const normalizedKey = this._key(key);
        const normalizedOptions = this._entryOptions(options);
        const validated = validateValueWithSchema(value, normalizedOptions.schema, normalizedKey);
        const json = serializeJson(validated);
        if (jsonBytes(json) > this.maxValueBytes) {
            throw new SloppyCacheError("SLOPPY_E_CACHE_VALUE_TOO_LARGE", "Sloppy distributed cache value exceeds maxValueBytes.", {
                keyHash: stableHash(normalizedKey),
                provider: this.provider,
            });
        }
        const timestamp = nowDate(this.clock).toISOString();
        const expiresAt = this._iso(expiresAtFromOptions(normalizedOptions, this.clock));
        const tagsJson = serializeJson(normalizedOptions.tags);
        if (this.provider === "sqlserver") {
            try {
                await this.db.exec(this.sql.insert, [
                    this.namespace,
                    normalizedKey,
                    json,
                    timestamp,
                    timestamp,
                    expiresAt,
                    normalizedOptions.slidingExpirationMs ?? null,
                    tagsJson,
                ]);
            } catch {
                await this.db.exec(this.sql.update, [
                    json,
                    timestamp,
                    expiresAt,
                    normalizedOptions.slidingExpirationMs ?? null,
                    tagsJson,
                    this.namespace,
                    normalizedKey,
                ]);
            }
        } else {
            await this.db.exec(this.sql.set, [
                this.namespace,
                normalizedKey,
                json,
                timestamp,
                timestamp,
                expiresAt,
                normalizedOptions.slidingExpirationMs ?? null,
                tagsJson,
            ]);
        }
        this._record("sets");
        return this;
    }

    async remove(key) {
        this._assertOpen("remove");
        await this._ensure();
        await this.db.exec(this.sql.deleteOne, [this.namespace, this._key(key)]);
        this._record("removes");
        return true;
    }

    async invalidateTag(tag) {
        return this.invalidateTags([tag]);
    }

    async invalidateTags(tags) {
        this._assertOpen("invalidateTags");
        await this._ensure();
        const normalized = normalizeTags(tags, this);
        const rows = await this.db.query(this.sql.selectNamespace, [this.namespace]);
        let removed = 0;
        for (const row of rows) {
            const rowTags = JSON.parse(row.tags_json ?? row.tagsJson ?? "[]");
            if (rowTags.some((current) => normalized.includes(current))) {
                await this.db.exec(this.sql.deleteOne, [this.namespace, row.cache_key ?? row.cacheKey]);
                removed += 1;
            }
        }
        this._record("tagInvalidations");
        return removed;
    }

    async clear(options = {}) {
        this._assertOpen("clear");
        await this._ensure();
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy distributed cache clear options must be a plain object.");
        }
        if (options.dangerouslyClearAll === true) {
            await this.db.exec(this.sql.clearAll, []);
            return true;
        }
        await this.db.exec(this.sql.clearNamespace, [this.namespace]);
        return true;
    }

    async cleanup() {
        this._assertOpen("cleanup");
        await this._ensure();
        await this.db.exec(this.sql.cleanup, [nowDate(this.clock).toISOString()]);
        return true;
    }
}

class HybridCache extends BaseCache {
    constructor(name, options) {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy hybrid cache options must be a plain object.");
        }
        super(name, "hybrid", options);
        if (!isCache(options.memory) || options.memory.kind !== "memory") {
            throw new TypeError("Sloppy hybrid cache memory must be a memory Cache instance.");
        }
        if (!isCache(options.distributed) || options.distributed.kind === "memory" || options.distributed.kind === "hybrid") {
            throw new TypeError("Sloppy hybrid cache distributed must be a distributed Cache instance.");
        }
        this.memory = options.memory;
        this.distributed = options.distributed;
        this.populateMemoryOnDistributedHit = options.populateMemoryOnDistributedHit !== false;
        this.failOpenOnDistributedRead = options.failOpenOnDistributedRead === true;
        this.owned = options.owned !== false;
        Object.seal(this);
    }

    async get(key, schemaOrOptions = undefined) {
        this._assertOpen("get");
        this._record("gets");
        const normalizedKey = this._key(key);
        const memory = await this.memory.get(normalizedKey, schemaOrOptions);
        if (memory !== undefined) {
            this._record("hits");
            return memory;
        }
        let distributed;
        try {
            distributed = await this.distributed.get(normalizedKey, schemaOrOptions);
        } catch (error) {
            if (this.failOpenOnDistributedRead) {
                this._record("misses");
                return undefined;
            }
            throw error;
        }
        if (distributed === undefined) {
            this._record("misses");
            return undefined;
        }
        if (this.populateMemoryOnDistributedHit) {
            await this.memory.set(normalizedKey, distributed, isPlainObject(schemaOrOptions) ? schemaOrOptions : {});
        }
        this._record("hits");
        return distributed;
    }

    async has(key) {
        return (await this.get(key)) !== undefined;
    }

    async set(key, value, options = {}) {
        this._assertOpen("set");
        await this.distributed.set(key, value, options);
        await this.memory.set(key, value, options);
        this._record("sets");
        return this;
    }

    async remove(key) {
        this._assertOpen("remove");
        await Promise.all([this.memory.remove(key), this.distributed.remove(key)]);
        this._record("removes");
        return true;
    }

    async invalidateTag(tag) {
        return this.invalidateTags([tag]);
    }

    async invalidateTags(tags) {
        this._assertOpen("invalidateTags");
        const removed = await Promise.all([this.memory.invalidateTags(tags), this.distributed.invalidateTags(tags)]);
        this._record("tagInvalidations");
        return removed.reduce((sum, value) => sum + Number(value ?? 0), 0);
    }

    async clear(options = {}) {
        this._assertOpen("clear");
        await Promise.all([this.memory.clear(options), this.distributed.clear(options)]);
        return true;
    }

    async cleanup(options = {}) {
        this._assertOpen("cleanup");
        await Promise.all([this.memory.cleanup(options), this.distributed.cleanup(options)]);
        return true;
    }

    stats() {
        return Object.freeze({
            ...super.stats(),
            memory: this.memory.stats(),
            distributed: this.distributed.stats(),
        });
    }

    dispose() {
        super.dispose();
        if (this.owned) {
            this.memory.dispose?.();
            this.distributed.dispose?.();
        }
    }
}

class NoopCache extends BaseCache {
    constructor(name = "noop") {
        super(name, "noop", {});
    }
    async get() { return undefined; }
    async has() { return false; }
    async set() { return this; }
    async remove() { return false; }
    async invalidateTag() { return 0; }
    async invalidateTags() { return 0; }
    async clear() { return 0; }
    async cleanup() { return 0; }
}

function isCache(value) {
    return value !== null && typeof value === "object" && value[CACHE_MARKER] === true && value.__sloppyCache === true;
}

function cacheFromFactoryArgs(kind, nameOrOptions, maybeOptions) {
    if (typeof nameOrOptions === "string") {
        return { name: nameOrOptions, options: maybeOptions ?? {} };
    }
    return { name: "default", options: nameOrOptions ?? {} };
}

function distributedFromFactoryArgs(operation, dbOrOptions, maybeOptions) {
    if (isPlainObject(dbOrOptions) && dbOrOptions.db !== undefined) {
        return {
            name: dbOrOptions.name ?? "default",
            db: dbOrOptions.db,
            options: { ...dbOrOptions, ...maybeOptions },
        };
    }
    return {
        name: maybeOptions?.name ?? "default",
        db: dbOrOptions,
        options: maybeOptions ?? {},
    };
}

function createDistributed(operation, expectedKind, dbOrOptions, maybeOptions) {
    const { name, db, options } = distributedFromFactoryArgs(operation, dbOrOptions, maybeOptions);
    const actualKind = providerKind(db, operation);
    if (actualKind !== expectedKind) {
        throw new TypeError(`Sloppy Cache.${operation} expected ${expectedKind} connection, got ${actualKind}.`);
    }
    return new DistributedCache(name, db, actualKind, options);
}

function key(...parts) {
    if (parts.length === 0) {
        throw new TypeError("Sloppy Cache.key requires at least one part.");
    }
    return parts.map((part) => {
        if (part === null || part === undefined) {
            throw new TypeError("Sloppy Cache.key parts must not be null or undefined.");
        }
        return encodeURIComponent(String(part));
    }).join(":");
}

function tags(...values) {
    return normalizeTags(values.flat());
}

const Cache = Object.freeze({
    memory(nameOrOptions = undefined, maybeOptions = undefined) {
        const { name, options } = cacheFromFactoryArgs("memory", nameOrOptions, maybeOptions);
        return new MemoryCache(name, options);
    },
    sqlite(dbOrOptions, maybeOptions = undefined) {
        return createDistributed("sqlite", "sqlite", dbOrOptions, maybeOptions);
    },
    postgres(dbOrOptions, maybeOptions = undefined) {
        return createDistributed("postgres", "postgres", dbOrOptions, maybeOptions);
    },
    sqlServer(dbOrOptions, maybeOptions = undefined) {
        return createDistributed("sqlServer", "sqlserver", dbOrOptions, maybeOptions);
    },
    sqlserver(dbOrOptions, maybeOptions = undefined) {
        return createDistributed("sqlServer", "sqlserver", dbOrOptions, maybeOptions);
    },
    distributed(kind, db, options = undefined) {
        if (kind === "sqlite") {
            return createDistributed("sqlite", "sqlite", db, options);
        }
        if (kind === "postgres") {
            return createDistributed("postgres", "postgres", db, options);
        }
        if (kind === "sqlserver" || kind === "sqlServer") {
            return createDistributed("sqlServer", "sqlserver", db, options);
        }
        throw new TypeError("Sloppy Cache.distributed kind must be sqlite, postgres, or sqlserver.");
    },
    hybrid(name, options) {
        return new HybridCache(name, options);
    },
    noop(name = "noop") {
        return new NoopCache(name);
    },
    token: cacheToken,
    key,
    tags,
    isCache,
    keyHash: stableHash,
    __testing: Object.freeze({
        distributedSql,
        normalizeEntryOptions,
    }),
});

export {
    Cache,
    SloppyCacheError,
    isCache,
    normalizeEntryOptions,
    normalizeKey,
    normalizeName,
    normalizeTag,
    normalizeTags,
    stableHash,
};
