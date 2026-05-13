import { Results } from "./results.js";

const POLICY_MARKER = Symbol.for("sloppy.rateLimit.policy");
const STORE_MARKER = Symbol.for("sloppy.rateLimit.store");
const PARTITION_MARKER = Symbol.for("sloppy.rateLimit.partition");
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const DEFAULT_MAX_KEYS = 10000;
const DEFAULT_MAX_COST = 1000000;
const DEFAULT_PROBLEM_TYPE = "https://sloppy.dev/problems/rate-limit";
const ERROR_CODE = "SLOPPY_E_RATE_LIMIT_EXCEEDED";

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
    if (clock !== undefined && typeof clock.now === "function") {
        const value = clock.now();
        return value instanceof Date ? value.getTime() : Number(value);
    }
    return Date.now();
}

function stableHash(value) {
    const text = String(value);
    let hash = 0xcbf29ce484222325n;
    const prime = 0x100000001b3n;
    for (let index = 0; index < text.length; index += 1) {
        hash ^= BigInt(text.charCodeAt(index));
        hash = BigInt.asUintN(64, hash * prime);
    }
    return hash.toString(16).padStart(16, "0");
}

function stableStringify(value) {
    if (value === null || typeof value !== "object") {
        return JSON.stringify(value);
    }
    if (Array.isArray(value)) {
        return `[${value.map((entry) => stableStringify(entry)).join(",")}]`;
    }
    const keys = Object.keys(value).sort();
    return `{${keys.map((key) => `${JSON.stringify(key)}:${stableStringify(value[key])}`).join(",")}}`;
}

function positiveInteger(value, subject, max = Number.MAX_SAFE_INTEGER) {
    if (!Number.isInteger(value) || value <= 0 || value > max) {
        throw new TypeError(`Sloppy RateLimit ${subject} must be a positive integer no greater than ${max}.`);
    }
    return value;
}

function positiveNumber(value, subject, max = Number.MAX_SAFE_INTEGER) {
    if (typeof value !== "number" || !Number.isFinite(value) || value <= 0 || value > max) {
        throw new TypeError(`Sloppy RateLimit ${subject} must be a positive finite number no greater than ${max}.`);
    }
    return value;
}

function optionalName(value, subject = "name") {
    if (value === undefined) {
        return undefined;
    }
    if (typeof value !== "string" || value.length === 0 || /[\x00-\x1F\x7F]/u.test(value)) {
        throw new TypeError(`Sloppy RateLimit ${subject} must be a non-empty string without control characters.`);
    }
    return value;
}

function validateHeaderName(name) {
    if (typeof name !== "string" || !HEADER_TOKEN_PATTERN.test(name)) {
        throw new TypeError("Sloppy RateLimit header partition name must be a safe HTTP token.");
    }
    return name.toLowerCase();
}

function freezeJson(value, subject) {
    if (value === undefined || value === null || typeof value === "string" ||
        typeof value === "boolean" || Number.isFinite(value)) {
        return value;
    }
    if (Array.isArray(value)) {
        return Object.freeze(value.map((entry) => freezeJson(entry, subject)));
    }
    if (isPlainObject(value)) {
        const output = {};
        for (const [key, nested] of Object.entries(value)) {
            output[key] = freezeJson(nested, subject);
        }
        return Object.freeze(output);
    }
    throw new TypeError(`Sloppy RateLimit ${subject} must be JSON-compatible.`);
}

function requestHeader(ctx, name) {
    const headers = ctx?.request?.headers;
    if (headers === undefined || headers === null) {
        return undefined;
    }
    if (typeof headers.get === "function") {
        return headers.get(name) ?? headers.get(name.toLowerCase()) ?? undefined;
    }
    if (isPlainObject(headers)) {
        const lower = name.toLowerCase();
        for (const [key, value] of Object.entries(headers)) {
            if (key.toLowerCase() === lower) {
                return value;
            }
        }
    }
    return undefined;
}

function firstHeaderValue(value) {
    if (Array.isArray(value)) {
        return value.length === 0 ? undefined : firstHeaderValue(value[0]);
    }
    if (value === undefined || value === null) {
        return undefined;
    }
    return String(value);
}

