import * as fs from "node:fs/promises";
import * as net from "node:net";

import { ProviderHealth } from "./data.js";
import { Results } from "./results.js";

const HEALTH_STATUSES = Object.freeze(["healthy", "degraded", "unhealthy"]);
const STATUS_RANK = Object.freeze({
    healthy: 0,
    degraded: 1,
    unhealthy: 2,
});
const SECRET_KEY_PATTERN = /authorization|cookie|credential|connectionstring|password|secret|token|apikey|api_key|accesskey|privatekey/iu;
const DEFAULT_TIMEOUT_MS = 5000;
const DEFAULT_MAX_DATA_DEPTH = 4;
const DEFAULT_MAX_DATA_KEYS = 32;
const DEFAULT_MAX_STRING = 256;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function assertName(name, subject) {
    if (typeof name !== "string" || name.length === 0 || name.includes("\0")) {
        throw new TypeError(`Sloppy ${subject} name must be a non-empty string without NUL.`);
    }
}

function normalizeStatus(status, fallback = "healthy") {
    if (status === undefined) {
        return fallback;
    }
    if (!HEALTH_STATUSES.includes(status)) {
        throw new TypeError("Sloppy health status must be healthy, degraded, or unhealthy.");
    }
    return status;
}

function normalizeTags(tags = undefined) {
    if (tags === undefined) {
        return Object.freeze([]);
    }
    if (!Array.isArray(tags)) {
        throw new TypeError("Sloppy health check tags must be an array.");
    }
    const seen = new Set();
    const output = [];
    for (const tag of tags) {
        if (typeof tag !== "string" || tag.length === 0 || tag.includes("\0")) {
            throw new TypeError("Sloppy health check tags must be non-empty strings without NUL.");
        }
        if (!seen.has(tag)) {
            seen.add(tag);
            output.push(tag);
        }
    }
    return Object.freeze(output);
}

function normalizeTimeoutMs(value) {
    if (value === undefined) {
        return DEFAULT_TIMEOUT_MS;
    }
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
        throw new TypeError("Sloppy health timeoutMs must be an integer from 0 to 4294967295.");
    }
    return value;
}

function normalizeCacheMs(value) {
    if (value === undefined) {
        return 0;
    }
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
        throw new TypeError("Sloppy health cacheMs must be an integer from 0 to 4294967295.");
    }
    return value;
}

function normalizeCheckOptions(options = undefined) {
    if (options === undefined) {
        return Object.freeze({
            tags: Object.freeze(["ready"]),
            timeoutMs: DEFAULT_TIMEOUT_MS,
            cacheMs: 0,
            critical: true,
            degradedIsUnhealthy: false,
        });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy health check options must be a plain object.");
    }
    return Object.freeze({
        tags: normalizeTags(options.tags ?? ["ready"]),
        timeoutMs: normalizeTimeoutMs(options.timeoutMs),
        cacheMs: normalizeCacheMs(options.cacheMs),
        critical: options.critical !== false,
        degradedIsUnhealthy: options.degradedIsUnhealthy === true,
    });
}

function nowMs() {
    if (globalThis.performance !== undefined && typeof globalThis.performance.now === "function") {
        return globalThis.performance.now();
    }
    return Date.now();
}

function safeDate() {
    return new Date().toISOString();
}

function redactValue(value, depth = 0) {
    if (value === undefined) {
        return undefined;
    }
    if (value === null || typeof value === "boolean" || typeof value === "number") {
        return value;
    }
    if (typeof value === "string") {
        return value.length > DEFAULT_MAX_STRING ? `${value.slice(0, DEFAULT_MAX_STRING)}...` : value;
    }
    if (typeof value === "bigint") {
        return value.toString();
    }
    if (value instanceof Error) {
        return Object.freeze({
            name: typeof value.name === "string" ? value.name : "Error",
            message: String(value.message ?? ""),
        });
    }
    if (depth >= DEFAULT_MAX_DATA_DEPTH) {
        return "[truncated]";
    }
    if (Array.isArray(value)) {
        return Object.freeze(value.slice(0, DEFAULT_MAX_DATA_KEYS).map((item) => redactValue(item, depth + 1)));
    }
    if (typeof value === "object") {
        const output = {};
        let count = 0;
        for (const [key, nested] of Object.entries(value).sort(([left], [right]) => left.localeCompare(right))) {
            if (count >= DEFAULT_MAX_DATA_KEYS) {
                output.truncated = true;
                break;
            }
            output[key] = SECRET_KEY_PATTERN.test(key) ? "[redacted]" : redactValue(nested, depth + 1);
            count += 1;
        }
        return Object.freeze(output);
    }
    return String(value);
}

