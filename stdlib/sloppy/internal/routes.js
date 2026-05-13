import { defineFunctionModuleName } from "./modules.js";
import {
    anonymousUser,
    authorizePolicy,
    authorizeRoute,
    normalizeAuthRequirement,
    snapshotAuthRequirement,
} from "../auth.js";
import { Cache, isCache, stableHash } from "../cache.js";
import { Text } from "../codec.js";
import {
    createSseRouteHandler,
    createWebSocketRouteHandler,
    normalizeWebSocketRouteOptions,
    webSocketRouteOptions,
} from "../realtime.js";
import { Results } from "../results.js";
import { isSchema, schema as Schema } from "../schema.js";
import { cleanupAfterFailure, finishWithCleanup, validateServiceToken } from "./services.js";
import { isPlainObject } from "./shared.js";

const ROUTE_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE"]);
const ROUTE_KINDS = new Set(["http", "sse", "websocket"]);
const PREFLIGHT_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"]);
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const ROUTE_PARAM_PATTERN = /^\{([A-Za-z_][0-9A-Za-z_]*)(?::(str|int|uuid|alpha|float))?\}$/u;
const MEDIA_TYPE_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+\/[!#$%&'*+\-.^_`|~0-9A-Za-z]+(?:\s*;\s*[!#$%&'*+\-.^_`|~0-9A-Za-z]+=[!#$%&'*+\-.^_`|~0-9A-Za-z]+)*$/u;
const DEFAULT_OUTPUT_CACHE_STATUS_CODES = Object.freeze([200, 203, 204]);
const DEFAULT_OUTPUT_CACHE_MAX_BODY_BYTES = 1024 * 1024;

function validatePattern(pattern) {
    if (typeof pattern !== "string" || pattern.length === 0 || !pattern.startsWith("/")) {
        throw new TypeError("Sloppy app.mapGet pattern must be a non-empty string starting with '/'.");
    }
    if (pattern === "/") {
        return;
    }
    if (pattern !== "/" && (pattern.endsWith("/") || pattern.includes("//"))) {
        throw new TypeError("Sloppy route patterns use strict slashes and must not end with '/' or contain '//'.");
    }
    for (const segment of pattern.split("/").slice(1)) {
        if (segment.length === 0) {
            throw new TypeError("Sloppy route patterns must not contain empty segments.");
        }
        if ((segment.includes("{") || segment.includes("}")) && !ROUTE_PARAM_PATTERN.test(segment)) {
            throw new TypeError("Sloppy route parameters must be whole segments like {id}, {id:int}, {id:uuid}, {slug:alpha}, or {value:float}.");
        }
    }
}

function validateGroupPrefix(prefix) {
    if (typeof prefix !== "string" || prefix.length === 0 || !prefix.startsWith("/")) {
        throw new TypeError("Sloppy app.mapGroup prefix must be a non-empty string starting with '/'.");
    }
}

function validateGroupChildPattern(pattern) {
    if (typeof pattern !== "string" || pattern.length === 0) {
        throw new TypeError("Sloppy route group child pattern must be a non-empty string.");
    }
}

function validateHandler(handler) {
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy route handler must be a function.");
    }
}

function validateMiddleware(middleware) {
    if (typeof middleware !== "function") {
        throw new TypeError("Sloppy middleware must be a function.");
    }
}

function validateMiddlewareEntry(entry) {
    if (entry === null || typeof entry !== "object") {
        throw new TypeError("Sloppy middleware entries must carry { fn, sequence }.");
    }
    validateMiddleware(entry.fn);
    if (typeof entry.sequence !== "number") {
        throw new TypeError("Sloppy middleware entries must carry a numeric sequence.");
    }
}

function orderedMiddlewareFunctions(entries) {
    return [...entries]
        .sort((a, b) => a.sequence - b.sequence)
        .map((entry) => entry.fn);
}

function middlewareMetadata(middleware) {
    return Object.freeze({
        count: middleware.length,
    });
}

function invokeMiddlewarePipeline(context, middleware, terminal) {
    let index = -1;

    function dispatch(nextIndex) {
        if (nextIndex <= index) {
            throw new Error("Sloppy middleware next() must not be called more than once.");
        }

        index = nextIndex;
        const current = middleware[nextIndex];
        if (current === undefined) {
            return terminal();
        }

        let nextCalled = false;
        let downstreamPromise;
        function next() {
            if (nextCalled) {
                throw new Error("Sloppy middleware next() must not be called more than once.");
            }

            nextCalled = true;
            const downstream = dispatch(nextIndex + 1);
            downstreamPromise = Promise.resolve(downstream);
            return downstream;
        }

        const middlewareReturn = current(context, next);
        if (!nextCalled) {
            return middlewareReturn;
        }

        return Promise.resolve(middlewareReturn).then(
            (value) => downstreamPromise.then(() => value),
            (error) => {
                if (downstreamPromise === undefined) {
                    throw error;
                }
                return downstreamPromise.then(
                    () => {
                        throw error;
                    },
                    () => {
                        throw error;
                    },
                );
            },
        );
    }

    return dispatch(0);
}

function handleRouteError(host, error, context) {
    if (typeof host.handleError !== "function") {
        throw error;
    }
    return host.handleError(error, context);
}

function appendContextResponseHeaders(result, context) {
    const responseHeaders = context?.__sloppyResponseHeaders;
    if (!isPlainObject(responseHeaders) || result === null || typeof result !== "object") {
        return result;
    }

    return Object.freeze({
        ...result,
        headers: Object.freeze({
            ...(isPlainObject(result.headers) ? result.headers : {}),
            ...responseHeaders,
        }),
    });
}

function finishRouteResult(result, policy, context) {
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return Promise.resolve(result).then((value) => finishRouteResult(value, policy, context));
    }

    return finishWithCors(appendContextResponseHeaders(result, context), policy, context);
}

function finishHandledRouteError(host, error, policy, context) {
    return finishRouteResult(handleRouteError(host, error, context), policy, context);
}

function finishRouteError(host, error, policy, context, cleanup) {
    try {
        return finishWithCleanup(finishHandledRouteError(host, error, policy, context), cleanup);
    } catch (handledError) {
        return cleanupAfterFailure(handledError, cleanup);
    }
}

function validateController(controller) {
    if (typeof controller !== "function") {
        throw new TypeError("Sloppy controller must be a constructor function.");
    }
}

function validateControllerAction(action) {
    if (typeof action !== "string" || action.length === 0) {
        throw new TypeError("Sloppy controller action must be a non-empty string.");
    }
}

function validateMetadataOptions(options) {
    if (options === undefined) {
        return undefined;
    }

    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy route metadata options must be a plain object.");
    }

    return Object.freeze({ ...options });
}

function validateTag(tag) {
    if (typeof tag !== "string" || tag.length === 0) {
        throw new TypeError("Sloppy route group tags must be non-empty strings.");
    }
}

function validateName(name, subject) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError(`Sloppy ${subject} name must be a non-empty string.`);
    }
}

function validateMetadataText(value, subject) {
    if (typeof value !== "string" || value.length === 0) {
        throw new TypeError(`Sloppy ${subject} must be a non-empty string.`);
    }
}

function cloneFrozenJson(value, subject) {
    if (value === null || typeof value === "string" || typeof value === "boolean") {
        return value;
    }
    if (typeof value === "number") {
        if (!Number.isFinite(value)) {
            throw new TypeError(`Sloppy ${subject} must be JSON-compatible.`);
        }
        return value;
    }
    if (Array.isArray(value)) {
        return Object.freeze(value.map((item) => cloneFrozenJson(item, subject)));
    }
    if (isPlainObject(value)) {
        const out = {};
        for (const [key, current] of Object.entries(value)) {
            out[key] = cloneFrozenJson(current, subject);
        }
        return Object.freeze(out);
    }
    throw new TypeError(`Sloppy ${subject} must be JSON-compatible.`);
}

function validateStatusCode(status) {
    if (!Number.isInteger(status) || status < 100 || status > 599) {
        throw new TypeError("Sloppy route response status must be an integer from 100 to 599.");
    }
}

function validateMediaType(value, subject) {
    if (typeof value !== "string" || !MEDIA_TYPE_PATTERN.test(value)) {
        throw new TypeError(`Sloppy ${subject} must be an HTTP media type.`);
    }
}

function schemaMetadata(schema, subject) {
    validateSchema(schema, subject);
    return schema.metadata;
}