function validateTrustProxyOptions(options = undefined, subject = "ip partition") {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError(`Sloppy RateLimit ${subject} options must be a plain object.`);
    }
    if (options?.trustProxy !== undefined && typeof options.trustProxy !== "boolean") {
        throw new TypeError(`Sloppy RateLimit ${subject} trustProxy must be a boolean.`);
    }
    return Object.freeze({ trustProxy: options?.trustProxy === true });
}

function rawRemoteAddress(ctx, options = undefined) {
    const proxy = validateTrustProxyOptions(options);
    if (proxy.trustProxy) {
        const forwarded = firstHeaderValue(requestHeader(ctx, "x-forwarded-for"))
            ?.split(",")[0]
            ?.trim();
        if (forwarded !== undefined && forwarded.length !== 0) {
            return forwarded;
        }
    }
    return firstHeaderValue(ctx?.connection?.remoteAddress) ??
        firstHeaderValue(ctx?.request?.remoteAddress) ??
        "unknown";
}

function createPartition(kind, metadata, resolver, options = {}) {
    const partition = {
        [PARTITION_MARKER]: true,
        kind,
        metadata: Object.freeze({ kind, ...metadata }),
        needsAuth: options.needsAuth === true,
        resolve: resolver,
        orIp(ipOptions = undefined) {
            const fallbackOptions = validateTrustProxyOptions(ipOptions, "orIp fallback");
            const primary = partition;
            return createPartition(
                `${kind}.orIp`,
                { ...primary.metadata, fallback: "ip", fallbackTrustProxy: fallbackOptions.trustProxy },
                (ctx) => {
                    const value = primary.resolve(ctx);
                    return value === undefined || value === null || value === ""
                        ? `ip:${rawRemoteAddress(ctx, fallbackOptions)}`
                        : value;
                },
                { needsAuth: false },
            );
        },
    };
    return Object.freeze(partition);
}

function ensurePartition(partitionBy) {
    if (partitionBy === "global") {
        return createPartition("global", { kind: "global" }, () => "global");
    }
    if (partitionBy?.[PARTITION_MARKER] === true && typeof partitionBy.resolve === "function") {
        return partitionBy;
    }
    if (typeof partitionBy === "function") {
        return createPartition(
            "custom",
            { kind: "custom", marker: partitionBy.name || "anonymous", partial: true },
            partitionBy,
        );
    }
    throw new TypeError("Sloppy RateLimit partitionBy is required; use RateLimit.partition.*() or 'global'.");
}

function normalizePolicyOptions(algorithm, options, required) {
    if (!isPlainObject(options)) {
        throw new TypeError(`Sloppy RateLimit.${algorithm} options must be a plain object.`);
    }
    const partition = ensurePartition(options.partitionBy);
    const statusCode = options.statusCode ?? 429;
    if (statusCode !== 429) {
        throw new TypeError("Sloppy RateLimit statusCode currently supports only 429.");
    }
    const cost = options.cost ?? 1;
    if (typeof cost !== "function") {
        positiveNumber(cost, "cost", DEFAULT_MAX_COST);
    }
    if (options.skip !== undefined && typeof options.skip !== "function") {
        throw new TypeError("Sloppy RateLimit skip must be a function.");
    }
    if (options.problem !== undefined && typeof options.problem !== "function" && !isPlainObject(options.problem)) {
        throw new TypeError("Sloppy RateLimit problem must be an object or function.");
    }
    const store = options.store;
    if (store !== undefined && typeof store !== "string" && store?.[STORE_MARKER] !== true) {
        throw new TypeError("Sloppy RateLimit store must be a store name or RateLimit store.");
    }
    const nameExplicit = options.name !== undefined;
    const name = optionalName(options.name) ?? `${algorithm}:${partition.metadata.kind}`;
    return Object.freeze({
        algorithm,
        name,
        nameExplicit,
        partition,
        store,
        cost,
        skip: options.skip,
        statusCode,
        problem: options.problem,
        required: Object.freeze(required),
    });
}