function normalizeCheckValue(value) {
    if (value === undefined || value === true) {
        return { status: "healthy" };
    }
    if (value === false) {
        return { status: "unhealthy" };
    }
    if (isPlainObject(value)) {
        const status = typeof value.status === "string"
            ? normalizeStatus(value.status)
            : typeof value.ok === "boolean"
                ? (value.ok ? "healthy" : "unhealthy")
                : "healthy";
        return {
            status,
            message: typeof value.message === "string" ? value.message : undefined,
            errorCode: typeof value.errorCode === "string" ? value.errorCode : undefined,
            diagnosticId: typeof value.diagnosticId === "string" ? value.diagnosticId : undefined,
            data: value.data === undefined ? undefined : redactValue(value.data),
        };
    }
    return { status: "healthy" };
}

function publicResult(name, outcome, started, checkedAtUtc, check, cached) {
    const result = {
        name,
        status: outcome.status,
        durationMs: Math.max(0, Math.round(nowMs() - started)),
        checkedAtUtc,
        tags: check.tags,
        critical: check.critical,
        cached,
        timeoutMs: check.timeoutMs,
    };
    if (outcome.message !== undefined) {
        result.message = outcome.message;
    }
    if (outcome.errorCode !== undefined) {
        result.errorCode = outcome.errorCode;
    }
    if (outcome.diagnosticId !== undefined) {
        result.diagnosticId = outcome.diagnosticId;
    }
    if (outcome.data !== undefined) {
        result.data = outcome.data;
    }
    return Object.freeze(result);
}

function runWithTimeout(promise, timeoutMs, name) {
    let timer;
    const timeout = new Promise((resolve) => {
        timer = setTimeout(() => {
            resolve({
                status: "unhealthy",
                message: `health check '${name}' exceeded ${timeoutMs}ms`,
                errorCode: "SLOPPY_E_HEALTH_TIMEOUT",
            });
        }, timeoutMs);
    });
    return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

async function runOneCheck(check, context) {
    const cacheNow = Date.now();
    if (check.cacheMs > 0 && check.cache !== undefined && cacheNow < check.cache.expiresAt) {
        return { ...check.cache.result, cached: true };
    }
    const started = nowMs();
    const checkedAtUtc = safeDate();
    let outcome;
    try {
        const run = Promise.resolve(check.check(context));
        outcome = normalizeCheckValue(await runWithTimeout(run, check.timeoutMs, check.name));
    } catch (error) {
        outcome = {
            status: "unhealthy",
            message: String(error?.message ?? "health check failed"),
            errorCode: "SLOPPY_E_HEALTH_CHECK_FAILED",
            data: redactValue(error),
        };
    }
    const result = publicResult(check.name, outcome, started, checkedAtUtc, check, false);
    if (check.cacheMs > 0) {
        check.cache = {
            expiresAt: cacheNow + check.cacheMs,
            result,
        };
    }
    return result;
}

function checkMatchesMode(check, mode, tags) {
    if (tags.length > 0 && !tags.every((tag) => check.tags.includes(tag))) {
        return false;
    }
    if (mode === "live") {
        return check.tags.includes("live");
    }
    if (mode === "ready") {
        return check.tags.includes("ready");
    }
    if (mode === "startup") {
        return check.tags.includes("startup");
    }
    return true;
}

function aggregateStatus(results, checksByName) {
    let rank = 0;
    for (const result of Object.values(results)) {
        const check = checksByName.get(result.name);
        let status = result.status;
        if (!result.critical && status === "unhealthy") {
            status = "degraded";
        }
        if (result.critical && result.status === "degraded" && check?.degradedIsUnhealthy === true) {
            status = "unhealthy";
        }
        rank = Math.max(rank, STATUS_RANK[status]);
    }
    return HEALTH_STATUSES[rank];
}

function summary(results) {
    const counts = { healthy: 0, degraded: 0, unhealthy: 0 };
    for (const result of Object.values(results)) {
        counts[result.status] += 1;
    }
    return Object.freeze(counts);
}

function createHealthRegistry(options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Health registry options must be a plain object.");
    }
    const checks = new Map();
    const registry = {
        check(name, check, checkOptions = undefined) {
            assertName(name, "health check");
            if (checks.has(name)) {
                throw new TypeError(`Sloppy health check '${name}' is already registered.`);
            }
            if (typeof check !== "function") {
                throw new TypeError("Sloppy health check must be a function.");
            }
            const normalized = normalizeCheckOptions(checkOptions);
            checks.set(name, {
                name,
                check,
                ...normalized,
                cache: undefined,
            });
            return registry;
        },
        checks() {
            return Object.freeze([...checks.values()].map((check) => Object.freeze({
                name: check.name,
                tags: check.tags,
                timeoutMs: check.timeoutMs,
                cacheMs: check.cacheMs,
                critical: check.critical,
            })));
        },
        async evaluate(mode = "health", context = undefined, options = undefined) {
            if (!["health", "live", "ready", "startup"].includes(mode)) {
                throw new TypeError("Sloppy health mode must be health, live, ready, or startup.");
            }
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy health evaluate options must be a plain object.");
            }
            const tags = normalizeTags(options?.tags ?? []);
            const started = nowMs();
            const checkedAtUtc = safeDate();
            const selected = [...checks.values()].filter((check) => checkMatchesMode(check, mode, tags));
            const entries = {};
            const checksByName = new Map(selected.map((check) => [check.name, check]));
            const settled = await Promise.allSettled(selected.map((check) => runOneCheck(check, context)));
            for (let index = 0; index < selected.length; index += 1) {
                const check = selected[index];
                const result = settled[index];
                entries[check.name] = result.status === "fulfilled"
                    ? result.value
                    : Object.freeze({
                        name: check.name,
                        status: "unhealthy",
                        message: String(result.reason?.message ?? "health check failed"),
                        errorCode: "SLOPPY_E_HEALTH_CHECK_FAILED",
                        durationMs: 0,
                        checkedAtUtc: safeDate(),
                        tags: check.tags,
                        critical: check.critical,
                        timeoutMs: check.timeoutMs,
                        cached: false,
                    });
            }
            const status = aggregateStatus(entries, checksByName);
            return Object.freeze({
                status,
                durationMs: Math.max(0, Math.round(nowMs() - started)),
                checkedAtUtc,
                checks: Object.freeze(entries),
                summary: summary(entries),
            });
        },
        resetCache() {
            for (const check of checks.values()) {
                check.cache = undefined;
            }
        },
    };
    return Object.freeze(registry);
}

