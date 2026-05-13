import { Text } from "./codec.js";
import { Random } from "./crypto.js";
import { HttpClient } from "./net.js";

const CLIENT_NAME_PATTERN = /^[A-Za-z][A-Za-z0-9_.-]{0,127}$/u;
const METHOD_WITH_BODY = new Set(["POST", "PUT", "PATCH"]);
const SAFE_RETRY_METHODS = new Set(["GET", "HEAD", "PUT", "DELETE"]);
const SENSITIVE_HEADER_PATTERN = /^(authorization|cookie|set-cookie|x-api-key|api-key)$/iu;
const SENSITIVE_QUERY_PATTERN = /(token|secret|password|authorization|api[_-]?key)/iu;
const HTTP_CLIENT_TOKEN_PREFIX = "http.";

class SloppyHttpClientError extends Error {
    constructor(code, message, options = undefined) {
        super(`${code}: ${message}`);
        this.name = "SloppyHttpClientError";
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

const HttpError = SloppyHttpClientError;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function isSchema(value) {
    return value !== null && typeof value === "object" && typeof value.validate === "function";
}

function isConfigReference(value) {
    return value !== null && typeof value === "object" && value.__sloppyConfigReference === true;
}

function validateClientName(name, subject = "Http.client") {
    if (typeof name !== "string" || !CLIENT_NAME_PATTERN.test(name)) {
        throw new TypeError(`${subject} name must start with a letter and contain only letters, digits, '.', '_', or '-'.`);
    }
    return name;
}

function httpServiceToken(name) {
    return `${HTTP_CLIENT_TOKEN_PREFIX}${validateClientName(name, "Http client token")}`;
}

function validateAbsoluteBaseUrl(value, subject) {
    if (isConfigReference(value)) {
        return value;
    }
    if (typeof value !== "string" || value.length === 0) {
        throw new TypeError(`${subject} baseUrl must be an absolute http:// or https:// URL.`);
    }
    let parsed;
    try {
        parsed = new URL(value);
    } catch {
        throw new TypeError(`${subject} baseUrl must be an absolute http:// or https:// URL.`);
    }
    if ((parsed.protocol !== "http:" && parsed.protocol !== "https:") || parsed.hash.length !== 0) {
        throw new TypeError(`${subject} baseUrl must be an absolute http:// or https:// URL without a fragment.`);
    }
    return parsed.toString().replace(/\/$/u, "");
}

function validateEndpointPath(path, subject) {
    if (typeof path !== "string" || !path.startsWith("/") || path.includes("#")) {
        throw new TypeError(`${subject} path must start with '/' and must not contain a fragment.`);
    }
    if (/[\x00-\x1F\x7F]/u.test(path)) {
        throw new TypeError(`${subject} path must not contain control characters.`);
    }
    return path;
}

function validateHeaders(headers, subject) {
    if (headers === undefined) {
        return undefined;
    }
    if (!isPlainObject(headers)) {
        throw new TypeError(`${subject} headers must be a plain object.`);
    }
    const normalized = {};
    for (const [name, value] of Object.entries(headers)) {
        if (!/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u.test(name)) {
            throw new TypeError(`${subject} header names must be safe HTTP tokens.`);
        }
        if (typeof value !== "string" || /[\x00-\x08\x0A-\x1F\x7F]/u.test(value)) {
            throw new TypeError(`${subject} header values must be safe strings.`);
        }
        normalized[name] = value;
    }
    return Object.freeze(normalized);
}

function positiveInteger(value, subject, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value) || value <= 0) {
        throw new TypeError(`${subject} must be a positive integer.`);
    }
    return value;
}

function nonNegativeInteger(value, subject, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value) || value < 0) {
        throw new TypeError(`${subject} must be a non-negative integer.`);
    }
    return value;
}