function createPolicy(algorithm, options, required) {
    const normalized = normalizePolicyOptions(algorithm, options, required);
    const policy = {
        [POLICY_MARKER]: true,
        algorithm,
        name: normalized.name,
        nameExplicit: normalized.nameExplicit,
        partition: normalized.partition,
        store: normalized.store,
        options: normalized,
        metadata: Object.freeze({
            name: normalized.name,
            algorithm,
            store: typeof normalized.store === "string"
                ? normalized.store
                : normalized.store?.kind ?? "default",
            partition: normalized.partition.metadata,
            requiresAuth: normalized.partition.needsAuth,
            ...required,
        }),
    };
    return Object.freeze(policy);
}

function fixedWindow(options) {
    return createPolicy("fixedWindow", options, {
        limit: positiveInteger(options?.limit, "limit"),
        windowMs: positiveInteger(options?.windowMs, "windowMs"),
    });
}

function slidingWindow(options) {
    return createPolicy("slidingWindow", options, {
        limit: positiveInteger(options?.limit, "limit"),
        windowMs: positiveInteger(options?.windowMs, "windowMs"),
    });
}

function tokenBucket(options) {
    return createPolicy("tokenBucket", options, {
        capacity: positiveNumber(options?.capacity, "capacity"),
        refillPerSecond: positiveNumber(options?.refillPerSecond, "refillPerSecond"),
    });
}

function concurrency(options) {
    return createPolicy("concurrency", options, {
        limit: positiveInteger(options?.limit, "limit"),
    });
}

class SloppyRateLimitError extends Error {
    constructor(message, options = undefined) {
        super(message);
        this.name = "SloppyRateLimitError";
        this.code = options?.code ?? "SLOPPY_E_RATE_LIMIT";
        this.policy = options?.policy;
        this.store = options?.store;
    }
}

function normalizeStoreOptions(options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy RateLimit.memory options must be a plain object.");
    }
    const maxKeys = positiveInteger(options?.maxKeys ?? DEFAULT_MAX_KEYS, "maxKeys", 1000000);
    const cleanupIntervalMs = positiveInteger(options?.cleanupIntervalMs ?? 60000, "cleanupIntervalMs");
    return Object.freeze({
        name: optionalName(options?.name) ?? "memory",
        maxKeys,
        cleanupIntervalMs,
        rejectOnMaxKeys: options?.rejectOnMaxKeys === true,
    });
}

function entryExpired(entry, current) {
    return entry.expiresAt !== undefined && entry.expiresAt <= current;
}

function retrySeconds(retryAfterMs) {
    return Math.max(1, Math.ceil(retryAfterMs / 1000));
}