function createHealthHandler(registry, mode, options = undefined) {
    const unhealthyStatus = options?.unhealthyStatus ?? 503;
    const degradedStatus = options?.degradedStatus ?? 200;
    return async function healthHandler(context) {
        const body = await registry.evaluate(mode, context);
        if (body.status === "unhealthy") {
            return Results.status(unhealthyStatus, body);
        }
        if (body.status === "degraded") {
            return Results.status(degradedStatus, body);
        }
        return Results.ok(body);
    };
}

function selfCheck() {
    return () => ({ status: "healthy", message: "process is alive" });
}

function runtimeCheck() {
    return (ctx) => {
        const shuttingDown = ctx?.lifecycle?.shuttingDown === true || ctx?.app?.lifecycle?.shuttingDown === true;
        const startupComplete = ctx?.lifecycle?.startupComplete ?? ctx?.app?.lifecycle?.startupComplete ?? true;
        if (startupComplete !== true) {
            return { status: "unhealthy", message: "runtime startup is not complete", errorCode: "SLOPPY_E_RUNTIME_STARTING" };
        }
        return shuttingDown
            ? { status: "unhealthy", message: "runtime is shutting down", errorCode: "SLOPPY_E_RUNTIME_SHUTTING_DOWN" }
            : { status: "healthy", message: "runtime is accepting requests" };
    };
}

function configCheck(required = []) {
    const requiredKeys = Array.isArray(required) ? required : required?.required ?? [];
    if (!Array.isArray(requiredKeys)) {
        throw new TypeError("Health.config required keys must be an array.");
    }
    return (ctx) => {
        const missing = [];
        for (const key of requiredKeys) {
            if (typeof key !== "string" || key.length === 0) {
                throw new TypeError("Health.config required keys must be non-empty strings.");
            }
            const value = ctx?.config?.get?.(key, undefined);
            if (value === undefined || value === null || value === "") {
                missing.push(key);
            }
        }
        return missing.length === 0
            ? { status: "healthy", data: { required: requiredKeys.length } }
            : { status: "unhealthy", message: "required config is missing", errorCode: "SLOPPY_E_CONFIG_MISSING", data: { missing } };
    };
}

function dataCheck(provider, options = undefined) {
    return async () => {
        const result = await ProviderHealth.check(provider, options ?? {});
        return { status: "healthy", data: result };
    };
}

function jobsCheck(resource = undefined, options = undefined) {
    const maxDead = options?.maxDead ?? 0;
    return () => {
        if (resource === undefined || resource === null) {
            return { status: "degraded", message: "job scheduler is not configured", data: { configured: false } };
        }
        const state = typeof resource.state === "function" ? resource.state() : resource.state;
        const dead = Number(state?.dead ?? state?.failed ?? 0);
        if (Number.isFinite(dead) && dead > maxDead) {
            return { status: "degraded", message: `${dead} failed jobs exceed threshold`, data: { dead, maxDead } };
        }
        return { status: "healthy", data: redactValue(state) };
    };
}

