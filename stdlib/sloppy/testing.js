import { Text } from "./codec.js";

const SUPPORTED_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "HEAD"]);
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function assertHeaderName(name, subject) {
    if (typeof name !== "string" || !HEADER_TOKEN_PATTERN.test(name)) {
        throw new TypeError(`Sloppy test host ${subject} header names must be safe HTTP tokens.`);
    }
}

function assertHeaderValue(value, subject) {
    if (typeof value !== "string" || /[\x00-\x08\x0A-\x1F\x7F]/u.test(value)) {
        throw new TypeError(`Sloppy test host ${subject} header values must be safe strings.`);
    }
}

function copyBytes(value, subject) {
    if (value instanceof Uint8Array) {
        return new Uint8Array(value);
    }

    if (value instanceof ArrayBuffer) {
        return new Uint8Array(value.slice(0));
    }

    if (ArrayBuffer.isView(value)) {
        const storage = value["buf" + "fer"];
        return new Uint8Array(storage.slice(value.byteOffset, value.byteOffset + value.byteLength));
    }

    throw new TypeError(`Sloppy test host ${subject} must be a string or Uint8Array.`);
}

function headerEntriesFromObject(headers, subject) {
    if (headers === undefined) {
        return [];
    }

    if (!isPlainObject(headers)) {
        throw new TypeError(`Sloppy test host ${subject} headers must be a plain object.`);
    }

    const entries = [];
    for (const [name, value] of Object.entries(headers)) {
        assertHeaderName(name, subject);
        assertHeaderValue(value, subject);
        entries.push([name, value]);
    }
    return entries;
}

function normalizeHeaderEntries(entries) {
    const normalized = [];
    for (const [name, value] of entries) {
        const lower = name.toLowerCase();
        const existing = normalized.find((entry) => entry[0] === lower);
        if (existing === undefined) {
            normalized.push([lower, value]);
        } else {
            existing[1] = `${existing[1]}, ${value}`;
        }
    }
    return normalized;
}

function createHeadersLike(entries) {
    const normalized = Object.freeze(normalizeHeaderEntries(entries).map(([name, value]) => Object.freeze([name, value])));
    return Object.freeze({
        get(name) {
            if (typeof name !== "string") {
                throw new TypeError("Sloppy test host headers.get name must be a string.");
            }
            const lower = name.toLowerCase();
            return normalized.find((entry) => entry[0] === lower)?.[1];
        },
        entries() {
            return normalized[Symbol.iterator]();
        },
        [Symbol.iterator]() {
            return normalized[Symbol.iterator]();
        },
    });
}

function hasHeader(entries, name) {
    const lower = name.toLowerCase();
    return entries.some(([current]) => current.toLowerCase() === lower);
}

function setDefaultHeader(entries, name, value) {
    if (!hasHeader(entries, name)) {
        entries.push([name, value]);
    }
}

function bodySourceCount(options) {
    return ["body", "text", "json"].filter((key) => options?.[key] !== undefined).length;
}

function normalizeRequestBody(options, headerEntries) {
    if (options === undefined || options === null) {
        return new Uint8Array(0);
    }

    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy test host request options must be a plain object.");
    }

    const sources = bodySourceCount(options);
    if (sources > 1) {
        throw new TypeError("Sloppy test host request options must use one body source.");
    }
    if (sources === 0) {
        return new Uint8Array(0);
    }

    let bytes;
    if (options.json !== undefined) {
        let text;
        try {
            text = JSON.stringify(options.json);
        } catch (error) {
            throw new TypeError(`Sloppy test host JSON body could not be serialized: ${error.message}`);
        }
        if (text === undefined) {
            throw new TypeError("Sloppy test host JSON body must be JSON serializable.");
        }
        bytes = Text.utf8.encode(text);
        setDefaultHeader(headerEntries, "Content-Type", JSON_CONTENT_TYPE);
    } else if (options.text !== undefined) {
        bytes = Text.utf8.encode(String(options.text));
        setDefaultHeader(headerEntries, "Content-Type", TEXT_CONTENT_TYPE);
    } else if (typeof options.body === "string") {
        bytes = Text.utf8.encode(options.body);
    } else {
        bytes = copyBytes(options.body, "body");
    }

    if (bytes.byteLength !== 0) {
        setDefaultHeader(headerEntries, "Content-Length", String(bytes.byteLength));
    }
    return bytes;
}