function routeParamEntries(pattern) {
    if (pattern === "/") {
        return [];
    }
    return pattern.split("/").slice(1)
        .map((segment) => ROUTE_PARAM_PATTERN.exec(segment))
        .filter((match) => match !== null)
        .map((match) => Object.freeze({ name: match[1], kind: match[2] ?? "str" }));
}

function encodeQuery(query) {
    if (query === undefined || query === null) {
        return "";
    }
    if (!isPlainObject(query)) {
        throw new TypeError("Sloppy urlFor query must be a plain object when provided.");
    }
    const pairs = [];
    for (const [key, value] of Object.entries(query)) {
        if (value === undefined || value === null) {
            continue;
        }
        const values = Array.isArray(value) ? value : [value];
        for (const item of values) {
            if (item !== undefined && item !== null) {
                pairs.push(`${encodeURIComponent(key)}=${encodeURIComponent(String(item))}`);
            }
        }
    }
    return pairs.length === 0 ? "" : `?${pairs.join("&")}`;
}

function routeParamValueSatisfies(kind, value) {
    if (kind === "str") {
        return true;
    }
    if (kind === "int") {
        return /^[0-9]+$/u.test(value);
    }
    if (kind === "uuid") {
        return /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/u.test(value);
    }
    if (kind === "alpha") {
        return /^[A-Za-z]+$/u.test(value);
    }
    if (kind === "float") {
        return /^(?:[0-9]+\.[0-9]*|\.[0-9]+)$/u.test(value);
    }
    return false;
}

function buildRouteUrl(pattern, params = {}, query = undefined) {
    if (!isPlainObject(params)) {
        throw new TypeError("Sloppy urlFor params must be a plain object.");
    }
    const used = new Set();
    const path = pattern === "/" ? "/" : pattern.split("/").map((segment, index) => {
        if (index === 0) {
            return "";
        }
        const match = ROUTE_PARAM_PATTERN.exec(segment);
        if (match === null) {
            return segment;
        }
        const name = match[1];
        if (params[name] === undefined || params[name] === null) {
            throw new TypeError(`Sloppy urlFor route parameter '${name}' is required.`);
        }
        const kind = match[2] ?? "str";
        const value = String(params[name]);
        if (!routeParamValueSatisfies(kind, value)) {
            throw new TypeError(`Sloppy urlFor route parameter '${name}' must satisfy '${kind}'.`);
        }
        used.add(name);
        return encodeURIComponent(value);
    }).join("/");
    const extra = Object.keys(params).filter((key) => !used.has(key));
    if (extra.length !== 0) {
        throw new TypeError(`Sloppy urlFor received extra route parameter '${extra[0]}'.`);
    }
    return `${path}${encodeQuery(query)}`;
}

function routeSegments(pattern) {
    return pattern === "/" ? [] : pattern.split("/").slice(1);
}

function routeSegmentRank(segment) {
    const match = ROUTE_PARAM_PATTERN.exec(segment);
    if (match === null) {
        return 3;
    }
    return (match[2] ?? "str") === "str" ? 1 : 2;
}

function compareRouteSpecificity(left, right) {
    const leftSegments = routeSegments(left.route.pattern);
    const rightSegments = routeSegments(right.route.pattern);
    const shared = Math.min(leftSegments.length, rightSegments.length);
    for (let index = 0; index < shared; index += 1) {
        const leftRank = routeSegmentRank(leftSegments[index]);
        const rightRank = routeSegmentRank(rightSegments[index]);
        if (leftRank !== rightRank) {
            return rightRank - leftRank;
        }
        if (leftRank === 3 && leftSegments[index] !== rightSegments[index]) {
            return leftSegments[index] < rightSegments[index] ? -1 : 1;
        }
    }
    if (leftSegments.length !== rightSegments.length) {
        return rightSegments.length - leftSegments.length;
    }
    return left.sourceOrder - right.sourceOrder;
}

function validateSchema(schema, subject) {
    if (!isSchema(schema)) {
        throw new TypeError(`Sloppy ${subject} schema must be a Schema value.`);
    }
}

function validateHeaderToken(value, subject) {
    if (typeof value !== "string" || !HEADER_TOKEN_PATTERN.test(value)) {
        throw new TypeError(`Sloppy CORS ${subject} must be an HTTP token string.`);
    }
}

function normalizeStringList(value, subject, { lower = false } = {}) {
    if (value === undefined) {
        return Object.freeze([]);
    }

    const values = Array.isArray(value) ? value : [value];
    const normalized = [];

    for (const current of values) {
        if (typeof current !== "string" || current.length === 0 || /[\x00-\x1F\x7F]/u.test(current)) {
            throw new TypeError(`Sloppy CORS ${subject} entries must be non-empty strings without control characters.`);
        }
        normalized.push(lower ? current.toLowerCase() : current);
    }

    return Object.freeze([...new Set(normalized)]);
}

function normalizeTokenList(value, subject) {
    const values = normalizeStringList(value, subject);
    for (const current of values) {
        validateHeaderToken(current, subject);
    }
    return values;
}

function normalizeCorsMethods(value) {
    const methods = normalizeStringList(value, "methods").map((method) => method.toUpperCase());

    for (const method of methods) {
        if (!PREFLIGHT_METHODS.has(method)) {
            throw new TypeError("Sloppy CORS methods must be supported HTTP methods.");
        }
    }

    return Object.freeze([...new Set(methods)]);
}

function normalizeCorsPolicy(policy) {
    if (!isPlainObject(policy)) {
        throw new TypeError("Sloppy app.useCors policy must be a plain object.");
    }

    const origins = normalizeStringList(policy.origins ?? policy.origin, "origins");
    if (origins.length === 0) {
        throw new TypeError("Sloppy CORS origins must include at least one origin or '*'.");
    }

    const allowAnyOrigin = origins.includes("*");
    if (allowAnyOrigin && origins.length !== 1) {
        throw new TypeError("Sloppy CORS '*' origin cannot be combined with other origins.");
    }

    const credentials = policy.credentials === true;
    if (allowAnyOrigin && credentials) {
        throw new TypeError("Sloppy CORS credentials require explicit origins.");
    }

    const maxAgeSeconds = policy.maxAgeSeconds ?? policy.maxAge;
    if (maxAgeSeconds !== undefined && (!Number.isInteger(maxAgeSeconds) || maxAgeSeconds < 0)) {
        throw new TypeError("Sloppy CORS maxAgeSeconds must be a non-negative integer.");
    }

    const headers = normalizeTokenList(policy.headers ?? policy.allowHeaders, "headers")
        .map((header) => header.toLowerCase());
    const exposedHeaders = normalizeTokenList(policy.exposedHeaders ?? policy.exposeHeaders, "exposedHeaders");

    return Object.freeze({
        origins,
        allowAnyOrigin,
        methods: normalizeCorsMethods(policy.methods),
        headers: Object.freeze([...new Set(headers)]),
        exposedHeaders,
        credentials,
        maxAgeSeconds,
    });
}

function snapshotCorsPolicy(policy) {
    if (policy === null) {
        return undefined;
    }

    return Object.freeze({
        origins: policy.origins,
        methods: policy.methods,
        headers: policy.headers,
        exposedHeaders: policy.exposedHeaders,
        credentials: policy.credentials,
        maxAgeSeconds: policy.maxAgeSeconds,
    });
}