function createMemoryStore(options = undefined) {
    const config = normalizeStoreOptions(options);
    const entries = new Map();
    let disposed = false;
    let lastCleanup = 0;
    let evictions = 0;
    let rejectedKeys = 0;

    function assertActive() {
        if (disposed) {
            throw new SloppyRateLimitError("SLOPPY_E_RATE_LIMIT_STORE_DISPOSED: memory rate-limit store is disposed.", {
                code: "SLOPPY_E_RATE_LIMIT_STORE_DISPOSED",
                store: config.name,
            });
        }
    }

    function cleanup(current, force = false) {
        if (!force && current - lastCleanup < config.cleanupIntervalMs) {
            return;
        }
        lastCleanup = current;
        for (const [key, entry] of entries) {
            if (entryExpired(entry, current)) {
                entries.delete(key);
                evictions += 1;
            }
        }
    }

    function ensureKey(key, createEntry, current) {
        cleanup(current);
        let entry = entries.get(key);
        if (entry !== undefined) {
            return entry;
        }
        if (entries.size >= config.maxKeys) {
            cleanup(current, true);
        }
        if (entries.size >= config.maxKeys) {
            if (config.rejectOnMaxKeys) {
                rejectedKeys += 1;
                throw new SloppyRateLimitError("SLOPPY_E_RATE_LIMIT_STORE_FULL: memory rate-limit store reached maxKeys.", {
                    code: "SLOPPY_E_RATE_LIMIT_STORE_FULL",
                    store: config.name,
                });
            }
            const oldest = entries.keys().next().value;
            if (oldest !== undefined) {
                entries.delete(oldest);
                evictions += 1;
            }
        }
        entry = createEntry();
        entries.set(key, entry);
        return entry;
    }

    function checkFixedWindow(key, policy, cost, current) {
        const windowMs = policy.metadata.windowMs;
        const limit = policy.metadata.limit;
        const entry = ensureKey(key, () => ({ count: 0, resetAt: current + windowMs, expiresAt: current + windowMs }), current);
        if (current >= entry.resetAt) {
            entry.count = 0;
            entry.resetAt = current + windowMs;
            entry.expiresAt = entry.resetAt;
        }
        const allowed = entry.count + cost <= limit;
        if (allowed) {
            entry.count += cost;
        }
        return {
            allowed,
            limit,
            remaining: Math.max(0, limit - entry.count),
            resetAtMs: entry.resetAt,
            retryAfterMs: allowed ? 0 : Math.max(1, entry.resetAt - current),
        };
    }

    function checkSlidingWindow(key, policy, cost, current) {
        const windowMs = policy.metadata.windowMs;
        const limit = policy.metadata.limit;
        const cutoff = current - windowMs;
        const entry = ensureKey(key, () => ({ hits: [], expiresAt: current + windowMs }), current);
        entry.hits = entry.hits.filter((hit) => hit.at > cutoff);
        const used = entry.hits.reduce((sum, hit) => sum + hit.cost, 0);
        const allowed = used + cost <= limit;
        if (allowed) {
            entry.hits.push({ at: current, cost });
        }
        const nextUsed = allowed ? used + cost : used;
        const oldest = entry.hits[0]?.at ?? current;
        entry.expiresAt = current + windowMs;
        return {
            allowed,
            limit,
            remaining: Math.max(0, limit - nextUsed),
            resetAtMs: oldest + windowMs,
            retryAfterMs: allowed ? 0 : Math.max(1, oldest + windowMs - current),
        };
    }

    function checkTokenBucket(key, policy, cost, current) {
        const capacity = policy.metadata.capacity;
        const refillPerSecond = policy.metadata.refillPerSecond;
        const entry = ensureKey(key, () => ({
            tokens: capacity,
            updatedAt: current,
            expiresAt: current + Math.ceil((capacity / refillPerSecond) * 1000) * 2,
        }), current);
        const elapsedSeconds = Math.max(0, (current - entry.updatedAt) / 1000);
        entry.tokens = Math.min(capacity, entry.tokens + elapsedSeconds * refillPerSecond);
        entry.updatedAt = current;
        const allowed = entry.tokens >= cost;
        if (allowed) {
            entry.tokens -= cost;
        }
        const missing = Math.max(0, cost - entry.tokens);
        const retryAfterMs = allowed ? 0 : Math.ceil((missing / refillPerSecond) * 1000);
        entry.expiresAt = current + Math.ceil((capacity / refillPerSecond) * 1000) * 2;
        return {
            allowed,
            limit: capacity,
            remaining: Math.max(0, Math.floor(entry.tokens)),
            resetAtMs: current + retryAfterMs,
            retryAfterMs,
        };
    }

    function checkConcurrency(key, policy, cost, current) {
        const limit = policy.metadata.limit;
        const entry = ensureKey(key, () => ({ active: 0, expiresAt: current + 60000 }), current);
        const allowed = entry.active + cost <= limit;
        if (!allowed) {
            return {
                allowed: false,
                limit,
                remaining: Math.max(0, limit - entry.active),
                resetAtMs: current + 1000,
                retryAfterMs: 1000,
            };
        }
        entry.active += cost;
        entry.expiresAt = undefined;
        let released = false;
        return {
            allowed: true,
            limit,
            remaining: Math.max(0, limit - entry.active),
            resetAtMs: current,
            retryAfterMs: 0,
            release() {
                if (released) {
                    return;
                }
                released = true;
                entry.active = Math.max(0, entry.active - cost);
                if (entry.active === 0) {
                    entries.delete(key);
                }
            },
        };
    }

    const store = {
        [STORE_MARKER]: true,
        __sloppyRateLimitStore: true,
        kind: "memory",
        name: config.name,
        async check(input) {
            assertActive();
            const current = input.nowMs ?? nowMs();
            const policyKey = input.policyKey ?? input.policy.name;
            const key = `${policyKey}:${input.policy.algorithm}:${input.partitionHash}`;
            if (input.policy.algorithm === "fixedWindow") {
                return checkFixedWindow(key, input.policy, input.cost, current);
            }
            if (input.policy.algorithm === "slidingWindow") {
                return checkSlidingWindow(key, input.policy, input.cost, current);
            }
            if (input.policy.algorithm === "tokenBucket") {
                return checkTokenBucket(key, input.policy, input.cost, current);
            }
            if (input.policy.algorithm === "concurrency") {
                return checkConcurrency(key, input.policy, input.cost, current);
            }
            throw new SloppyRateLimitError(`SLOPPY_E_RATE_LIMIT_ALGORITHM: unsupported algorithm '${input.policy.algorithm}'.`, {
                code: "SLOPPY_E_RATE_LIMIT_ALGORITHM",
                store: config.name,
            });
        },
        stats() {
            cleanup(nowMs(), true);
            return Object.freeze({
                kind: "memory",
                name: config.name,
                keys: entries.size,
                maxKeys: config.maxKeys,
                evictions,
                rejectedKeys,
                disposed,
            });
        },
        reset() {
            entries.clear();
            evictions = 0;
            rejectedKeys = 0;
        },
        dispose() {
            disposed = true;
            entries.clear();
        },
        async health() {
            return disposed
                ? { status: "unhealthy", message: "memory rate-limit store is disposed" }
                : { status: "healthy", data: store.stats() };
        },
    };
    return Object.freeze(store);
}