function memoryCheck(options = undefined) {
    const degradedRssBytes = options?.degradedRssBytes;
    const unhealthyRssBytes = options?.unhealthyRssBytes;
    return () => {
        const memory = globalThis.process?.memoryUsage?.();
        if (memory === undefined) {
            return { status: "degraded", message: "memory usage is unavailable", data: { available: false } };
        }
        const rss = memory.rss;
        if (typeof unhealthyRssBytes === "number" && rss >= unhealthyRssBytes) {
            return { status: "unhealthy", message: "RSS exceeds unhealthy threshold", data: { rss } };
        }
        if (typeof degradedRssBytes === "number" && rss >= degradedRssBytes) {
            return { status: "degraded", message: "RSS exceeds degraded threshold", data: { rss } };
        }
        return { status: "healthy", data: { rss, heapUsed: memory.heapUsed, heapTotal: memory.heapTotal } };
    };
}

function diskCheck(options = undefined) {
    if (!isPlainObject(options)) {
        throw new TypeError("Health.disk options must be a plain object.");
    }
    const targetPath = options.path;
    if (typeof targetPath !== "string" || targetPath.length === 0) {
        throw new TypeError("Health.disk path must be a non-empty string.");
    }
    return async () => {
        await fs.access(targetPath);
        if (typeof fs.statfs === "function" && options.minFreeBytes !== undefined) {
            const stats = await fs.statfs(targetPath);
            const free = Number(stats.bavail) * Number(stats.bsize);
            if (free < options.minFreeBytes) {
                return { status: "degraded", message: "disk free space is below threshold", data: { freeBytes: free, minFreeBytes: options.minFreeBytes } };
            }
            return { status: "healthy", data: { freeBytes: free } };
        }
        return { status: "healthy", data: { path: targetPath } };
    };
}

function httpCheck(url, options = undefined) {
    if (typeof url !== "string" || url.length === 0) {
        throw new TypeError("Health.http url must be a non-empty string.");
    }
    const expectedStatus = options?.expectedStatus ?? 200;
    return async () => {
        if (typeof fetch !== "function") {
            return { status: "degraded", message: "fetch is unavailable", errorCode: "SLOPPY_E_HEALTH_HTTP_UNAVAILABLE" };
        }
        const response = await fetch(url, {
            method: options?.method ?? "GET",
            signal: options?.signal,
        });
        return response.status === expectedStatus
            ? { status: "healthy", data: { status: response.status } }
            : { status: "unhealthy", message: "HTTP health target returned unexpected status", data: { status: response.status, expectedStatus } };
    };
}

function tcpCheck(host, port, options = undefined) {
    if (typeof host !== "string" || host.length === 0 || !Number.isInteger(port) || port < 1 || port > 65535) {
        throw new TypeError("Health.tcp requires a host string and TCP port.");
    }
    return async () => {
        const timeoutMs = normalizeTimeoutMs(options?.timeoutMs ?? DEFAULT_TIMEOUT_MS);
        return await new Promise((resolve) => {
            const socket = net.createConnection({ host, port });
            let done = false;
            function finish(status, message = undefined) {
                if (done) {
                    return;
                }
                done = true;
                socket.destroy();
                resolve({ status, message, data: { host, port } });
            }
            socket.setTimeout(timeoutMs, () => finish("unhealthy", "TCP health target timed out"));
            socket.once("connect", () => finish("healthy"));
            socket.once("error", (error) => finish("unhealthy", String(error.message ?? error)));
        });
    };
}

function unavailableCheck(feature) {
    return () => ({
        status: "degraded",
        message: `${feature} is unavailable in this app`,
        errorCode: "SLOPPY_E_HEALTH_FEATURE_UNAVAILABLE",
        data: { feature },
    });
}

const Health = Object.freeze({
    createRegistry: createHealthRegistry,
    handler: createHealthHandler,
    self: selfCheck,
    runtime: runtimeCheck,
    config: configCheck,
    data: dataCheck,
    jobs: jobsCheck,
    disk: diskCheck,
    memory: memoryCheck,
    http: httpCheck,
    tcp: tcpCheck,
    openApi: () => unavailableCheck("openapi"),
    cache: (cache) => () => cache === undefined
        ? { status: "degraded", message: "cache is not configured", data: { configured: false } }
        : { status: "healthy", data: redactValue(cache.state ?? { configured: true }) },
    storage: (storage) => () => storage === undefined
        ? { status: "degraded", message: "storage is not configured", data: { configured: false } }
        : { status: "healthy", data: redactValue(storage.state ?? { configured: true }) },
    redact: redactValue,
});

export { Health, createHealthHandler, createHealthRegistry, redactValue };