function getRequestHeader(context, name) {
    const headers = context?.request?.headers;
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

function allowedOrigin(policy, origin) {
    if (typeof origin !== "string" || origin.length === 0) {
        return undefined;
    }

    if (policy.allowAnyOrigin) {
        return "*";
    }

    return policy.origins.includes(origin) ? origin : undefined;
}

function mergeVary(existing, value) {
    if (existing === undefined || existing.length === 0) {
        return value;
    }

    const tokens = existing.split(",").map((token) => token.trim().toLowerCase());
    return tokens.includes(value.toLowerCase()) ? existing : `${existing}, ${value}`;
}

function normalizeRouteStringList(value, subject, { lower = false, allowAll = false } = {}) {
    if (value === undefined) {
        return Object.freeze([]);
    }
    if (allowAll && value === "all") {
        return "all";
    }
    if (!Array.isArray(value)) {
        throw new TypeError(`Sloppy ${subject} must be an array${allowAll ? " or 'all'" : ""}.`);
    }
    const output = [];
    for (const item of value) {
        if (typeof item !== "string" || item.length === 0 || /[\x00-\x1F\x7F]/u.test(item)) {
            throw new TypeError(`Sloppy ${subject} entries must be non-empty strings without control characters.`);
        }
        output.push(lower ? item.toLowerCase() : item);
    }
    return Object.freeze([...new Set(output)]);
}

function normalizeOutputCacheOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy outputCache options must be a plain object.");
    }
    if (!Number.isInteger(options.ttlMs) || options.ttlMs < 1 || options.ttlMs > 0x7fffffff) {
        throw new TypeError("Sloppy outputCache ttlMs must be an integer from 1 to 2147483647.");
    }
    const statusCodes = options.statusCodes === undefined ? DEFAULT_OUTPUT_CACHE_STATUS_CODES : options.statusCodes;
    if (!Array.isArray(statusCodes) || statusCodes.length === 0 ||
        !statusCodes.every((status) => Number.isInteger(status) && status >= 100 && status <= 599))
    {
        throw new TypeError("Sloppy outputCache statusCodes must be a non-empty array of HTTP status codes.");
    }
    const maxBodyBytes = options.maxBodyBytes ?? DEFAULT_OUTPUT_CACHE_MAX_BODY_BYTES;
    if (!Number.isInteger(maxBodyBytes) || maxBodyBytes < 0) {
        throw new TypeError("Sloppy outputCache maxBodyBytes must be a non-negative integer.");
    }
    const varyByClaim = normalizeRouteStringList(options.varyByClaim, "outputCache varyByClaim");
    const sharedAuthenticated = options.allowSharedAuthenticated === true;
    if (options.allowAuthenticated === true && options.varyByUser !== true) {
        if (!sharedAuthenticated || (options.varyByRole !== true && varyByClaim.length === 0)) {
            throw new TypeError("Sloppy outputCache allowAuthenticated requires varyByUser; shared role/claim caching requires allowSharedAuthenticated.");
        }
    }
    if (options.tags !== undefined && typeof options.tags !== "function" && !Array.isArray(options.tags)) {
        throw new TypeError("Sloppy outputCache tags must be an array or function.");
    }
    return Object.freeze({
        ttlMs: options.ttlMs,
        cacheName: options.cacheName ?? "default",
        varyByQuery: normalizeRouteStringList(options.varyByQuery, "outputCache varyByQuery", { allowAll: true }),
        varyByHeader: normalizeRouteStringList(options.varyByHeader, "outputCache varyByHeader", { lower: true }),
        varyByRouteParams: normalizeRouteStringList(options.varyByRouteParams, "outputCache varyByRouteParams"),
        varyByUser: options.varyByUser === true,
        varyByClaim,
        varyByRole: options.varyByRole === true,
        tags: options.tags,
        statusCodes: Object.freeze([...new Set(statusCodes)]),
        maxBodyBytes,
        allowSetCookie: options.allowSetCookie === true,
        allowSharedAuthenticated: sharedAuthenticated,
        allowAuthenticated: options.varyByUser === true || (sharedAuthenticated && (options.varyByRole === true || varyByClaim.length !== 0)),
    });
}

function normalizeCacheHeaderOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy cacheHeaders options must be a plain object.");
    }
    if (options.cacheControl !== undefined && (typeof options.cacheControl !== "string" || options.cacheControl.length === 0)) {
        throw new TypeError("Sloppy cacheHeaders cacheControl must be a non-empty string.");
    }
    if (options.vary !== undefined && !Array.isArray(options.vary)) {
        throw new TypeError("Sloppy cacheHeaders vary must be an array.");
    }
    return Object.freeze({
        cacheControl: options.cacheControl,
        vary: options.vary === undefined ? Object.freeze([]) : normalizeRouteStringList(options.vary, "cacheHeaders vary"),
        etag: options.etag === true,
        lastModified: options.lastModified,
    });
}

function appendResponseHeaders(result, headers) {
    if (result === null || typeof result !== "object") {
        return result;
    }
    return Object.freeze({
        ...result,
        headers: Object.freeze({
            ...(isPlainObject(result.headers) ? result.headers : {}),
            ...headers,
        }),
    });
}

function resultBodySize(result) {
    if (result?.body === undefined) {
        return 0;
    }
    if (typeof result.body === "string") {
        return Text.utf8.encode(result.body).byteLength;
    }
    if (result.body instanceof Uint8Array) {
        return result.body.byteLength;
    }
    const serialized = JSON.stringify(result.body);
    return serialized === undefined ? 0 : Text.utf8.encode(serialized).byteLength;
}

function containsNonJsonValue(value, seen = new Set()) {
    if (value === null || typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
        return false;
    }
    if (typeof value === "function" || typeof value === "symbol" || typeof value === "undefined" || typeof value === "bigint") {
        return true;
    }
    if (typeof value !== "object") {
        return true;
    }
    if (seen.has(value)) {
        return true;
    }
    seen.add(value);
    if (Array.isArray(value)) {
        return value.some((item) => containsNonJsonValue(item, seen));
    }
    if (!isPlainObject(value)) {
        return true;
    }
    return Object.values(value).some((item) => containsNonJsonValue(item, seen));
}

function outputCacheUnsupportedResultReason(result) {
    if (result?.__sloppyResult !== true) {
        return "unsupported-result";
    }
    if (result.kind === "stream") {
        return "streaming";
    }
    if (result.kind === "json") {
        return containsNonJsonValue(result.body) ? "unsupported-body" : undefined;
    }
    if (result.kind === "text") {
        return typeof result.body === "string" ? undefined : "unsupported-body";
    }
    if (result.kind === "bytes") {
        return result.body instanceof Uint8Array ? undefined : "unsupported-body";
    }
    if (result.kind === "empty") {
        return result.body === undefined ? undefined : "unsupported-body";
    }
    return "unsupported-result-kind";
}

function outputCacheBypassReason(result, options, context, routeInfo) {
    const method = context.request?.method ?? routeInfo.method;
    if (method !== "GET" && method !== "HEAD") {
        return "method";
    }
    if (routeInfo.auth !== undefined && options.allowAuthenticated !== true) {
        return "auth-unsafe";
    }
    const unsupported = outputCacheUnsupportedResultReason(result);
    if (unsupported !== undefined) {
        return unsupported;
    }
    if (!options.statusCodes.includes(result.status)) {
        return "status";
    }
    if (Array.isArray(result.setCookies) && result.setCookies.length !== 0 && options.allowSetCookie !== true) {
        return "set-cookie";
    }
    if (resultBodySize(result) > options.maxBodyBytes) {
        return "body-too-large";
    }
    return undefined;
}

function outputCachePartition(options, context) {
    const parts = [];
    if (options.varyByUser) {
        const sub = context.user?.sub ?? context.user?.id;
        if (typeof sub !== "string" || sub.length === 0) {
            throw new Error("Sloppy outputCache varyByUser requires an authenticated user subject.");
        }
        parts.push(["user", stableHash(sub)]);
    }
    if (options.varyByRole) {
        const roles = Array.isArray(context.user?.roles) ? context.user.roles : [];
        parts.push(["roles", roles.map(String).sort().join(",")]);
    }
    for (const claim of options.varyByClaim) {
        const value = typeof context.user?.claim === "function" ? context.user.claim(claim) : context.user?.claims?.[claim];
        parts.push([`claim:${claim}`, stableHash(String(value ?? ""))]);
    }
    return parts;
}

function outputCacheKey(options, context, routeInfo) {
    const query = context.query ?? {};
    const queryParts = options.varyByQuery === "all"
        ? Object.entries(query).sort(([left], [right]) => left.localeCompare(right))
        : options.varyByQuery.map((name) => [name, query[name] ?? ""]);
    const headerParts = options.varyByHeader.map((name) => [name, getRequestHeader(context, name) ?? ""]);
    const routeParts = options.varyByRouteParams.map((name) => [name, context.route?.[name] ?? ""]);
    return `output:${stableHash(JSON.stringify({
        method: context.request?.method ?? routeInfo.method,
        route: routeInfo.pattern,
        query: queryParts,
        headers: headerParts,
        params: routeParts,
        partition: outputCachePartition(options, context),
    }))}`;
}

function outputCacheTags(options, context, routeInfo) {
    const source = typeof options.tags === "function" ? options.tags(context) : options.tags;
    const tags = Array.isArray(source) ? source : [];
    return Object.freeze([`route:${routeInfo.pattern}`, ...tags]);
}