function splitTarget(target) {
    if (typeof target !== "string" || target.length === 0 || !target.startsWith("/")) {
        throw new TypeError("Sloppy test host target must be a request target starting with '/'.");
    }
    if (target.includes("#")) {
        throw new TypeError("Sloppy test host target must not include a fragment.");
    }

    const queryIndex = target.indexOf("?");
    const rawPath = queryIndex < 0 ? target : target.slice(0, queryIndex);
    const queryString = queryIndex < 0 ? "" : target.slice(queryIndex + 1);
    if (rawPath.length === 0 || !rawPath.startsWith("/")) {
        throw new TypeError("Sloppy test host target path must start with '/'.");
    }
    const path = decodePercentComponent(rawPath, "path", false);
    return { path, queryString, rawTarget: target };
}

function decodePercentComponent(value, subject, plusAsSpace) {
    const encoded = plusAsSpace ? value.replace(/\+/gu, " ") : value;
    if (/%(?![0-9A-Fa-f]{2})/u.test(encoded)) {
        throw new TypeError(`Sloppy test host ${subject} percent escapes must use two hex digits.`);
    }
    try {
        return decodeURIComponent(encoded);
    } catch {
        throw new TypeError(`Sloppy test host ${subject} percent escapes must be valid UTF-8.`);
    }
}

function parseQuery(queryString) {
    const query = {};
    if (queryString.length === 0) {
        return Object.freeze(query);
    }

    for (const pair of queryString.split("&")) {
        const equals = pair.indexOf("=");
        const name = equals < 0 ? pair : pair.slice(0, equals);
        const value = equals < 0 ? "" : pair.slice(equals + 1);
        query[decodePercentComponent(name, "query", true)] = decodePercentComponent(value, "query", true);
    }
    return Object.freeze(query);
}

function splitRouteSegments(value) {
    if (value === "/") {
        return [];
    }
    if (!value.startsWith("/") || value.endsWith("/")) {
        return undefined;
    }
    return value.slice(1).split("/");
}

function parsePatternParam(segment) {
    if (!segment.startsWith("{") || !segment.endsWith("}") || segment.length < 3) {
        return undefined;
    }

    const inner = segment.slice(1, -1);
    const colon = inner.indexOf(":");
    const name = colon < 0 ? inner : inner.slice(0, colon);
    const type = colon < 0 ? "str" : inner.slice(colon + 1);
    if (!/^[A-Za-z_][0-9A-Za-z_]*$/u.test(name) || (type !== "str" && type !== "int")) {
        return undefined;
    }
    return { name, type };
}

function matchRoutePattern(pattern, path) {
    const patternSegments = splitRouteSegments(pattern);
    const pathSegments = splitRouteSegments(path);
    if (patternSegments === undefined || pathSegments === undefined || patternSegments.length !== pathSegments.length) {
        return undefined;
    }

    const route = {};
    for (let index = 0; index < patternSegments.length; index += 1) {
        const patternSegment = patternSegments[index];
        const pathSegment = pathSegments[index];
        const param = parsePatternParam(patternSegment);
        if (param === undefined) {
            if (patternSegment !== pathSegment) {
                return undefined;
            }
            continue;
        }
        if (pathSegment.length === 0 || (param.type === "int" && !/^[0-9]+$/u.test(pathSegment))) {
            return undefined;
        }
        route[param.name] = pathSegment;
    }
    return Object.freeze(route);
}

function mediaType(contentType) {
    return contentType.split(";", 1)[0].trim().toLowerCase();
}

function isJsonMediaType(contentType) {
    const type = mediaType(contentType);
    return type === "application/json" || (type.startsWith("application/") && type.endsWith("+json"));
}

function bodyKindForRequest(headers, bodyBytes) {
    if (bodyBytes.byteLength === 0) {
        return "none";
    }

    const contentType = headers.get("content-type");
    if (contentType === undefined) {
        return "unsupported";
    }

    if (isJsonMediaType(contentType)) {
        try {
            JSON.parse(Text.utf8.decode(bodyBytes));
        } catch {
            return "malformed-json";
        }
        return "json";
    }

    const type = mediaType(contentType);
    if (type === "text/plain") {
        return "text";
    }
    if (type === "application/octet-stream") {
        return "bytes";
    }
    return "unsupported";
}