function redis(redisClient, options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy RateLimit.redis options must be a plain object.");
    }
    const prefix = options?.prefix ?? "sloppy:rl:";
    if (typeof prefix !== "string" || prefix.length === 0 || /[\x00-\x1F\x7F]/u.test(prefix)) {
        throw new TypeError("Sloppy RateLimit.redis prefix must be a non-empty string without control characters.");
    }
    const name = optionalName(options?.name) ?? "redis";
    return Object.freeze({
        [STORE_MARKER]: true,
        __sloppyRateLimitStore: true,
        kind: "redis",
        name,
        prefix,
        async check() {
            throw new SloppyRateLimitError(
                "SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE: Redis rate-limit store requires the Sloppy Redis provider, which is not present in this build.",
                { code: "SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE", store: name },
            );
        },
        async health() {
            if (redisClient !== undefined && typeof redisClient.ping === "function") {
                try {
                    await redisClient.ping();
                    return {
                        status: "degraded",
                        message: "Redis connection responded, but the Sloppy Redis rate-limit provider is not available in this build.",
                        errorCode: "SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE",
                        data: { kind: "redis", prefixHash: stableHash(prefix) },
                    };
                } catch (error) {
                    return { status: "unhealthy", message: String(error?.message ?? error) };
                }
            }
            return {
                status: "degraded",
                message: "Redis provider is not available in this Sloppy build.",
                errorCode: "SLOPPY_E_RATE_LIMIT_REDIS_UNAVAILABLE",
            };
        },
        stats() {
            return Object.freeze({ kind: "redis", name, prefixHash: stableHash(prefix), available: false });
        },
    });
}

function isRateLimitPolicy(value) {
    return value?.[POLICY_MARKER] === true;
}

function isRateLimitStore(value) {
    return value?.[STORE_MARKER] === true;
}

function snapshotRateLimitPolicy(policy) {
    if (!isRateLimitPolicy(policy)) {
        throw new TypeError("Sloppy endpoint rateLimit expects a RateLimit policy.");
    }
    return Object.freeze({
        ...policy.metadata,
        partition: Object.freeze({ ...policy.metadata.partition }),
    });
}

function resolveStore(ctx, policy) {
    if (isRateLimitStore(policy.store)) {
        return policy.store;
    }
    const stores = ctx?.__sloppyHost?.rateLimitStores;
    const name = typeof policy.store === "string" ? policy.store : "default";
    const store = stores?.get?.(name);
    if (store === undefined) {
        throw new SloppyRateLimitError(`SLOPPY_E_RATE_LIMIT_STORE_NOT_FOUND: rate-limit store '${name}' is not registered.`, {
            code: "SLOPPY_E_RATE_LIMIT_STORE_NOT_FOUND",
            policy: policy.name,
            store: name,
        });
    }
    return store;
}

function normalizeCost(policy, ctx) {
    const value = typeof policy.options.cost === "function" ? policy.options.cost(ctx) : policy.options.cost;
    return positiveNumber(value, "cost", DEFAULT_MAX_COST);
}