function recordOutputCacheDiagnostic(context, code, fields = {}) {
    context.diagnostics?.record?.({
        code,
        subsystem: "output-cache",
        severity: code.endsWith("BYPASS") ? "debug" : "info",
        fields,
    });
}

function recordOutputCacheMetric(context, labels) {
    const metrics = context.metrics;
    if (metrics === undefined || metrics === null) {
        return;
    }
    const safeLabels = Object.freeze({
        route: labels.route,
        outcome: labels.outcome,
        reason: labels.reason ?? "",
        statusClass: labels.statusClass ?? "",
    });
    try {
        if (typeof metrics.increment === "function") {
            metrics.increment("output_cache.requests.total", safeLabels);
            return;
        }
        metrics.counter?.("output_cache.requests.total", {
            description: "Output cache requests by route pattern and outcome.",
        })?.inc(safeLabels);
    } catch {
        // Metrics must not change route behavior.
    }
}

async function invokeWithOutputCache(context, routeInfo, outputCache, handler) {
    const cache = context.services?.tryGet?.(Cache.token(outputCache.cacheName));
    if (!isCache(cache)) {
        throw new Error(`Sloppy outputCache cache '${outputCache.cacheName}' is not registered.`);
    }
    if (routeInfo.auth !== undefined && outputCache.allowAuthenticated !== true) {
        recordOutputCacheDiagnostic(context, "SLOPPY_OUTPUT_CACHE_BYPASS", { reason: "auth-unsafe", route: routeInfo.pattern });
        return appendResponseHeaders(await handler(), { "X-Sloppy-Output-Cache": "BYPASS" });
    }
    const key = outputCacheKey(outputCache, context, routeInfo);
    const cached = await cache.get(key);
    if (cached !== undefined) {
        recordOutputCacheMetric(context, { route: routeInfo.pattern, outcome: "hit", statusClass: `${Math.trunc(cached.status / 100)}xx` });
        recordOutputCacheDiagnostic(context, "SLOPPY_OUTPUT_CACHE_HIT", { route: routeInfo.pattern, keyHash: stableHash(key) });
        return appendResponseHeaders(cached, { "X-Sloppy-Output-Cache": "HIT" });
    }
    const result = await handler();
    const reason = outputCacheBypassReason(result, outputCache, context, routeInfo);
    if (reason !== undefined) {
        recordOutputCacheMetric(context, { route: routeInfo.pattern, outcome: "bypass", reason, statusClass: result?.status === undefined ? "" : `${Math.trunc(result.status / 100)}xx` });
        recordOutputCacheDiagnostic(context, "SLOPPY_OUTPUT_CACHE_BYPASS", { route: routeInfo.pattern, reason });
        return appendResponseHeaders(result, { "X-Sloppy-Output-Cache": "BYPASS" });
    }
    await cache.set(key, result, {
        ttlMs: outputCache.ttlMs,
        tags: outputCacheTags(outputCache, context, routeInfo),
    });
    recordOutputCacheMetric(context, { route: routeInfo.pattern, outcome: "miss", statusClass: `${Math.trunc(result.status / 100)}xx` });
    recordOutputCacheDiagnostic(context, "SLOPPY_OUTPUT_CACHE_MISS", { route: routeInfo.pattern, keyHash: stableHash(key) });
    return appendResponseHeaders(result, { "X-Sloppy-Output-Cache": "MISS" });
}

function applyCacheHeaders(result, options) {
    const headers = {};
    if (options.cacheControl !== undefined) {
        headers["Cache-Control"] = options.cacheControl;
    }
    for (const value of options.vary) {
        const current = headers.Vary ?? result?.headers?.Vary ?? result?.headers?.vary;
        headers.Vary = mergeVary(current, value);
    }
    if (options.lastModified !== undefined) {
        const date = options.lastModified instanceof Date ? options.lastModified : new Date(options.lastModified);
        if (Number.isFinite(date.getTime())) {
            headers["Last-Modified"] = date.toUTCString();
        }
    }
    if (options.etag) {
        headers.ETag = `"${stableHash(JSON.stringify(result?.body ?? ""))}"`;
    }
    return appendResponseHeaders(result, headers);
}

function appendCorsHeaders(result, policy, context) {
    if (policy === null) {
        return result;
    }

    const origin = getRequestHeader(context, "origin");
    const allowed = allowedOrigin(policy, origin);
    if (allowed === undefined) {
        return result;
    }

    const headers = {
        ...(isPlainObject(result?.headers) ? result.headers : {}),
        "Access-Control-Allow-Origin": allowed,
    };

    if (!policy.allowAnyOrigin) {
        headers.Vary = mergeVary(headers.Vary, "Origin");
    }
    if (policy.credentials) {
        headers["Access-Control-Allow-Credentials"] = "true";
    }
    if (policy.exposedHeaders.length !== 0) {
        headers["Access-Control-Expose-Headers"] = policy.exposedHeaders.join(", ");
    }

    return Object.freeze({
        ...result,
        headers: Object.freeze(headers),
    });
}

function finishWithCors(result, policy, context) {
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return Promise.resolve(result).then((value) => appendCorsHeaders(value, policy, context));
    }

    return appendCorsHeaders(result, policy, context);
}

function requestedHeadersAllowed(policy, requestedHeaders) {
    if (typeof requestedHeaders !== "string" || requestedHeaders.length === 0) {
        return true;
    }

    const requested = requestedHeaders
        .split(",")
        .map((header) => header.trim().toLowerCase())
        .filter((header) => header.length !== 0);

    return requested.every((header) => policy.headers.includes(header));
}

function createCorsPreflightHandler(state) {
    return function corsPreflightHandler(context) {
        const origin = getRequestHeader(context, "origin");
        const allowed = allowedOrigin(state.policy, origin);
        const requestedMethod = getRequestHeader(context, "access-control-request-method")?.toUpperCase();
        const requestedHeaders = getRequestHeader(context, "access-control-request-headers");
        const methods = state.policy.methods.length === 0 ? Array.from(state.methods) : state.policy.methods;

        if (
            allowed === undefined ||
            !methods.includes(requestedMethod) ||
            !requestedHeadersAllowed(state.policy, requestedHeaders)
        ) {
            return Results.status(403);
        }

        const headers = {
            "Access-Control-Allow-Origin": allowed,
            "Access-Control-Allow-Methods": methods.join(", "),
        };

        if (!state.policy.allowAnyOrigin) {
            headers.Vary = "Origin, Access-Control-Request-Method, Access-Control-Request-Headers";
        }
        if (state.policy.credentials) {
            headers["Access-Control-Allow-Credentials"] = "true";
        }
        if (state.policy.headers.length !== 0) {
            headers["Access-Control-Allow-Headers"] = state.policy.headers.join(", ");
        }
        if (state.policy.maxAgeSeconds !== undefined) {
            headers["Access-Control-Max-Age"] = String(state.policy.maxAgeSeconds);
        }

        return Results.status(204, undefined, { headers });
    };
}




const EMPTY_HEADERS = Object.freeze({
    get() {
        return undefined;
    },
    entries() {
        return Object.freeze([]);
    },
});

function createDefaultRequest(routeInfo) {
    return Object.freeze({
        method: routeInfo.method,
        path: routeInfo.pattern,
        rawTarget: routeInfo.pattern,
        headers: EMPTY_HEADERS,
    });
}

function attachHostMarker(context, host) {
    Object.defineProperty(context, "__sloppyHost", {
        value: host,
    });
    return context;
}

function stringPolicyName(policy) {
    if (typeof policy !== "string" || policy.length === 0) {
        throw new TypeError("Sloppy ctx.authorize policy must be a non-empty string.");
    }
    return policy;
}