function createBodyObject(kind, bytes) {
    let consumed = false;
    let textCache;
    const body = {
        get consumed() {
            return consumed;
        },
        bytes() {
            if (consumed) {
                throw new TypeError("Request body is already consumed.");
            }
            consumed = true;
            return new Uint8Array(bytes);
        },
        text() {
            if (consumed) {
                throw new TypeError("Request body is already consumed.");
            }
            consumed = true;
            textCache ??= Text.utf8.decode(bytes);
            return textCache;
        },
        json() {
            if (consumed) {
                throw new TypeError("Request body is already consumed.");
            }
            consumed = true;
            if (kind !== "json") {
                throw new TypeError("Request body is not available as JSON.");
            }
            textCache ??= Text.utf8.decode(bytes);
            return JSON.parse(textCache);
        },
    };
    return Object.freeze(body);
}

function createRequestObject(method, targetParts, headers, bodyKind, bodyBytes) {
    let textCache;
    const request = {
        method,
        path: targetParts.path,
        rawTarget: targetParts.rawTarget,
        headers,
        body: createBodyObject(bodyKind, bodyBytes),
        contentType: headers.get("content-type") ?? null,
        contentLength: bodyBytes.byteLength === 0 ? null : bodyBytes.byteLength,
        bytes() {
            return new Uint8Array(bodyBytes);
        },
        text() {
            textCache ??= Text.utf8.decode(bodyBytes);
            return textCache;
        },
        json() {
            if (bodyKind !== "json") {
                throw new TypeError("Request body is not available as JSON.");
            }
            textCache ??= Text.utf8.decode(bodyBytes);
            return JSON.parse(textCache);
        },
    };
    return Object.freeze(request);
}

function signalObject() {
    return Object.freeze({
        aborted: false,
        reason: null,
        throwIfAborted() {},
    });
}

function createContext(app, method, targetParts, headers, route, bodyKind, bodyBytes) {
    return Object.freeze({
        services: app.services.createScope(),
        capabilities: app.capabilities,
        config: app.config,
        log: app.log,
        route,
        query: parseQuery(targetParts.queryString),
        request: createRequestObject(method, targetParts, headers, bodyKind, bodyBytes),
        connection: Object.freeze({
            id: "test-host",
            protocol: "http",
            scheme: "test",
            secure: false,
        }),
        signal: signalObject(),
        deadline: null,
    });
}

function descriptorHeaders(result) {
    const entries = [];
    if (result?.headers !== undefined && result.headers !== null) {
        for (const [name, value] of Object.entries(result.headers)) {
            entries.push([name, value]);
        }
    }
    if (result?.location !== undefined) {
        entries.push(["Location", result.location]);
    }
    if (result?.contentType !== undefined) {
        entries.push(["Content-Type", result.contentType]);
    }
    return entries;
}

function responseFromParts(status, headers, bodyBytes) {
    const body = new Uint8Array(bodyBytes);
    return Object.freeze({
        status,
        headers: createHeadersLike(headers),
        bytes() {
            return new Uint8Array(body);
        },
        text() {
            return Text.utf8.decode(body);
        },
        json() {
            return JSON.parse(Text.utf8.decode(body));
        },
    });
}

function responseFromText(status, text, contentType = TEXT_CONTENT_TYPE) {
    return responseFromParts(status, [["Content-Type", contentType]], Text.utf8.encode(text));
}

function responseFromResult(result) {
    if (typeof result === "string") {
        return responseFromText(200, result);
    }

    if (result === undefined || result === null || result.__sloppyResult !== true) {
        throw new TypeError("Sloppy test host route handlers must return a string or Results.* descriptor.");
    }

    const headers = descriptorHeaders(result);
    if (result.kind === "empty") {
        return responseFromParts(result.status, headers, new Uint8Array(0));
    }
    if (result.kind === "text" || result.kind === "html") {
        return responseFromParts(result.status, headers, Text.utf8.encode(String(result.body)));
    }
    if (result.kind === "bytes") {
        return responseFromParts(result.status, headers, copyBytes(result.body, "response body"));
    }
    if (result.kind === "json" || result.kind === "problem") {
        const body = result.body === undefined ? null : result.body;
        return responseFromParts(result.status, headers, Text.utf8.encode(JSON.stringify(body)));
    }

    throw new TypeError(`Sloppy test host does not support result kind '${result.kind}'.`);
}