function optionalDelayMs(options, subject) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${subject} options must be a plain object.`);
    }
    return nonNegativeInteger(options.delayMs, `${subject} delayMs`);
}

function normalizePoolOptions(value, subject) {
    if (value === undefined) {
        return Object.freeze({
            maxConnectionsPerOrigin: 8,
            idleTimeoutMs: 30000,
            connectionLifetimeMs: undefined,
            pendingQueueLimit: 0,
        });
    }
    if (!isPlainObject(value)) {
        throw new TypeError(`${subject} pool must be a plain object.`);
    }
    return {
        maxConnectionsPerOrigin: positiveInteger(value.maxConnectionsPerOrigin, `${subject} pool.maxConnectionsPerOrigin`, 8),
        idleTimeoutMs: nonNegativeInteger(value.idleTimeoutMs, `${subject} pool.idleTimeoutMs`, 30000),
        connectionLifetimeMs: nonNegativeInteger(value.connectionLifetimeMs, `${subject} pool.connectionLifetimeMs`),
        pendingQueueLimit: nonNegativeInteger(value.pendingQueueLimit, `${subject} pool.pendingQueueLimit`, 0),
    };
}

function normalizeRetryPolicy(policy) {
    if (policy === undefined || policy === null) {
        return retryNone();
    }
    if (!isPlainObject(policy) || policy.kind === undefined) {
        throw new TypeError("Http retry policy must come from Http.retry.");
    }
    return policy;
}

function retryNone() {
    return Object.freeze({ kind: "none", maxAttempts: 1 });
}

function normalizeRetryMethods(methods, defaultMethods) {
    if (methods === undefined) {
        return Object.freeze([...defaultMethods]);
    }
    if (!Array.isArray(methods)) {
        throw new TypeError("Http retryOnMethods must be an array.");
    }
    return Object.freeze(methods.map((method) => String(method).toUpperCase()));
}

function retryFixed(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Http.retry.fixed options must be a plain object.");
    }
    return Object.freeze({
        kind: "fixed",
        maxAttempts: positiveInteger(options.maxAttempts, "Http.retry.fixed maxAttempts", 3),
        delayMs: nonNegativeInteger(options.delayMs, "Http.retry.fixed delayMs", 100),
        retryOnStatus: Object.freeze([...(options.retryOnStatus ?? [408, 429, 500, 502, 503, 504])]),
        retryOnMethods: normalizeRetryMethods(options.retryOnMethods, SAFE_RETRY_METHODS),
        jitter: options.jitter === true,
        allowPostWithIdempotencyKey: options.allowPostWithIdempotencyKey === true,
    });
}

function retryExponential(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Http.retry.exponential options must be a plain object.");
    }
    return Object.freeze({
        kind: "exponential",
        maxAttempts: positiveInteger(options.maxAttempts, "Http.retry.exponential maxAttempts", 3),
        initialDelayMs: nonNegativeInteger(options.initialDelayMs, "Http.retry.exponential initialDelayMs", 100),
        maxDelayMs: nonNegativeInteger(options.maxDelayMs, "Http.retry.exponential maxDelayMs", 2000),
        retryOnStatus: Object.freeze([...(options.retryOnStatus ?? [408, 429, 500, 502, 503, 504])]),
        retryOnMethods: normalizeRetryMethods(options.retryOnMethods, SAFE_RETRY_METHODS),
        jitter: options.jitter === true,
        allowPostWithIdempotencyKey: options.allowPostWithIdempotencyKey === true,
    });
}

function circuitBreaker(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Http.circuitBreaker options must be a plain object.");
    }
    const failureRatio = options.failureRatio ?? 0.5;
    if (!Number.isFinite(failureRatio) || failureRatio <= 0 || failureRatio > 1) {
        throw new TypeError("Http.circuitBreaker failureRatio must be greater than 0 and at most 1.");
    }
    return Object.freeze({
        failureRatio,
        minimumThroughput: positiveInteger(options.minimumThroughput, "Http.circuitBreaker minimumThroughput", 10),
        samplingWindowMs: positiveInteger(options.samplingWindowMs, "Http.circuitBreaker samplingWindowMs", 30000),
        breakDurationMs: positiveInteger(options.breakDurationMs, "Http.circuitBreaker breakDurationMs", 30000),
        halfOpenMaxCalls: positiveInteger(options.halfOpenMaxCalls, "Http.circuitBreaker halfOpenMaxCalls", 1),
    });
}

function bulkhead(options = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Http.bulkhead options must be a plain object.");
    }
    return Object.freeze({
        maxConcurrent: positiveInteger(options.maxConcurrent, "Http.bulkhead maxConcurrent", 32),
        maxQueue: nonNegativeInteger(options.maxQueue, "Http.bulkhead maxQueue", 128),
        queueTimeoutMs: nonNegativeInteger(options.queueTimeoutMs, "Http.bulkhead queueTimeoutMs", 1000),
    });
}

function timeout(ms) {
    return Object.freeze({ timeoutMs: positiveInteger(ms, "Http.timeout") });
}

function normalizeClientOptions(options = {}, subject = "Http.client") {
    if (!isPlainObject(options)) {
        throw new TypeError(`${subject} options must be a plain object.`);
    }
    const normalized = {
        baseUrl: validateAbsoluteBaseUrl(options.baseUrl, subject),
        headers: validateHeaders(options.headers, subject),
        timeoutMs: positiveInteger(options.timeoutMs, `${subject} timeoutMs`),
        retry: normalizeRetryPolicy(options.retry),
        circuitBreaker: options.circuitBreaker === undefined ? undefined : circuitBreaker(options.circuitBreaker),
        bulkhead: options.bulkhead === undefined ? undefined : bulkhead(options.bulkhead),
        pool: normalizePoolOptions(options.pool, subject),
        metrics: options.metrics === false ? false : true,
        diagnostics: options.diagnostics === false ? false : true,
    };
    return Object.freeze(normalized);
}

function resolveConfigValue(value, config, subject) {
    if (!isConfigReference(value)) {
        return value;
    }
    if (config === undefined || typeof config.require !== "function") {
        throw new SloppyHttpClientError(
            "SLOPPY_E_HTTP_CONFIG_UNAVAILABLE",
            `${subject} requires app config to resolve '${value.key}'.`,
        );
    }
    return validateAbsoluteBaseUrl(config.require(value.key), subject);
}

function createMetrics() {
    const counters = new Map();
    function key(name, labels = {}) {
        return `${name}:${JSON.stringify(Object.keys(labels).sort().map((entry) => [entry, labels[entry]]))}`;
    }
    return {
        increment(name, labels = {}, amount = 1) {
            const id = key(name, labels);
            const current = counters.get(id);
            counters.set(id, {
                name,
                labels: Object.freeze({ ...labels }),
                value: (current?.value ?? 0) + amount,
            });
        },
        snapshot() {
            return Object.freeze(Array.from(counters.values()).map((entry) => Object.freeze({
                name: entry.name,
                labels: entry.labels,
                value: entry.value,
            })));
        },
    };
}

function createDiagnostics() {
    const records = [];
    return {
        record(record) {
            records.push(Object.freeze({ ...record, fields: Object.freeze(record.fields ?? {}) }));
        },
        snapshot() {
            return Object.freeze([...records]);
        },
    };
}

function redactHeaders(headers = {}) {
    const output = {};
    for (const [name, value] of Object.entries(headers)) {
        output[name] = SENSITIVE_HEADER_PATTERN.test(name) ? "[REDACTED]" : value;
    }
    return Object.freeze(output);
}

function redactUrlTemplate(path, query = undefined) {
    if (query === undefined || query === null) {
        return path;
    }
    const keys = Object.keys(query);
    if (keys.length === 0) {
        return path;
    }
    const rendered = keys
        .sort()
        .map((key) => `${encodeURIComponent(key)}=${SENSITIVE_QUERY_PATTERN.test(key) ? "[REDACTED]" : "{value}"}`)
        .join("&");
    return `${path}?${rendered}`;
}

function sleep(ms, signal) {
    if (ms <= 0) {
        return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
        const timer = setTimeout(resolve, ms);
        if (signal !== undefined && typeof signal.addEventListener === "function") {
            const abort = () => {
                clearTimeout(timer);
                reject(new SloppyHttpClientError("SLOPPY_E_HTTP_CANCELLED", "HTTP request was cancelled."));
            };
            signal.addEventListener("abort", abort, { once: true });
        }
    });
}

function validateWithSchema(schema, value, code, message) {
    if (schema === undefined || schema === null) {
        return value;
    }
    if (!isSchema(schema)) {
        throw new TypeError(`${message} schema must be a Sloppy schema.`);
    }
    const result = schema.validate(value);
    if (!result.ok) {
        throw new SloppyHttpClientError(code, message, { issues: result.issues });
    }
    return result.value;
}

function appendPathQuery(path, query) {
    if (query === undefined || query === null) {
        return path;
    }
    if (!isPlainObject(query)) {
        throw new TypeError("Http request query must be a plain object.");
    }
    const params = new URLSearchParams();
    for (const [key, value] of Object.entries(query)) {
        if (value === undefined || value === null) {
            continue;
        }
        if (Array.isArray(value)) {
            for (const item of value) {
                params.append(key, String(item));
            }
        } else {
            params.append(key, String(value));
        }
    }
    const text = params.toString();
    return text.length === 0 ? path : `${path}?${text}`;
}

function expandPath(path, params = {}) {
    if (params === undefined || params === null) {
        params = {};
    }
    if (!isPlainObject(params)) {
        throw new TypeError("Http request params must be a plain object.");
    }
    return path.replace(/\{([A-Za-z_][0-9A-Za-z_]*)\}/gu, (match, name) => {
        if (!Object.prototype.hasOwnProperty.call(params, name)) {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_MISSING_PATH_PARAM", `HTTP request path parameter '${name}' is missing.`);
        }
        return encodeURIComponent(String(params[name]));
    });
}

function responseStatusClass(status) {
    return `${Math.floor(status / 100)}xx`;
}

function retryDelay(policy, attempt, response) {
    const retryAfter = response?.headers?.get?.("retry-after");
    if (retryAfter !== undefined && retryAfter !== null) {
        const seconds = Number(retryAfter);
        if (Number.isFinite(seconds) && seconds >= 0) {
            return seconds * 1000;
        }
    }
    if (policy.kind === "fixed") {
        return policy.jitter ? randomInteger(policy.delayMs + 1) : policy.delayMs;
    }
    const base = Math.min(policy.maxDelayMs, policy.initialDelayMs * (2 ** Math.max(0, attempt - 1)));
    return policy.jitter ? randomInteger(base + 1) : base;
}

function randomInteger(exclusiveMax) {
    if (exclusiveMax <= 1) {
        return 0;
    }
    const bytes = Random.bytes(4);
    const value = (
        (bytes[0] * 0x1000000) +
        (bytes[1] << 16) +
        (bytes[2] << 8) +
        bytes[3]
    ) >>> 0;
    return value % exclusiveMax;
}

function canRetryMethod(policy, method, requestOptions) {
    if (policy.retryOnMethods.includes(method)) {
        return true;
    }
    if (method === "POST" && policy.allowPostWithIdempotencyKey === true) {
        const headers = requestOptions.headers ?? {};
        return Object.keys(headers).some((name) => name.toLowerCase() === "idempotency-key");
    }
    return false;
}

class CircuitState {
    constructor(policy) {
        this.policy = policy;
        this.state = "closed";
        this.openedAt = 0;
        this.halfOpenCalls = 0;
        this.samples = [];
    }

    beforeRequest() {
        if (this.policy === undefined) {
            return;
        }
        const now = Date.now();
        if (this.state === "open") {
            if (now - this.openedAt < this.policy.breakDurationMs) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_CIRCUIT_OPEN", "HTTP client circuit is open.");
            }
            this.state = "half-open";
            this.halfOpenCalls = 0;
        }
        if (this.state === "half-open") {
            if (this.halfOpenCalls >= this.policy.halfOpenMaxCalls) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_CIRCUIT_OPEN", "HTTP client circuit is open.");
            }
            this.halfOpenCalls += 1;
        }
    }

    afterRequest(successful) {
        if (this.policy === undefined) {
            return;
        }
        const now = Date.now();
        if (this.state === "half-open") {
            if (successful) {
                this.state = "closed";
                this.samples = [];
                return;
            }
            this.state = "open";
            this.openedAt = now;
            return;
        }
        this.samples.push({ at: now, successful });
        this.samples = this.samples.filter((sample) => now - sample.at <= this.policy.samplingWindowMs);
        if (this.samples.length < this.policy.minimumThroughput) {
            return;
        }
        const failures = this.samples.filter((sample) => !sample.successful).length;
        if (failures / this.samples.length >= this.policy.failureRatio) {
            this.state = "open";
            this.openedAt = now;
        }
    }

    snapshot() {
        return Object.freeze({ state: this.state, sampleCount: this.samples.length });
    }
}

class BulkheadState {
    constructor(policy) {
        this.policy = policy;
        this.active = 0;
        this.queue = [];
        this.rejected = 0;
    }

    async enter(signal) {
        if (this.policy === undefined) {
            return () => {};
        }
        if (this.active < this.policy.maxConcurrent) {
            this.active += 1;
            return () => this.leave();
        }
        if (this.queue.length >= this.policy.maxQueue) {
            this.rejected += 1;
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_BULKHEAD_REJECTED", "HTTP client bulkhead queue is full.");
        }
        return await new Promise((resolve, reject) => {
            let timer;
            const record = {
                resolve: () => {
                    clearTimeout(timer);
                    this.active += 1;
                    resolve(() => this.leave());
                },
                reject,
            };
            this.queue.push(record);
            timer = setTimeout(() => {
                const index = this.queue.indexOf(record);
                if (index >= 0) {
                    this.queue.splice(index, 1);
                }
                this.rejected += 1;
                reject(new SloppyHttpClientError("SLOPPY_E_HTTP_BULKHEAD_REJECTED", "HTTP client bulkhead queue timed out."));
            }, this.policy.queueTimeoutMs);
            if (signal !== undefined && typeof signal.addEventListener === "function") {
                signal.addEventListener("abort", () => {
                    clearTimeout(timer);
                    const index = this.queue.indexOf(record);
                    if (index >= 0) {
                        this.queue.splice(index, 1);
                    }
                    reject(new SloppyHttpClientError("SLOPPY_E_HTTP_CANCELLED", "HTTP request was cancelled."));
                }, { once: true });
            }
        });
    }

    leave() {
        this.active = Math.max(0, this.active - 1);
        const next = this.queue.shift();
        if (next !== undefined) {
            next.resolve();
        }
    }

    snapshot() {
        return Object.freeze({ active: this.active, queued: this.queue.length, rejected: this.rejected });
    }
}

class HttpClientFactory {
    constructor(options = {}) {
        if (!isPlainObject(options)) {
            throw new TypeError("HttpClientFactory.create options must be a plain object.");
        }
        this._clients = new Map();
        for (const client of options.clients ?? []) {
            this.addClient(client);
        }
    }

    static create(options = {}) {
        return new HttpClientFactory(options);
    }

    addClient(client) {
        if (client?.__sloppyHttpClientRegistration?.kind !== "named" &&
            client?.__sloppyHttpClientRegistration?.kind !== "typed")
        {
            throw new TypeError("HttpClientFactory.addClient expects Http.client or Http.typedClient.");
        }
        const name = client.__sloppyHttpClientRegistration.name;
        if (this._clients.has(name)) {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_CLIENT_DUPLICATE", `HTTP client '${name}' is already registered.`);
        }
        this._clients.set(name, client);
        return this;
    }

    get(name) {
        validateClientName(name, "HttpClientFactory.get");
        const client = this._clients.get(name);
        if (client === undefined) {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_CLIENT_MISSING", `HTTP client '${name}' is not registered.`);
        }
        return client;
    }

    dispose() {
        return Promise.all(Array.from(this._clients.values(), (client) => client.dispose?.())).then(() => undefined);
    }
}

function createManagedClient(name, options, transport = undefined) {
    const metrics = createMetrics();
    const diagnostics = createDiagnostics();
    const circuit = new CircuitState(options.circuitBreaker);
    const bulk = new BulkheadState(options.bulkhead);
    const lowLevel = transport ?? HttpClient.create({
        baseUrl: options.baseUrl,
        headers: options.headers,
        timeoutMs: options.timeoutMs,
        pool: options.pool,
    });

    async function execute(method, path, requestOptions = {}) {
        const targetPath = appendPathQuery(expandPath(path, requestOptions.params), requestOptions.query);
        const safeTemplate = redactUrlTemplate(path, requestOptions.query);
        const headers = { ...(requestOptions.headers ?? {}) };
        if (requestOptions.correlationId !== undefined && headers["x-correlation-id"] === undefined) {
            headers["x-correlation-id"] = String(requestOptions.correlationId);
        }
        const effectiveRetry = normalizeRetryPolicy(requestOptions.retry ?? options.retry);
        let leaveBulkhead = () => {};
        let attempt = 0;
        let lastError;
        try {
            leaveBulkhead = await bulk.enter(requestOptions.signal);
            while (attempt < effectiveRetry.maxAttempts) {
                attempt += 1;
                circuit.beforeRequest();
                let response;
                try {
                    response = await lowLevel.request({
                        url: targetPath,
                        method,
                        headers,
                        json: requestOptions.json,
                        text: requestOptions.text,
                        bytes: requestOptions.bytes,
                        stream: requestOptions.stream,
                        timeoutMs: requestOptions.timeoutMs ?? options.timeoutMs,
                        deadline: requestOptions.deadline,
                        signal: requestOptions.signal,
                    });
                    const retryableStatus = effectiveRetry.retryOnStatus?.includes(response.status) === true;
                    if (
                        retryableStatus &&
                        attempt < effectiveRetry.maxAttempts &&
                        canRetryMethod(effectiveRetry, method, { headers }) &&
                        requestOptions.stream === undefined
                    ) {
                        metrics.increment("http.client.retries.total", { client: name, method, route: path });
                        await sleep(retryDelay(effectiveRetry, attempt, response), requestOptions.signal);
                        continue;
                    }
                    const successful = response.status < 500;
                    circuit.afterRequest(successful);
                    metrics.increment("http.client.requests.total", {
                        client: name,
                        method,
                        route: path,
                        status: String(response.status),
                        statusClass: responseStatusClass(response.status),
                        outcome: successful ? "success" : "failure",
                    });
                    return createResponse(response, { client: name, method, path: safeTemplate, attempt });
                } catch (error) {
                    lastError = error;
                    circuit.afterRequest(false);
                    if (
                        attempt < effectiveRetry.maxAttempts &&
                        canRetryMethod(effectiveRetry, method, { headers }) &&
                        requestOptions.stream === undefined &&
                        error?.code !== "SLOPPY_E_HTTP_CANCELLED"
                    ) {
                        metrics.increment("http.client.retries.total", { client: name, method, route: path });
                        await sleep(retryDelay(effectiveRetry, attempt), requestOptions.signal);
                        continue;
                    }
                    throw error;
                }
            }
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_RETRY_EXHAUSTED", "HTTP client retry attempts were exhausted.", { cause: lastError });
        } catch (error) {
            diagnostics.record({
                code: error?.code ?? "SLOPPY_E_HTTP_REQUEST_FAILED",
                message: "HTTP client request failed.",
                fields: {
                    client: name,
                    method,
                    path: safeTemplate,
                    headers: redactHeaders(headers),
                },
            });
            metrics.increment("http.client.errors.total", { client: name, method, route: path });
            throw error;
        } finally {
            leaveBulkhead();
        }
    }

    return {
        __sloppyHttpTransport: transport,
        name,
        request(method, path, requestOptions = {}) {
            return new HttpRequestBuilder(execute, String(method).toUpperCase(), validateEndpointPath(path, "Http.client request"), requestOptions);
        },
        get(path, requestOptions = {}) {
            return this.request("GET", path, requestOptions);
        },
        post(path, requestOptions = {}) {
            return this.request("POST", path, requestOptions);
        },
        put(path, requestOptions = {}) {
            return this.request("PUT", path, requestOptions);
        },
        patch(path, requestOptions = {}) {
            return this.request("PATCH", path, requestOptions);
        },
        delete(path, requestOptions = {}) {
            return this.request("DELETE", path, requestOptions);
        },
        head(path, requestOptions = {}) {
            return this.request("HEAD", path, requestOptions);
        },
        metrics() {
            return Object.freeze({
                counters: metrics.snapshot(),
                pool: lowLevel.poolStats?.(),
                circuit: circuit.snapshot(),
                bulkhead: bulk.snapshot(),
            });
        },
        diagnostics() {
            return diagnostics.snapshot();
        },
        health() {
            const circuitState = circuit.snapshot().state;
            return Object.freeze({
                name,
                status: circuitState === "open" ? "unhealthy" : "healthy",
                circuit: circuitState,
            });
        },
        dispose() {
            return lowLevel.dispose?.() ?? lowLevel.close?.();
        },
        close() {
            return lowLevel.close?.() ?? lowLevel.dispose?.();
        },
    };
}

class HttpRequestBuilder {
    constructor(execute, method, path, options = {}) {
        this._execute = execute;
        this._method = method;
        this._path = path;
        this._options = { ...options };
    }

    header(name, value) {
        this._options.headers = { ...(this._options.headers ?? {}), [name]: String(value) };
        return this;
    }

    headers(headers) {
        this._options.headers = { ...(this._options.headers ?? {}), ...validateHeaders(headers, "Http request") };
        return this;
    }

    query(query) {
        this._options.query = { ...(this._options.query ?? {}), ...query };
        return this;
    }

    timeoutMs(timeoutMs) {
        this._options.timeoutMs = positiveInteger(timeoutMs, "Http request timeoutMs");
        return this;
    }

    jsonBody(value) {
        if (!METHOD_WITH_BODY.has(this._method)) {
            throw new TypeError("Http request JSON body is only supported on POST, PUT, or PATCH.");
        }
        this._options.json = value;
        return this;
    }

    textBody(value) {
        if (!METHOD_WITH_BODY.has(this._method)) {
            throw new TypeError("Http request text body is only supported on POST, PUT, or PATCH.");
        }
        this._options.text = String(value);
        return this;
    }

    bytesBody(value) {
        if (!METHOD_WITH_BODY.has(this._method)) {
            throw new TypeError("Http request bytes body is only supported on POST, PUT, or PATCH.");
        }
        this._options.bytes = value;
        return this;
    }

    send() {
        return this._execute(this._method, this._path, this._options);
    }

    async text() {
        return await (await this.send()).text();
    }

    async bytes() {
        return await (await this.send()).bytes();
    }

    json(schema = undefined) {
        if (METHOD_WITH_BODY.has(this._method) && !isSchema(schema) && schema !== undefined && this._options.json === undefined) {
            this._options.json = schema;
            return this;
        }
        return this.send().then((response) => response.json(schema));
    }

    async expectStatus(status) {
        return await (await this.send()).expectStatus(status);
    }

    async expectJson(expected) {
        return await (await this.send()).expectJson(expected);
    }

    then(resolve, reject) {
        return this.send().then(resolve, reject);
    }
}

function createResponse(response, context) {
    let bodyCache;
    return Object.freeze({
        status: response.status,
        headers: response.headers,
        context: Object.freeze({ ...context }),
        async text() {
            if (bodyCache === undefined) {
                bodyCache = await response.text();
            }
            if (typeof bodyCache === "string") {
                return bodyCache;
            }
            return Text.utf8.decode(bodyCache);
        },
        async bytes() {
            if (bodyCache === undefined) {
                bodyCache = await response.bytes();
            }
            if (bodyCache instanceof Uint8Array) {
                return new Uint8Array(bodyCache);
            }
            return Text.utf8.encode(String(bodyCache));
        },
        async json(schema = undefined) {
            const value = await response.json();
            return validateWithSchema(schema, value, "SLOPPY_E_HTTP_RESPONSE_VALIDATION_FAILED", "HTTP response validation failed.");
        },
        async problem() {
            return await this.json();
        },
        expectStatus(status) {
            if (this.status !== status) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_UNEXPECTED_STATUS", `Expected HTTP status ${status}, got ${this.status}.`, { status: this.status });
            }
            return this;
        },
        expectHeader(name, expected) {
            const actual = this.headers.get(name);
            if (expected instanceof RegExp ? !expected.test(String(actual ?? "")) : actual !== expected) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_UNEXPECTED_HEADER", `Expected HTTP header '${name}' to match.`);
            }
            return this;
        },
        async expectJson(expected) {
            const actual = await this.json();
            if (JSON.stringify(actual) !== JSON.stringify(expected)) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_UNEXPECTED_JSON", "Expected HTTP JSON response to match.", { actual, expected });
            }
            return this;
        },
        async expectProblem(expected = {}) {
            const problem = await this.problem();
            for (const [key, value] of Object.entries(expected)) {
                if (problem[key] !== value) {
                    throw new SloppyHttpClientError("SLOPPY_E_HTTP_UNEXPECTED_PROBLEM", `Expected problem ${key} to be ${value}.`);
                }
            }
            return this;
        },
        throwOnError() {
            if (this.status >= 400) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_STATUS_ERROR", `HTTP response returned status ${this.status}.`, { status: this.status });
            }
            return this;
        },
    });
}

class EndpointBuilder {
    constructor(method, path) {
        this._method = method;
        this._path = validateEndpointPath(path, `Http.${method.toLowerCase()}`);
        this._paramsSchema = undefined;
        this._querySchema = undefined;
        this._bodySchema = undefined;
        this._returns = new Map();
    }

    params(schema) {
        this._paramsSchema = schema;
        return this;
    }

    query(schema) {
        this._querySchema = schema;
        return this;
    }

    body(schema) {
        this._bodySchema = schema;
        return this;
    }

    returns(status, schema = undefined) {
        if (!Number.isInteger(status) || status < 100 || status > 599) {
            throw new TypeError("Http endpoint return status must be an HTTP status code.");
        }
        this._returns.set(status, schema);
        return this;
    }

    __build(name) {
        return Object.freeze({
            name,
            method: this._method,
            path: this._path,
            paramsSchema: this._paramsSchema,
            querySchema: this._querySchema,
            bodySchema: this._bodySchema,
            returns: new Map(this._returns),
        });
    }
}

function endpoint(method, path) {
    return new EndpointBuilder(method, path);
}

function createTypedClient(name, options = {}, transport = undefined) {
    validateClientName(name, "Http.typedClient");
    const endpoints = options.endpoints;
    if (!isPlainObject(endpoints) || Object.keys(endpoints).length === 0) {
        throw new TypeError("Http.typedClient endpoints must be a non-empty plain object.");
    }
    const normalizedOptions = normalizeClientOptions(options, "Http.typedClient");
    const named = transport ?? createManagedClient(name, normalizedOptions);
    const methods = {};
    const endpointMetadata = [];
    for (const [endpointName, builder] of Object.entries(endpoints)) {
        if (typeof endpointName !== "string" || endpointName.length === 0) {
            throw new TypeError("Http.typedClient endpoint names must be non-empty strings.");
        }
        if (typeof builder?.__build !== "function") {
            throw new TypeError(`Http.typedClient endpoint '${endpointName}' must come from Http.get/post/put/patch/delete.`);
        }
        const contract = builder.__build(endpointName);
        endpointMetadata.push(contract);
        methods[endpointName] = async (input = {}, requestOptions = {}) => {
            if (!isPlainObject(input)) {
                throw new TypeError(`Http typed endpoint '${endpointName}' input must be a plain object.`);
            }
            const params = { ...input };
            delete params.query;
            delete params.body;
            const validatedParams = validateWithSchema(contract.paramsSchema, params, "SLOPPY_E_HTTP_REQUEST_VALIDATION_FAILED", "HTTP request params validation failed.");
            const validatedQuery = validateWithSchema(contract.querySchema, input.query ?? requestOptions.query, "SLOPPY_E_HTTP_REQUEST_VALIDATION_FAILED", "HTTP request query validation failed.");
            const validatedBody = validateWithSchema(contract.bodySchema, input.body ?? requestOptions.body, "SLOPPY_E_HTTP_REQUEST_VALIDATION_FAILED", "HTTP request body validation failed.");
            const response = await named.request(contract.method, contract.path, {
                ...requestOptions,
                params: validatedParams,
                query: validatedQuery,
                json: validatedBody,
            }).send();
            const responseSchema = contract.returns.get(response.status);
            if (!contract.returns.has(response.status)) {
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_UNEXPECTED_STATUS", `HTTP typed endpoint '${endpointName}' returned unexpected status ${response.status}.`, { status: response.status });
            }
            if (response.status >= 400) {
                const problem = await response.problem().catch(() => undefined);
                throw new SloppyHttpClientError("SLOPPY_E_HTTP_STATUS_ERROR", `HTTP typed endpoint '${endpointName}' returned status ${response.status}.`, { status: response.status, problem });
            }
            if (responseSchema === undefined || response.status === 204 || response.status === 304) {
                return undefined;
            }
            return await response.json(responseSchema);
        };
    }
    methods.send = (endpointName, input = {}, requestOptions = {}) => {
        if (typeof methods[endpointName] !== "function") {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_ENDPOINT_MISSING", `HTTP typed endpoint '${endpointName}' is not defined.`);
        }
        return methods[endpointName](input, requestOptions);
    };
    methods.metrics = () => named.metrics();
    methods.diagnostics = () => named.diagnostics();
    methods.dispose = () => named.dispose();
    Object.defineProperty(methods, "__sloppyHttpClientRegistration", {
        value: Object.freeze({
            kind: "typed",
            name,
            options: normalizedOptions,
            endpoints: Object.freeze(endpointMetadata),
            token: methods,
            namedToken: httpServiceToken(name),
            createNamed: (config) => createManagedClient(name, resolveClientOptions(normalizedOptions, config)),
            createTyped: (client) => {
                const mockTransport = client?.__sloppyHttpTransport;
                if (mockTransport !== undefined) {
                    return createTypedClient(
                        name,
                        { ...normalizedOptions, baseUrl: "http://testhost.invalid", endpoints },
                        createManagedClient(name, { ...normalizedOptions, baseUrl: "http://testhost.invalid" }, mockTransport),
                    );
                }
                return createTypedClient(name, { ...normalizedOptions, endpoints }, client);
            },
        }),
    });
    Object.defineProperty(methods, "__sloppyHttpClientToken", {
        value: httpServiceToken(name),
    });
    return Object.freeze(methods);
}

function resolveClientOptions(options, config) {
    return Object.freeze({
        ...options,
        baseUrl: resolveConfigValue(options.baseUrl, config, "Http client"),
    });
}

function createNamedClient(name, options = {}, transport = undefined) {
    validateClientName(name, "Http.client");
    const normalizedOptions = normalizeClientOptions(options, "Http.client");
    const client = createManagedClient(name, normalizedOptions, transport);
    Object.defineProperty(client, "__sloppyHttpClientRegistration", {
        value: Object.freeze({
            kind: "named",
            name,
            options: normalizedOptions,
            token: httpServiceToken(name),
            createNamed: (config) => createManagedClient(name, resolveClientOptions(normalizedOptions, config)),
        }),
    });
    Object.defineProperty(client, "__sloppyHttpClientToken", {
        value: httpServiceToken(name),
    });
    return Object.freeze(client);
}

function mockResponse(status, headers, body) {
    const bodyBytes = typeof body === "string" ? Text.utf8.encode(body) : body;
    return createResponse({
        status,
        headers: Object.freeze({
            get(name) {
                const lower = String(name).toLowerCase();
                const entry = Object.entries(headers).find(([key]) => key.toLowerCase() === lower);
                return entry?.[1] ?? null;
            },
        }),
        async text() {
            return Text.utf8.decode(bodyBytes);
        },
        async bytes() {
            return new Uint8Array(bodyBytes);
        },
        async json() {
            return JSON.parse(Text.utf8.decode(bodyBytes));
        },
    }, { mock: true });
}

function pathPatternToRegExp(path) {
    const parts = validateEndpointPath(path, "TestHttp.mock").split(/(\{[A-Za-z_][0-9A-Za-z_]*\})/u);
    return new RegExp(`^${parts.map((part) => {
        const match = /^\{([A-Za-z_][0-9A-Za-z_]*)\}$/u.exec(part);
        return match === null ? part.replace(/[.*+?^${}()|[\]\\]/gu, "\\$&") : "([^/]+)";
    }).join("")}$`, "u");
}

class TestHttpMock {
    constructor() {
        this._routes = [];
        this._calls = [];
        this._unexpected = [];
    }

    _route(method, path) {
        let route = this._routes.find((candidate) => candidate.method === method && candidate.path === path);
        if (route === undefined) {
            route = { method, path, pattern: pathPatternToRegExp(path), responses: [] };
            this._routes.push(route);
        }
        return Object.freeze({
            replyJson: (status, value, headers = {}, options = undefined) => {
                route.responses.push({ kind: "json", status, value, headers, delayMs: optionalDelayMs(options, "TestHttp.mock replyJson") });
                return this;
            },
            replyText: (status, value, headers = {}, options = undefined) => {
                route.responses.push({ kind: "text", status, value, headers, delayMs: optionalDelayMs(options, "TestHttp.mock replyText") });
                return this;
            },
            replyBytes: (status, value, headers = {}, options = undefined) => {
                route.responses.push({ kind: "bytes", status, value, headers, delayMs: optionalDelayMs(options, "TestHttp.mock replyBytes") });
                return this;
            },
            timeout: () => {
                route.responses.push({ kind: "timeout" });
                return this;
            },
            connectionError: () => {
                route.responses.push({ kind: "error" });
                return this;
            },
        });
    }

    get(path) { return this._route("GET", path); }
    post(path) { return this._route("POST", path); }
    put(path) { return this._route("PUT", path); }
    patch(path) { return this._route("PATCH", path); }
    delete(path) { return this._route("DELETE", path); }

    createClient(name = "mock", options = {}) {
        if (!isPlainObject(options)) {
            throw new TypeError("TestHttp.mock createClient options must be a plain object.");
        }
        const transport = Object.freeze({
            request: async ({ method, url, json, text, bytes, headers }) => {
                const [path] = String(url).split("?");
                const route = this._routes.find((candidate) => candidate.method === method && candidate.pattern.test(path));
                const call = Object.freeze({ method, path, url, json, text, bytes, headers: redactHeaders(headers) });
                this._calls.push(call);
                if (route === undefined) {
                    this._unexpected.push(call);
                    throw new SloppyHttpClientError("SLOPPY_E_HTTP_MOCK_UNEXPECTED_CALL", `Unexpected outbound HTTP call ${method} ${path}.`, { call });
                }
                const response = route.responses.length > 1 ? route.responses.shift() : route.responses[0];
                if (response === undefined) {
                    throw new SloppyHttpClientError("SLOPPY_E_HTTP_MOCK_EXHAUSTED", `No mock response configured for ${method} ${path}.`);
                }
                if (response.kind === "timeout") {
                    throw new SloppyHttpClientError("SLOPPY_E_HTTP_TIMEOUT", "Mock HTTP request timed out.");
                }
                if (response.kind === "error") {
                    throw new SloppyHttpClientError("SLOPPY_E_HTTP_CONNECT_FAILED", "Mock HTTP connection failed.");
                }
                if (response.delayMs !== undefined) {
                    await sleep(response.delayMs);
                }
                const headersOut = { ...response.headers };
                let body;
                if (response.kind === "json") {
                    body = Text.utf8.encode(JSON.stringify(response.value));
                    headersOut["content-type"] ??= "application/json; charset=utf-8";
                } else if (response.kind === "text") {
                    body = Text.utf8.encode(String(response.value));
                    headersOut["content-type"] ??= "text/plain; charset=utf-8";
                } else {
                    body = response.value instanceof Uint8Array ? response.value : new Uint8Array(response.value);
                }
                return mockResponse(response.status, headersOut, body);
            },
            poolStats: () => Object.freeze({
                connectionsCreated: 0,
                connectionsReused: Math.max(0, this._calls.length - 1),
                connectionsClosedIdle: 0,
                connectionsClosed: 0,
                poolWaitCount: 0,
                poolRejectedCount: 0,
                activeRequests: 0,
                idleConnections: 0,
                queuedRequests: 0,
            }),
        });
        return createNamedClient(name, { ...options, baseUrl: options.baseUrl ?? "http://testhost.invalid" }, transport);
    }

    expectCalled(method, path) {
        if (!this._calls.some((call) => call.method === method && call.path === path)) {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_MOCK_EXPECTATION_FAILED", `Expected outbound HTTP call ${method} ${path}.`);
        }
        return this;
    }

    expectNoUnexpectedCalls() {
        if (this._unexpected.length !== 0) {
            throw new SloppyHttpClientError("SLOPPY_E_HTTP_MOCK_UNEXPECTED_CALL", "Unexpected outbound HTTP calls were observed.", { calls: this._unexpected });
        }
        return this;
    }
}

function createTestHttpServiceOverrides(httpClients = {}) {
    if (!isPlainObject(httpClients)) {
        throw new TypeError("Sloppy TestHost httpClients overrides must be a plain object.");
    }
    const overrides = {};
    for (const [name, mock] of Object.entries(httpClients)) {
        validateClientName(name, "TestHost httpClients");
        if (typeof mock?.createClient !== "function") {
            throw new TypeError(`Sloppy TestHost httpClients.${name} must come from TestHttp.mock().`);
        }
        overrides[httpServiceToken(name)] = mock.createClient(name);
    }
    return overrides;
}

const TestHttp = Object.freeze({
    mock() {
        return new TestHttpMock();
    },
});

const Http = Object.freeze({
    client: createNamedClient,
    typedClient: createTypedClient,
    get(path) { return endpoint("GET", path); },
    post(path) { return endpoint("POST", path); },
    put(path) { return endpoint("PUT", path); },
    patch(path) { return endpoint("PATCH", path); },
    delete(path) { return endpoint("DELETE", path); },
    retry: Object.freeze({
        none: retryNone,
        fixed: retryFixed,
        exponential: retryExponential,
    }),
    circuitBreaker,
    bulkhead,
    timeout,
    clientOptionsSchema: Object.freeze({
        validate(value) {
            try {
                normalizeClientOptions(value, "Http.clientOptionsSchema");
                return Object.freeze({ ok: true, value });
            } catch (error) {
                return Object.freeze({
                    ok: false,
                    issues: Object.freeze([Object.freeze({
                        path: Object.freeze([]),
                        code: "http.client.options",
                        message: error.message,
                    })]),
                });
            }
        },
    }),
});

export {
    createTestHttpServiceOverrides,
    Http,
    HttpClientFactory,
    HttpError,
    SloppyHttpClientError,
    TestHttp,
};