function attachAuthHelpers(context) {
    if (context.auth !== undefined) {
        return context;
    }
    const helpers = Object.freeze({
        get user() {
            return context.user;
        },
        requireUser() {
            if (context.user?.authenticated !== true) {
                throw new Error("SLOPPY_E_AUTH_MISSING_CREDENTIALS: authenticated user is required.");
            }
            return context.user;
        },
        hasScope(scope) {
            return context.user?.hasScope(scope) === true;
        },
        hasRole(role) {
            return context.user?.hasRole(role) === true;
        },
        hasClaim(name, value = undefined) {
            return context.user?.hasClaim(name, value) === true;
        },
        async authorize(policy, resource = undefined) {
            const name = stringPolicyName(policy);
            const denied = authorizePolicy(context.__sloppyHost?.auth, name, context.user, context, resource);
            const result = denied !== null && typeof denied === "object" && typeof denied.then === "function"
                ? await denied
                : denied;
            return result === undefined;
        },
    });
    Object.defineProperties(context, {
        auth: {
            value: helpers,
            enumerable: true,
            configurable: true,
        },
        claims: {
            get() {
                return context.user?.claims ?? {};
            },
            enumerable: true,
            configurable: true,
        },
        requireUser: {
            value: helpers.requireUser,
            enumerable: true,
            configurable: true,
        },
        hasScope: {
            value: helpers.hasScope,
            enumerable: true,
            configurable: true,
        },
        hasRole: {
            value: helpers.hasRole,
            enumerable: true,
            configurable: true,
        },
        hasClaim: {
            value: helpers.hasClaim,
            enumerable: true,
            configurable: true,
        },
        authorize: {
            value: helpers.authorize,
            enumerable: true,
            configurable: true,
        },
    });
    return context;
}

function createHandlerContext(host, routeInfo) {
    return attachAuthHelpers(attachHostMarker({
        services: host.services.createScope(),
        capabilities: host.capabilities,
        config: host.config,
        log: host.log,
        user: anonymousUser(),
        route: {},
        routeName: routeInfo.name ?? "",
        routePattern: routeInfo.pattern,
        urlFor: routeInfo.urlFor,
        request: createDefaultRequest(routeInfo),
    }, host));
}

function decorateProvidedContext(host, context, routeInfo) {
    const nextContext = {
        ...context,
    };

    nextContext.config ??= host.config;
    nextContext.log ??= host.log;
    nextContext.capabilities ??= host.capabilities;
    nextContext.user ??= anonymousUser();
    attachAuthHelpers(attachHostMarker(nextContext, host));

    if (nextContext.route === undefined || nextContext.route === null) {
        nextContext.route = {};
    }
    if (nextContext.routeName === undefined) {
        nextContext.routeName = routeInfo.name ?? "";
    }
    if (nextContext.routePattern === undefined) {
        nextContext.routePattern = routeInfo.pattern;
    }
    if (nextContext.urlFor === undefined) {
        nextContext.urlFor = routeInfo.urlFor;
    }
    if (nextContext.request === undefined || nextContext.request === null) {
        nextContext.request = createDefaultRequest(routeInfo);
    } else {
        nextContext.request = Object.freeze({
            ...nextContext.request,
            method: nextContext.request.method ?? routeInfo.method,
            path: nextContext.request.path ?? routeInfo.pattern,
            rawTarget: nextContext.request.rawTarget ?? nextContext.request.target ??
                nextContext.request.path ?? routeInfo.pattern,
            headers: nextContext.request.headers ?? EMPTY_HEADERS,
        });
    }

    return nextContext;
}

function createRouteHandler(host, handler, middleware = [], corsPolicy = null, routeInfo) {
    function invokeHandler(ctx) {
        const denied = authorizeRoute(ctx, routeInfo.auth, host.auth);
        if (denied !== null && typeof denied === "object" && typeof denied.then === "function") {
            return Promise.resolve(denied).then((resolved) => {
                if (resolved !== undefined) {
                    return resolved;
                }
                return handler(ctx);
            });
        }
        if (denied !== undefined) {
            return denied;
        }
        const output = routeInfo.outputCache === undefined
            ? handler(ctx)
            : invokeWithOutputCache(ctx, routeInfo, routeInfo.outputCache, () => handler(ctx));
        if (routeInfo.cacheHeaders === undefined) {
            return output;
        }
        return Promise.resolve(output).then((result) => applyCacheHeaders(result, routeInfo.cacheHeaders));
    }

    function routeHandler(context) {
        if (context !== undefined && context !== null) {
            const providedContext = decorateProvidedContext(host, context, routeInfo);
            try {
                const result = invokeMiddlewarePipeline(
                    providedContext,
                    middleware,
                    () => invokeHandler(providedContext),
                );
                if (result !== null && typeof result === "object" && typeof result.then === "function") {
                    return Promise.resolve(result).then(
                        (value) => finishRouteResult(value, corsPolicy, providedContext),
                        (error) => finishHandledRouteError(host, error, corsPolicy, providedContext),
                    );
                }
                return finishRouteResult(result, corsPolicy, providedContext);
            } catch (error) {
                return finishHandledRouteError(host, error, corsPolicy, providedContext);
            }
        }

        const ownedContext = createHandlerContext(host, routeInfo);
        try {
            const result = invokeMiddlewarePipeline(
                ownedContext,
                middleware,
                () => invokeHandler(ownedContext),
            );
            if (result !== null && typeof result === "object" && typeof result.then === "function") {
                return Promise.resolve(result).then(
                    (value) => finishWithCleanup(
                        finishRouteResult(value, corsPolicy, ownedContext),
                        () => ownedContext.services.dispose(),
                    ),
                    (error) => finishRouteError(
                        host,
                        error,
                        corsPolicy,
                        ownedContext,
                        () => ownedContext.services.dispose(),
                    ),
                );
            }
            return finishWithCleanup(
                finishRouteResult(result, corsPolicy, ownedContext),
                () => ownedContext.services.dispose(),
            );
        } catch (error) {
            return finishRouteError(
                host,
                error,
                corsPolicy,
                ownedContext,
                () => ownedContext.services.dispose(),
            );
        }
    }
    const websocketOptions = webSocketRouteOptions(handler);
    if (websocketOptions !== undefined) {
        Object.defineProperty(routeHandler, Symbol.for("sloppy.websocket.routeOptions"), {
            value: websocketOptions,
        });
    }
    return routeHandler;
}

function snapshotRoute(route) {
    return Object.freeze({
        method: route.method,
        kind: route.kind,
        pattern: route.pattern,
        handler: route.handler,
        name: route.name,
        params: Object.freeze(routeParamEntries(route.pattern)),
        metadata: snapshotMetadata(route.metadata),
    });
}

function routeSnapshotOrder(routes) {
    return routes.map((route, index) => Object.freeze({
        route,
        sourceOrder: index,
    })).sort(compareRouteSpecificity).map((entry) => entry.route);
}

function urlForRoute(routes, name, params = {}, query = undefined) {
    validateName(name, "route");
    const route = routes.find((current) => current.name === name);
    if (route === undefined) {
        throw new Error(`Sloppy route name '${name}' is not registered.`);
    }
    return buildRouteUrl(route.pattern, params, query);
}

function snapshotMetadata(metadata) {
    const snapshot = { ...metadata };

    if (Array.isArray(snapshot.tags)) {
        snapshot.tags = Object.freeze([...snapshot.tags]);
    }
    if (snapshot.cors !== undefined) {
        const { state, ...cors } = snapshot.cors;
        snapshot.cors = Object.freeze({
            ...cors,
            origins: Object.freeze([...(cors.origins ?? [])]),
            methods: Object.freeze([...(cors.methods ?? [])]),
            headers: Object.freeze([...(cors.headers ?? [])]),
            exposedHeaders: Object.freeze([...(cors.exposedHeaders ?? [])]),
        });
    }
    if (snapshot.auth !== undefined) {
        snapshot.auth = snapshotAuthRequirement(snapshot.auth);
    }
    if (Array.isArray(snapshot.responses)) {
        snapshot.responses = Object.freeze(snapshot.responses.map((response) => Object.freeze({ ...response })));
    }
    if (Array.isArray(snapshot.consumes)) {
        snapshot.consumes = Object.freeze([...snapshot.consumes]);
    }
    if (Array.isArray(snapshot.produces)) {
        snapshot.produces = Object.freeze([...snapshot.produces]);
    }
    if (Array.isArray(snapshot.headers)) {
        snapshot.headers = Object.freeze(snapshot.headers.map((header) => Object.freeze({ ...header })));
    }
    if (snapshot.realtime?.websocket !== undefined) {
        snapshot.realtime = Object.freeze({
            ...snapshot.realtime,
            websocket: Object.freeze({
                ...snapshot.realtime.websocket,
                protocols: Object.freeze([...(snapshot.realtime.websocket.protocols ?? [])]),
                origins: snapshot.realtime.websocket.origins === "*"
                    ? "*"
                    : snapshot.realtime.websocket.origins === undefined
                        ? undefined
                        : Object.freeze([...snapshot.realtime.websocket.origins]),
            }),
        });
    }

    return Object.freeze(snapshot);
}