function routeLabel(ctx) {
    return ctx?.routePattern ?? ctx?.route?.pattern ?? "unknown";
}

function effectivePolicyKey(policy, ctx) {
    if (policy.nameExplicit === true) {
        return `named:${policy.name}`;
    }
    const method = ctx?.request?.method ?? ctx?.method ?? "UNKNOWN";
    return `route:${String(method).toUpperCase()}:${routeLabel(ctx)}:${policy.name}`;
}

function metricLabels(policy, store, outcome, ctx) {
    return Object.freeze({
        policy: policy.name,
        route: routeLabel(ctx),
        algorithm: policy.algorithm,
        store: store.kind,
        outcome,
    });
}

function recordMetric(ctx, name, labels, value = undefined) {
    try {
        const registry = ctx?.metrics;
        if (registry?.counter !== undefined && name.endsWith(".total")) {
            registry.counter(name).inc(labels, value ?? 1);
        } else if (registry?.gauge !== undefined) {
            registry.gauge(name).set(labels, value ?? 0);
        } else if (typeof registry?.increment === "function") {
            registry.increment(name, labels, value ?? 1);
        }
    } catch {
    }
}

function recordDiagnostic(ctx, policy, store, partitionHash, result) {
    try {
        ctx?.diagnostics?.record?.({
            code: result.allowed ? "SLOPPY_RATE_LIMIT_ALLOWED" : ERROR_CODE,
            subsystem: "rate-limit",
            severity: result.allowed ? "debug" : "warn",
            message: result.allowed ? "Rate limit allowed request." : "Rate limit denied request.",
            fields: {
                policy: policy.name,
                route: routeLabel(ctx),
                algorithm: policy.algorithm,
                store: store.kind,
                partitionHash,
                reason: result.allowed ? "allowed" : "limit-exceeded",
                retryAfterMs: result.retryAfterMs,
            },
        });
    } catch {
    }
}

function denialProblem(ctx, policy, result) {
    const base = Object.freeze({
        type: DEFAULT_PROBLEM_TYPE,
        title: "Too Many Requests",
        status: 429,
        code: ERROR_CODE,
    });
    const custom = typeof policy.options.problem === "function"
        ? policy.options.problem(ctx, result)
        : policy.options.problem;
    return freezeJson({ ...base, ...(custom ?? {}) }, "problem");
}

function denialResult(ctx, policy, result) {
    const retryAfter = retrySeconds(result.retryAfterMs);
    return Results.problem(denialProblem(ctx, policy, result), {
        status: 429,
        headers: {
            "Retry-After": String(retryAfter),
            "RateLimit-Limit": String(Math.trunc(result.limit)),
            "RateLimit-Remaining": String(Math.trunc(result.remaining)),
            "RateLimit-Reset": String(retryAfter),
        },
    });
}

function partitionHash(policy, partitionValue) {
    const metadata = policy.partition.metadata;
    return stableHash(stableStringify({
        customMarker: metadata.marker,
        fallback: metadata.fallback,
        fallbackTrustProxy: metadata.fallbackTrustProxy === true,
        kind: metadata.kind,
        name: metadata.name,
        partial: metadata.partial === true,
        trustProxy: metadata.trustProxy === true,
        value: String(partitionValue),
    }));
}