function findRoute(routes, method, path) {
    let methodMismatch = false;
    for (const route of routes) {
        const params = matchRoutePattern(route.pattern, path);
        if (params === undefined) {
            continue;
        }
        if (route.method !== method) {
            methodMismatch = true;
            continue;
        }
        return { route, params, methodMismatch: false };
    }
    return { route: undefined, params: undefined, methodMismatch };
}

function routeHasParams(route) {
    return route.pattern.includes("{");
}

function snapshotRoutes(app) {
    return Object.freeze(app.__getRoutes()
        .map((route, index) => Object.freeze({ ...route, __sourceOrder: index }))
        .sort((left, right) => {
            if (routeHasParams(left) !== routeHasParams(right)) {
                return routeHasParams(left) ? 1 : -1;
            }
            return left.__sourceOrder - right.__sourceOrder;
        }));
}

function normalizeMethod(method) {
    if (typeof method !== "string" || method.trim().length === 0) {
        throw new TypeError("Sloppy test host method must be a non-empty string.");
    }
    const normalized = method.toUpperCase();
    if (!SUPPORTED_METHODS.has(normalized)) {
        throw new TypeError("Sloppy test host method is not supported.");
    }
    return normalized;
}

function normalizeOptions(options) {
    if (options === undefined) {
        return {};
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy test host request options must be a plain object.");
    }
    return options;
}

function createTestHost(app) {
    if (app === null || typeof app !== "object" || typeof app.__getRoutes !== "function") {
        throw new TypeError("Sloppy createTestHost expects a Sloppy app.");
    }

    app.freeze();
    const routes = snapshotRoutes(app);
    let closed = false;
    let activeRequests = 0;
    let closePromise = undefined;
    let drainWaiters = [];

    function finishRequest() {
        activeRequests -= 1;
        if (closed && activeRequests === 0) {
            const waiters = drainWaiters;
            drainWaiters = [];
            for (const resolve of waiters) {
                resolve();
            }
        }
    }

    function waitForDrain() {
        if (activeRequests === 0) {
            return Promise.resolve();
        }
        return new Promise((resolve) => {
            drainWaiters.push(resolve);
        });
    }

    async function request(method, target, options = undefined) {
        if (closed) {
            throw new Error("Sloppy test host is closed.");
        }
        activeRequests += 1;
        try {
            const normalizedMethod = normalizeMethod(method);
            const normalizedOptions = normalizeOptions(options);
            const targetParts = splitTarget(target);
            const headerEntries = headerEntriesFromObject(normalizedOptions.headers, "request");
            const bodyBytes = normalizeRequestBody(normalizedOptions, headerEntries);
            const headers = createHeadersLike(headerEntries);
            const match = findRoute(routes, normalizedMethod, targetParts.path);

            if (match.route === undefined) {
                return match.methodMismatch
                    ? responseFromText(405, "Method Not Allowed\n")
                    : responseFromText(404, "Not Found\n");
            }

            const bodyKind = bodyKindForRequest(headers, bodyBytes);
            if (bodyKind === "malformed-json") {
                return responseFromText(400, "Malformed JSON\n");
            }
            if (bodyKind === "unsupported" && bodyBytes.byteLength !== 0) {
                return responseFromText(415, "Unsupported Media Type\n");
            }

            const context = createContext(app, normalizedMethod, targetParts, headers, match.params, bodyKind, bodyBytes);
            try {
                return responseFromResult(await match.route.handler(context));
            } finally {
                await context.services.dispose();
            }
        } finally {
            finishRequest();
        }
    }

    const host = {
        request,
        get(target, options) {
            return request("GET", target, options);
        },
        post(target, options) {
            return request("POST", target, options);
        },
        put(target, options) {
            return request("PUT", target, options);
        },
        patch(target, options) {
            return request("PATCH", target, options);
        },
        delete(target, options) {
            return request("DELETE", target, options);
        },
        options(target, options) {
            return request("OPTIONS", target, options);
        },
        async close() {
            if (closePromise !== undefined) {
                return closePromise;
            }
            closed = true;
            closePromise = (async () => {
                await waitForDrain();
                return app.services.dispose();
            })();
            return closePromise;
        },
    };
    return Object.freeze(host);
}

const Testing = Object.freeze({
    createHost: createTestHost,
});

export { createTestHost, Testing };