function createEndpointBuilder(route, assertAppMutable) {
    function setName(name) {
        assertAppMutable();
        validateName(name, "endpoint");
        if (route.name !== name && route.routeSet.some((current) => current.name === name)) {
            throw new Error(`Sloppy route name '${name}' is already registered.`);
        }

        route.name = name;
        if (route.routeInfo !== undefined) {
            route.routeInfo.name = name;
        }
        return endpoint;
    }

    function addResponse(status, schemaOrResult = undefined, options = undefined) {
        assertAppMutable();
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy endpoint returns options must be a plain object.");
        }
        validateStatusCode(status);

        const response = {
            status,
            description: options?.description,
            contentType: options?.contentType ?? route.metadata.produces?.[0] ?? "application/json",
            schema: schemaOrResult === undefined ? undefined : schemaMetadata(schemaOrResult, "endpoint returns"),
        };
        if (response.description !== undefined) {
            validateMetadataText(response.description, "endpoint response description");
        }
        validateMediaType(response.contentType, "endpoint response content type");

        const responses = [...(route.metadata.responses ?? [])];
        const existing = responses.findIndex((current) => current.status === status);
        if (existing >= 0) {
            responses[existing] = Object.freeze(response);
        } else {
            responses.push(Object.freeze(response));
        }
        responses.sort((left, right) => left.status - right.status);
        route.metadata.responses = Object.freeze(responses);
        route.metadata.returns = Object.freeze(response);
        return endpoint;
    }

    function setAuthRequirement(requirement) {
        route.metadata.auth = requirement;
        if (route.routeInfo !== undefined) {
            route.routeInfo.auth = requirement;
        }
    }

    function mergeAuthRequirement(extra) {
        const current = route.metadata.auth?.required === true ? route.metadata.auth : { required: true };
        setAuthRequirement(Object.freeze({
            ...current,
            ...extra,
            schemes: Object.freeze([
                ...new Set([
                    ...((current.schemes === undefined) ? [] : current.schemes),
                    ...((extra.schemes === undefined) ? [] : extra.schemes),
                ]),
            ]),
            scopes: Object.freeze([
                ...new Set([
                    ...((current.scopes === undefined) ? [] : current.scopes),
                    ...((extra.scopes === undefined) ? [] : extra.scopes),
                ]),
            ]),
            roles: Object.freeze([
                ...new Set([
                    ...((current.roles === undefined) ? [] : current.roles),
                    ...((extra.roles === undefined) ? [] : extra.roles),
                ]),
            ]),
            claims: Object.freeze([
                ...new Set([
                    ...((current.claims === undefined) ? [] : current.claims),
                    ...((extra.claims === undefined) ? [] : extra.claims),
                ]),
            ]),
        }));
    }

    const endpoint = {
        withName(name) {
            return setName(name);
        },
        name(name) {
            return setName(name);
        },
        summary(text) {
            assertAppMutable();
            validateMetadataText(text, "endpoint summary");
            route.metadata.summary = text;
            return endpoint;
        },
        description(text) {
            assertAppMutable();
            validateMetadataText(text, "endpoint description");
            route.metadata.description = text;
            return endpoint;
        },
        tags(...tags) {
            assertAppMutable();
            for (const tag of tags) {
                validateTag(tag);
            }
            route.metadata.tags = Object.freeze([...new Set([...(route.metadata.tags ?? []), ...tags])]);
            return endpoint;
        },
        deprecated(reasonOrBool = true) {
            assertAppMutable();
            if (typeof reasonOrBool !== "boolean" && typeof reasonOrBool !== "string") {
                throw new TypeError("Sloppy endpoint deprecated metadata must be a boolean or reason string.");
            }
            route.metadata.deprecated = reasonOrBool === false
                ? false
                : Object.freeze({ value: true, reason: typeof reasonOrBool === "string" ? reasonOrBool : undefined });
            return endpoint;
        },
        requireAuth(options = undefined) {
            assertAppMutable();
            const requirement = normalizeAuthRequirement(options);
            setAuthRequirement(requirement);
            return endpoint;
        },
        requiresAuth(options = undefined) {
            return endpoint.requireAuth(options);
        },
        allowAnonymous() {
            assertAppMutable();
            setAuthRequirement(Object.freeze({ required: false, allowAnonymous: true }));
            return endpoint;
        },
        authorize(policy) {
            assertAppMutable();
            mergeAuthRequirement({ policy });
            return endpoint;
        },
        requiresScope(...scopes) {
            assertAppMutable();
            mergeAuthRequirement({ scopes: Object.freeze(scopes) });
            return endpoint;
        },
        requiresRole(...roles) {
            assertAppMutable();
            mergeAuthRequirement({ roles: Object.freeze(roles) });
            return endpoint;
        },
        security(options = undefined) {
            return endpoint.requireAuth(options);
        },
        requiresScope(scope) {
            assertAppMutable();
            validateMetadataText(scope, "authorization scope");
            const existing = route.metadata.auth ?? { required: true };
            const requirement = Object.freeze({
                ...existing,
                required: true,
                scopes: Object.freeze([...new Set([...(existing.scopes ?? []), scope])]),
            });
            route.metadata.auth = requirement;
            if (route.routeInfo !== undefined) {
                route.routeInfo.auth = requirement;
            }
            return endpoint;
        },
        allowedOrigins(origins) {
            assertAppMutable();
            if (route.kind !== "websocket") {
                throw new TypeError("Sloppy endpoint allowedOrigins is only supported on WebSocket routes.");
            }
            const current = route.metadata.realtime?.websocket ?? {};
            route.metadata.realtime = Object.freeze({
                ...(route.metadata.realtime ?? {}),
                kind: "websocket",
                websocket: normalizeWebSocketRouteOptions({
                    ...current,
                    origins,
                }),
            });
            return endpoint;
        },
        accepts(schema, options = undefined) {
            assertAppMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy endpoint accepts options must be a plain object.");
            }
            if (options?.description !== undefined) {
                validateMetadataText(options.description, "endpoint request description");
            }
            const contentType = options?.contentType ?? route.metadata.consumes?.[0] ?? "application/json";
            validateMediaType(contentType, "endpoint request content type");

            route.metadata.accepts = Object.freeze({
                contentType,
                required: options?.required !== false,
                description: options?.description,
                schema: schemaMetadata(schema, "endpoint accepts"),
            });
            return endpoint;
        },
        returns(statusOrSchema, schemaOrOptions = undefined, maybeOptions = undefined) {
            assertAppMutable();
            if (typeof statusOrSchema === "number") {
                return addResponse(statusOrSchema, schemaOrOptions, maybeOptions);
            }
            const options = schemaOrOptions;
            return addResponse(options?.status ?? 200, statusOrSchema, options);
        },
        produces(mediaType) {
            assertAppMutable();
            validateMediaType(mediaType, "endpoint produces media type");
            route.metadata.produces = Object.freeze([...new Set([...(route.metadata.produces ?? []), mediaType])]);
            return endpoint;
        },
        consumes(mediaType) {
            assertAppMutable();
            validateMediaType(mediaType, "endpoint consumes media type");
            route.metadata.consumes = Object.freeze([...new Set([...(route.metadata.consumes ?? []), mediaType])]);
            return endpoint;
        },
        header(name, schema, options = undefined) {
            assertAppMutable();
            validateHeaderToken(name, "endpoint header");
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy endpoint header options must be a plain object.");
            }
            if (options?.description !== undefined) {
                validateMetadataText(options.description, "endpoint header description");
            }
            route.metadata.headers = Object.freeze([
                ...(route.metadata.headers ?? []).filter((header) => header.name.toLowerCase() !== name.toLowerCase()),
                Object.freeze({
                    name,
                    schema: schemaMetadata(schema, "endpoint header"),
                    required: options?.required === true,
                    description: options?.description,
                }),
            ]);
            return endpoint;
        },
        query(schemaOrObject, options = undefined) {
            assertAppMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy endpoint query options must be a plain object.");
            }
            const schemaValue = isSchema(schemaOrObject) ? schemaOrObject : Schema.object(schemaOrObject);
            route.metadata.query = Object.freeze({
                schema: schemaMetadata(schemaValue, "endpoint query"),
                required: options?.required === true,
            });
            return endpoint;
        },
        params(schemaOrObject, options = undefined) {
            assertAppMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy endpoint params options must be a plain object.");
            }
            const schemaValue = isSchema(schemaOrObject) ? schemaOrObject : Schema.object(schemaOrObject);
            route.metadata.params = Object.freeze({
                schema: schemaMetadata(schemaValue, "endpoint params"),
                required: options?.required !== false,
            });
            return endpoint;
        },
        openapi(override) {
            assertAppMutable();
            if (!isPlainObject(override)) {
                throw new TypeError("Sloppy endpoint OpenAPI override must be a plain object.");
            }
            route.metadata.openapi = cloneFrozenJson(override, "endpoint OpenAPI override");
            return endpoint;
        },
        outputCache(options) {
            assertAppMutable();
            const normalized = normalizeOutputCacheOptions(options);
            route.metadata.outputCache = normalized;
            route.routeInfo.outputCache = normalized;
            return endpoint;
        },
        noOutputCache() {
            assertAppMutable();
            route.metadata.outputCache = undefined;
            route.routeInfo.outputCache = undefined;
            return endpoint;
        },
        cacheHeaders(options) {
            assertAppMutable();
            const normalized = normalizeCacheHeaderOptions(options);
            route.metadata.cacheHeaders = normalized;
            route.routeInfo.cacheHeaders = normalized;
            return endpoint;
        },
    };

    return Object.freeze(endpoint);
}