async function enforceRateLimit(ctx, policy) {
    if (!isRateLimitPolicy(policy)) {
        throw new TypeError("Sloppy rate-limit enforcement expects a RateLimit policy.");
    }
    if (policy.options.skip !== undefined && await policy.options.skip(ctx)) {
        return Object.freeze({ allowed: true, skipped: true });
    }
    if (policy.partition.needsAuth && ctx?.user?.authenticated !== true) {
        throw new SloppyRateLimitError("SLOPPY_E_RATE_LIMIT_AUTH_REQUIRED: authenticated partition requires an authenticated route.", {
            code: "SLOPPY_E_RATE_LIMIT_AUTH_REQUIRED",
            policy: policy.name,
        });
    }
    const partitionValue = await policy.partition.resolve(ctx);
    if (partitionValue === undefined || partitionValue === null || partitionValue === "") {
        throw new SloppyRateLimitError("SLOPPY_E_RATE_LIMIT_PARTITION_EMPTY: partition resolved to an empty value.", {
            code: "SLOPPY_E_RATE_LIMIT_PARTITION_EMPTY",
            policy: policy.name,
        });
    }
    const partitionHashValue = partitionHash(policy, partitionValue);
    const store = resolveStore(ctx, policy);
    const cost = normalizeCost(policy, ctx);
    const current = nowMs(ctx?.clock);
    let result;
    try {
        result = await store.check({
            policy,
            policyKey: effectivePolicyKey(policy, ctx),
            cost,
            partitionHash: partitionHashValue,
            nowMs: current,
        });
    } catch (error) {
        recordMetric(ctx, "rate_limit.store.errors.total", metricLabels(policy, store, "error", ctx));
        throw error;
    }
    const labels = metricLabels(policy, store, result.allowed ? "allowed" : "denied", ctx);
    recordMetric(ctx, "rate_limit.requests.total", labels);
    recordMetric(ctx, result.allowed ? "rate_limit.allowed.total" : "rate_limit.denied.total", labels);
    recordMetric(ctx, "rate_limit.tokens.remaining", labels, result.remaining);
    if (policy.algorithm === "concurrency") {
        recordMetric(ctx, "rate_limit.concurrency.active", labels, Math.max(0, result.limit - result.remaining));
    }
    recordDiagnostic(ctx, policy, store, partitionHashValue, result);
    if (result.allowed) {
        return Object.freeze({ ...result, partitionHash: partitionHashValue, store, release: result.release });
    }
    return Object.freeze({
        ...result,
        partitionHash: partitionHashValue,
        store,
        response: denialResult(ctx, policy, result),
    });
}

function rateLimitHealth(store) {
    if (!isRateLimitStore(store)) {
        throw new TypeError("Health.rateLimit expects a RateLimit store.");
    }
    return async () => {
        if (typeof store.health === "function") {
            return store.health();
        }
        return { status: "healthy", data: { kind: store.kind } };
    };
}

const partition = Object.freeze({
    ip(options = undefined) {
        const proxy = validateTrustProxyOptions(options);
        return createPartition(
            "ip",
            { kind: "ip", trustProxy: proxy.trustProxy },
            (ctx) => `ip:${rawRemoteAddress(ctx, proxy)}`,
        );
    },
    user() {
        return createPartition("user", { kind: "user" }, (ctx) => ctx?.user?.sub, { needsAuth: true });
    },
    apiKey() {
        return createPartition("apiKey", { kind: "apiKey" }, (ctx) => {
            if (ctx?.user?.authenticated === true && /api.?key/iu.test(ctx.user.scheme ?? ctx.user.authScheme ?? "")) {
                return ctx.user.sub;
            }
            return requestHeader(ctx, "x-api-key");
        });
    },
    header(name) {
        const header = validateHeaderName(name);
        return createPartition("header", { kind: "header", name: header }, (ctx) => requestHeader(ctx, header));
    },
    claim(name) {
        const claim = optionalName(name, "claim name");
        return createPartition("claim", { kind: "claim", name: claim }, (ctx) => ctx?.user?.claims?.[claim], { needsAuth: true });
    },
    routeParam(name) {
        const routeParam = optionalName(name, "route parameter name");
        return createPartition("routeParam", { kind: "routeParam", name: routeParam }, (ctx) => ctx?.route?.[routeParam]);
    },
    custom(fn, options = undefined) {
        if (typeof fn !== "function") {
            throw new TypeError("Sloppy RateLimit.partition.custom expects a function.");
        }
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy RateLimit.partition.custom options must be a plain object.");
        }
        const marker = optionalName(options?.marker ?? (fn.name || "anonymous"), "custom partition marker");
        return createPartition("custom", { kind: "custom", marker, partial: true }, fn);
    },
});

function token(name = undefined) {
    return optionalName(name, "token") ?? "default";
}

const RateLimit = Object.freeze({
    fixedWindow,
    slidingWindow,
    tokenBucket,
    concurrency,
    memory: createMemoryStore,
    redis,
    partition,
    token,
    health: rateLimitHealth,
});

export {
    RateLimit,
    SloppyRateLimitError,
    enforceRateLimit,
    isRateLimitPolicy,
    isRateLimitStore,
    rateLimitHealth,
    snapshotRateLimitPolicy,
};