function normalizeGroupPrefix(prefix) {
    validateGroupPrefix(prefix);

    if (prefix === "/") {
        return "/";
    }

    return prefix.replace(/\/+$/u, "");
}

function composeRoutePattern(prefix, childPattern) {
    validateGroupChildPattern(childPattern);

    if (prefix === "/") {
        return childPattern.startsWith("/") ? childPattern : `/${childPattern}`;
    }

    if (childPattern === "/") {
        return prefix;
    }

    return childPattern.startsWith("/") ? `${prefix}${childPattern}` : `${prefix}/${childPattern}`;
}

function normalizeMapArguments(pattern, optionsOrHandler, maybeHandler) {
    if (typeof optionsOrHandler === "function" && maybeHandler === undefined) {
        return {
            pattern,
            metadata: undefined,
            handler: optionsOrHandler,
        };
    }

    return {
        pattern,
        metadata: validateMetadataOptions(optionsOrHandler),
        handler: maybeHandler,
    };
}

function mergeRouteMetadata(groupMetadata, routeMetadata) {
    if (routeMetadata?.tags !== undefined && !Array.isArray(routeMetadata.tags)) {
        throw new TypeError("Sloppy route metadata tags must be an array when provided.");
    }

    const tags = [
        ...groupMetadata.tags,
        ...((routeMetadata?.tags !== undefined) ? routeMetadata.tags : []),
    ];

    for (const tag of tags) {
        validateTag(tag);
    }

    return {
        ...routeMetadata,
        tags,
        groupName: groupMetadata.name,
        groupPrefix: groupMetadata.prefix,
        ...(routeMetadata?.auth !== undefined
            ? { auth: routeMetadata.auth }
            : groupMetadata.auth === undefined ? {} : { auth: groupMetadata.auth }),
    };
}

function createRouteMetadata(routeMetadata) {
    if (routeMetadata?.tags !== undefined) {
        if (!Array.isArray(routeMetadata.tags)) {
            throw new TypeError("Sloppy route metadata tags must be an array when provided.");
        }

        for (const tag of routeMetadata.tags) {
            validateTag(tag);
        }

        return {
            ...routeMetadata,
            tags: [...routeMetadata.tags],
        };
    }

    return routeMetadata ?? {};
}

function registerRoute(
    routes,
    host,
    assertAppMutable,
    currentModule,
    method,
    pattern,
    optionsOrHandler,
    maybeHandler,
    metadataBase,
    middleware = [],
    corsPolicy = null,
    kind = "http",
) {
    const args = normalizeMapArguments(pattern, optionsOrHandler, maybeHandler);

    assertAppMutable();
    validatePattern(args.pattern);
    validateHandler(args.handler);
    if (!ROUTE_KINDS.has(kind)) {
        throw new TypeError("Sloppy route kind is not supported by bootstrap registration.");
    }
    if (!ROUTE_METHODS.has(method)) {
        throw new TypeError("Sloppy route method is not supported by bootstrap registration.");
    }
    for (const current of middleware) {
        validateMiddlewareEntry(current);
    }

    if (routes.some((route) => route.method === method && route.pattern === args.pattern)) {
        throw new Error(`Sloppy route '${method} ${args.pattern}' is already registered.`);
    }
    if (args.metadata?.name !== undefined &&
        (typeof args.metadata.name !== "string" || args.metadata.name.length === 0))
    {
        throw new TypeError("Sloppy route name must be a non-empty string.");
    }
    if (args.metadata?.name !== undefined &&
        routes.some((route) => route.name === args.metadata.name))
    {
        throw new Error(`Sloppy route name '${args.metadata.name}' is already registered.`);
    }

    const orderedMiddleware = orderedMiddlewareFunctions(middleware);
    const realtimeMetadata = kind === "websocket"
        ? { kind, websocket: webSocketRouteOptions(args.handler) ?? normalizeWebSocketRouteOptions() }
        : { kind };
    let metadata = {
        ...(metadataBase ? mergeRouteMetadata(metadataBase, args.metadata) : createRouteMetadata(args.metadata)),
        ...((kind === "http") ? {} : { realtime: realtimeMetadata }),
        ...((currentModule !== null) ? { module: currentModule } : {}),
        middleware: middlewareMetadata(orderedMiddleware),
        ...((corsPolicy !== null) ? { cors: snapshotCorsPolicy(corsPolicy) } : {}),
    };
    const outputCache = metadata.outputCache === undefined ? undefined : normalizeOutputCacheOptions(metadata.outputCache);
    const cacheHeaders = metadata.cacheHeaders === undefined ? undefined : normalizeCacheHeaderOptions(metadata.cacheHeaders);
    metadata = {
        ...metadata,
        outputCache,
        cacheHeaders,
    };
    const routeInfo = {
        method,
        pattern: args.pattern,
        name: typeof args.metadata?.name === "string" ? args.metadata.name : null,
        auth: metadata.auth,
        outputCache,
        cacheHeaders,
        kind,
        urlFor(name, params = {}, query = undefined) {
            return urlForRoute(routes, name, params, query);
        },
    };
    const route = {
        method,
        kind,
        pattern: args.pattern,
        handler: createRouteHandler(
            host,
            args.handler,
            Object.freeze(orderedMiddleware),
            corsPolicy,
            routeInfo,
        ),
        name: routeInfo.name,
        routeInfo,
        routeSet: routes,
        metadata,
    };

    routes.push(route);
    if (corsPolicy !== null) {
        registerCorsPreflightRoute(
            routes,
            host,
            assertAppMutable,
            args.pattern,
            method,
            corsPolicy,
            Object.freeze(orderedMiddleware),
        );
    }
    return createEndpointBuilder(route, assertAppMutable);
}

function corsPoliciesEqual(a, b) {
    if (a === b) {
        return true;
    }
    if (a === null || b === null || typeof a !== "object" || typeof b !== "object") {
        return false;
    }
    if (
        a.allowAnyOrigin !== b.allowAnyOrigin ||
        a.credentials !== b.credentials ||
        a.maxAgeSeconds !== b.maxAgeSeconds
    ) {
        return false;
    }
    const arraysEqual = (left, right) => {
        if (!Array.isArray(left) || !Array.isArray(right) || left.length !== right.length) {
            return false;
        }
        for (let index = 0; index < left.length; index += 1) {
            if (left[index] !== right[index]) {
                return false;
            }
        }
        return true;
    };
    return (
        arraysEqual(a.origins, b.origins) &&
        arraysEqual(a.methods, b.methods) &&
        arraysEqual(a.headers, b.headers) &&
        arraysEqual(a.exposedHeaders, b.exposedHeaders)
    );
}

function registerCorsPreflightRoute(
    routes,
    host,
    assertAppMutable,
    pattern,
    method,
    corsPolicy,
    middleware,
) {
    const existing = routes.find((route) => route.method === "OPTIONS" &&
        route.pattern === pattern &&
        route.metadata?.cors?.preflight === true);

    if (existing !== undefined) {
        if (!corsPoliciesEqual(existing.metadata.cors.state.policy, corsPolicy)) {
            throw new Error(`Sloppy CORS preflight route '${pattern}' already has a different policy.`);
        }
        existing.metadata.cors.state.methods.add(method);
        existing.kind = "http";
        existing.handler = createRouteHandler(
            host,
            createCorsPreflightHandler(existing.metadata.cors.state),
            middleware,
            null,
            existing.routeInfo ?? { method: "OPTIONS", pattern, name: existing.name ?? null, kind: "http" },
        );
        existing.metadata.middleware = middlewareMetadata(middleware);
        return;
    }

    const state = {
        policy: corsPolicy,
        methods: new Set([method]),
    };
    const routeInfo = {
        method: "OPTIONS",
        pattern,
        name: null,
        kind: "http",
        urlFor(name, params = {}, query = undefined) {
            return urlForRoute(routes, name, params, query);
        },
    };
    routes.push({
        method: "OPTIONS",
        kind: "http",
        pattern,
        handler: createRouteHandler(
            host,
            createCorsPreflightHandler(state),
            middleware,
            null,
            routeInfo,
        ),
        name: null,
        routeInfo,
        routeSet: routes,
        metadata: {
            cors: {
                ...snapshotCorsPolicy(corsPolicy),
                preflight: true,
                state,
            },
            middleware: middlewareMetadata(middleware),
        },
    });
}

function controllerInjectionTokens(controller) {
    const tokens = controller.inject ?? controller.dependencies ?? [];

    if (!Array.isArray(tokens)) {
        throw new TypeError("Sloppy controller inject metadata must be an array when provided.");
    }

    for (const token of tokens) {
        validateServiceToken(token);
    }

    return Object.freeze([...tokens]);
}

function createControllerHandler(host, Controller, action, routeInfo) {
    const inject = controllerInjectionTokens(Controller);
    const prototypeMethod = Controller.prototype?.[action];

    if (typeof prototypeMethod !== "function") {
        throw new TypeError(`Sloppy controller action '${action}' must name a prototype method.`);
    }

    return function controllerHandler(context) {
        let ctx = context === undefined || context === null
            ? createHandlerContext(host, routeInfo)
            : decorateProvidedContext(host, context, routeInfo);
        let ownsServices = context === undefined || context === null;
        if (ctx.services === undefined || ctx.services === null) {
            const nextContext = {
                ...ctx,
                services: host.services.createScope(),
            };
            attachHostMarker(nextContext, host);
            ctx = Object.freeze(nextContext);
            ownsServices = true;
        }
        const services = ctx.services;
        try {
            const dependencies = inject.map((token) => services.get(token));
            const instance = new Controller(...dependencies);
            const result = instance[action](ctx);
            if (ownsServices) {
                return finishWithCleanup(result, () => services.dispose());
            }
            return result;
        } catch (error) {
            if (ownsServices) {
                return cleanupAfterFailure(error, () => services.dispose());
            }
            throw error;
        }
    };
}

function createControllerMapper(
    routes,
    host,
    assertAppMutable,
    currentModule,
    prefix,
    Controller,
    middleware = [],
    getCorsPolicy = () => null,
) {
    const normalizedPrefix = normalizeGroupPrefix(prefix);
    validateController(Controller);

    function map(method, pattern, action, options) {
        validateControllerAction(action);
        return registerRoute(
            routes,
            host,
            assertAppMutable,
            currentModule,
            method,
            composeRoutePattern(normalizedPrefix, pattern),
            {
                ...(options ?? {}),
                controller: Controller.name || "AnonymousController",
                action,
            },
            createControllerHandler(
                host,
                Controller,
                action,
                Object.freeze({ method, pattern: composeRoutePattern(normalizedPrefix, pattern) }),
            ),
            undefined,
            middleware,
            getCorsPolicy(),
        );
    }

    return Object.freeze({
        get(pattern, action, options) {
            return map("GET", pattern, action, options);
        },
        post(pattern, action, options) {
            return map("POST", pattern, action, options);
        },
        put(pattern, action, options) {
            return map("PUT", pattern, action, options);
        },
        patch(pattern, action, options) {
            return map("PATCH", pattern, action, options);
        },
        delete(pattern, action, options) {
            return map("DELETE", pattern, action, options);
        },
    });
}

function createRouteGroup(
    routes,
    host,
    assertAppMutable,
    getCurrentModule,
    prefix,
    getInheritedMiddleware = () => [],
    nextMiddlewareSequence = () => 0,
    getCorsPolicy = () => null,
) {
    const groupMetadata = {
        prefix: normalizeGroupPrefix(prefix),
        tags: [],
        name: null,
        auth: undefined,
    };
    const groupMiddleware = [];

    function createMapMethod(method, kind = "http") {
        return function mapRoute(pattern, optionsOrHandler, maybeHandler) {
            const fullPattern = composeRoutePattern(groupMetadata.prefix, pattern);
            const mappedOptionsOrHandler = kind === "sse" && typeof optionsOrHandler === "function"
                ? createSseRouteHandler(optionsOrHandler)
                : kind === "websocket" && typeof optionsOrHandler === "function"
                    ? createWebSocketRouteHandler(optionsOrHandler, maybeHandler)
                    : optionsOrHandler;
            const mappedMaybeHandler = kind === "sse" && typeof optionsOrHandler !== "function"
                ? createSseRouteHandler(maybeHandler)
                : kind === "websocket" && typeof optionsOrHandler !== "function"
                    ? createWebSocketRouteHandler(maybeHandler, optionsOrHandler)
                    : kind === "websocket"
                        ? undefined
                        : maybeHandler;
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                getCurrentModule(),
                method,
                fullPattern,
                mappedOptionsOrHandler,
                mappedMaybeHandler,
                {
                    prefix: groupMetadata.prefix,
                    tags: groupMetadata.tags,
                    name: groupMetadata.name,
                    auth: groupMetadata.auth,
                },
                [...getInheritedMiddleware(), ...groupMiddleware],
                getCorsPolicy(),
                kind,
            );
        };
    }

    const group = {
        get prefix() {
            return groupMetadata.prefix;
        },

        withTags(...tags) {
            assertAppMutable();

            for (const tag of tags) {
                validateTag(tag);
            }

            groupMetadata.tags.push(...tags);
            return group;
        },

        withName(name) {
            assertAppMutable();
            validateName(name, "route group");

            groupMetadata.name = name;
            return group;
        },

        use(middleware) {
            assertAppMutable();
            validateMiddleware(middleware);

            groupMiddleware.push({ fn: middleware, sequence: nextMiddlewareSequence() });
            return group;
        },

        requireAuth(options = undefined) {
            assertAppMutable();
            groupMetadata.auth = normalizeAuthRequirement(options);
            return group;
        },
        requiresAuth(options = undefined) {
            return group.requireAuth(options);
        },
        allowAnonymous() {
            assertAppMutable();
            groupMetadata.auth = Object.freeze({ required: false, allowAnonymous: true });
            return group;
        },

        mapGet: createMapMethod("GET"),
        mapPost: createMapMethod("POST"),
        mapPut: createMapMethod("PUT"),
        mapPatch: createMapMethod("PATCH"),
        mapDelete: createMapMethod("DELETE"),
        get: createMapMethod("GET"),
        post: createMapMethod("POST"),
        put: createMapMethod("PUT"),
        patch: createMapMethod("PATCH"),
        delete: createMapMethod("DELETE"),
        sse: createMapMethod("GET", "sse"),
        ws: createMapMethod("GET", "websocket"),
        websocket: createMapMethod("GET", "websocket"),
        group(childPrefix) {
            assertAppMutable();
            const child = createRouteGroup(
                routes,
                host,
                assertAppMutable,
                getCurrentModule,
                composeRoutePattern(groupMetadata.prefix, childPrefix),
                () => [...getInheritedMiddleware(), ...groupMiddleware],
                nextMiddlewareSequence,
                getCorsPolicy,
            );
            if (groupMetadata.auth !== undefined) {
                child.requireAuth(groupMetadata.auth);
            }
            return child;
        },
    };

    return Object.freeze(group);
}


function createRouterGroup(prefix, configure) {
    validateGroupPrefix(prefix);

    if (configure !== undefined && typeof configure !== "function") {
        throw new TypeError("Sloppy Router.group configure callback must be a function.");
    }

    function routerGroup(app) {
        const group = app.group(prefix);
        if (configure !== undefined) {
            configure(group);
        }
        return group;
    }

    defineFunctionModuleName(routerGroup, `router:${normalizeGroupPrefix(prefix)}`);
    return Object.freeze(routerGroup);
}

export {
    createControllerMapper,
    createRouteGroup,
    createRouterGroup,
    normalizeCorsPolicy,
    registerRoute,
    routeSnapshotOrder,
    snapshotRoute,
    urlForRoute,
};
