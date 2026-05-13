import { Base64Url, Text } from "./codec.js";
import { Cache, isCache } from "./cache.js";
import { Hmac, Random, Secret } from "./crypto.js";
import { data, Migrations } from "./data.js";
import { Directory, File } from "./fs.js";
import { HttpClient, TcpListener } from "./net.js";
import { Process as SloppyProcess, System as SloppySystem } from "./os.js";
import { RAW_JSON_BODY, serializeJson } from "./results.js";
import { Schema, validationProblem } from "./schema.js";

const SUPPORTED_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "HEAD"]);
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";
const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const TESTHOST_BINARY_BODY = Symbol("sloppyTestHostBinaryBody");
const TESTHOST_TEXT_BODY = Symbol("sloppyTestHostTextBody");
const SECRET_REDACTION = "[REDACTED]";
const SENSITIVE_KEY_PATTERN = /(password|passwd|pwd|secret|token|authorization|cookie|set-cookie|apikey|clientsecret|privatekey|passphrase|connectionstring)/iu;
const DEFAULT_SERIALIZATION_OPTIONS = Object.freeze({
    json: undefined,
    contentNegotiation: Object.freeze({
        strictAccept: false,
    }),
});
const ROUTE_PARAM_PATTERN = /^\{([A-Za-z_][0-9A-Za-z_]*)(?::(str|int|uuid|alpha|float))?\}$/u;
const WEBSOCKET_ROUTE_OPTIONS = Symbol.for("sloppy.websocket.routeOptions");

function nowMs() {
    if (globalThis.performance !== undefined && typeof globalThis.performance.now === "function") {
        return globalThis.performance.now();
    }
    return Date.now();
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function normalizeOverrideMap(value, subject) {
    if (value === undefined) {
        return Object.freeze({});
    }
    if (!isPlainObject(value)) {
        throw new TypeError(`Sloppy TestHost ${subject} overrides must be a plain object.`);
    }
    return Object.freeze({ ...value });
}

function redactConfiguredSecrets(value, secretTexts) {
    let text = String(value);
    for (const secret of secretTexts) {
        text = text.replaceAll(secret, SECRET_REDACTION);
    }
    return text;
}

function redactedValue(key, value, secretTexts = []) {
    if (SENSITIVE_KEY_PATTERN.test(String(key))) {
        return SECRET_REDACTION;
    }
    if (value === null || value === undefined) {
        return value;
    }
    if (typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
        return redactConfiguredSecrets(value, secretTexts);
    }
    if (Array.isArray(value)) {
        return Object.freeze(value.map((entry) => redactedValue(key, entry, secretTexts)));
    }
    if (isPlainObject(value)) {
        const copied = {};
        for (const [entryKey, entryValue] of Object.entries(value)) {
            copied[entryKey] = redactedValue(entryKey, entryValue, secretTexts);
        }
        return Object.freeze(copied);
    }
    return redactConfiguredSecrets(value, secretTexts);
}

function createDiagnosticsStore(secrets = []) {
    const records = [];
    const secretTexts = secrets
        .filter((value) => typeof value === "string" && value.length > 0)
        .map((value) => String(value));

    function sanitizeRecord(record) {
        const fields = {};
        for (const [key, value] of Object.entries(record.fields ?? {})) {
            fields[key] = redactedValue(key, value, secretTexts);
        }
        return Object.freeze({
            code: String(record.code),
            subsystem: record.subsystem ?? "testhost",
            severity: record.severity ?? "info",
            message: redactConfiguredSecrets(record.message ?? record.code, secretTexts),
            fields: Object.freeze(fields),
        });
    }

    const diagnostics = {
        record(record) {
            records.push(sanitizeRecord(record));
        },
        snapshot() {
            return Object.freeze([...records]);
        },
        latest() {
            return records.at(-1);
        },
        filter(criteria = {}) {
            if (!isPlainObject(criteria)) {
                throw new TypeError("Sloppy TestHost diagnostics filter criteria must be a plain object.");
            }
            return Object.freeze(records.filter((record) => {
                if (criteria.code !== undefined && record.code !== criteria.code) {
                    return false;
                }
                if (criteria.subsystem !== undefined && record.subsystem !== criteria.subsystem) {
                    return false;
                }
                if (criteria.severity !== undefined && record.severity !== criteria.severity) {
                    return false;
                }
                return true;
            }));
        },
        expectCode(code) {
            if (!records.some((record) => record.code === code)) {
                throw new Error(`Sloppy TestHost expected diagnostic code '${code}'.`);
            }
            return diagnostics;
        },
        expectNoSecretLeaks() {
            const text = serializeJson(records);
            for (const secret of secretTexts) {
                if (text.includes(secret)) {
                    throw new Error("Sloppy TestHost diagnostics leaked a configured secret value.");
                }
            }
            return diagnostics;
        },
    };
    return Object.freeze(diagnostics);
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
        if (lower === "set-cookie") {
            normalized.push([lower, value]);
            continue;
        }
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

function headersToEntries(headers) {
    if (headers === undefined || headers === null) {
        return [];
    }
    if (typeof headers.entries === "function") {
        return Array.from(headers.entries());
    }
    if (isPlainObject(headers)) {
        return Object.entries(headers);
    }
    throw new TypeError("Sloppy test host headers must be response headers or a plain object.");
}

function responseHeaderEntries(response, omitEntityHeaders = false) {
    const entries = [];
    for (const [name, value] of response.headers.entries()) {
        const lower = name.toLowerCase();
        if (omitEntityHeaders && (lower === "content-type" || lower === "content-length")) {
            continue;
        }
        entries.push([name, value]);
    }
    return entries;
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
    return ["body", "text", "json"].filter((key) => options?.[key] !== undefined).length +
        (options?.[TESTHOST_TEXT_BODY] === undefined ? 0 : 1);
}

function normalizeRequestBodyWithOptions(options, headerEntries, serializationOptions) {
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
            text = serializeJson(options.json, serializationOptions.json);
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
    } else if (options[TESTHOST_TEXT_BODY] !== undefined) {
        bytes = Text.utf8.encode(String(options[TESTHOST_TEXT_BODY]));
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

function normalizeRequestBody(options, headerEntries) {
    return normalizeRequestBodyWithOptions(options, headerEntries, DEFAULT_SERIALIZATION_OPTIONS);
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
    const match = ROUTE_PARAM_PATTERN.exec(segment);
    if (match === null) {
        return undefined;
    }
    return { name: match[1], type: match[2] ?? "str" };
}

function routeSegmentMatchesParam(param, value) {
    if (value.length === 0) {
        return false;
    }
    if (param.type === "int") {
        return /^[0-9]+$/u.test(value);
    }
    if (param.type === "uuid") {
        return /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/u.test(value);
    }
    if (param.type === "alpha") {
        return /^[A-Za-z]+$/u.test(value);
    }
    if (param.type === "float") {
        return /^[0-9]*\.[0-9]+$|^[0-9]+\.[0-9]*$/u.test(value);
    }
    return param.type === "str";
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
        if (!routeSegmentMatchesParam(param, pathSegment)) {
            return undefined;
        }
        route[param.name] = pathSegment;
    }
    return Object.freeze(route);
}

function mediaType(contentType) {
    return contentType.split(";", 1)[0].trim().toLowerCase();
}

function contentTypeParameter(contentType, name) {
    const target = name.toLowerCase();
    for (const part of contentType.split(";").slice(1)) {
        const equals = part.indexOf("=");
        if (equals < 0) {
            continue;
        }
        const key = part.slice(0, equals).trim().toLowerCase();
        let value = part.slice(equals + 1).trim();
        if (value.length >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
            value = value.slice(1, -1);
        }
        if (key === target) {
            return value;
        }
    }
    return undefined;
}

function isJsonMediaType(contentType) {
    const type = mediaType(contentType);
    return type === "application/json" || (type.startsWith("application/") && type.endsWith("+json"));
}

function acceptsMediaType(accept, contentType) {
    if (accept === undefined || accept.trim().length === 0) {
        return true;
    }
    const responseType = mediaType(contentType);
    const slash = responseType.indexOf("/");
    const responseTop = slash < 0 ? responseType : responseType.slice(0, slash);

    for (const rawPart of accept.split(",")) {
        const part = rawPart.trim();
        if (part.length === 0) {
            continue;
        }
        const [range, ...parameters] = part.split(";").map((value) => value.trim().toLowerCase());
        const qParameter = parameters.find((parameter) => parameter.startsWith("q="));
        if (qParameter !== undefined && Number(qParameter.slice(2)) === 0) {
            continue;
        }
        if (range === "*/*" || range === responseType) {
            return true;
        }
        if (range.endsWith("/*") && range.slice(0, -2) === responseTop) {
            return true;
        }
    }
    return false;
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
    if (type === "application/x-www-form-urlencoded") {
        return "form";
    }
    if (type === "multipart/form-data") {
        const boundary = contentTypeParameter(contentType, "boundary");
        return typeof boundary === "string" && boundary.length > 0
            ? "multipart"
            : "malformed-multipart";
    }
    return "unsupported";
}

function parseCookieHeader(value) {
    const cookies = new Map();
    if (value === undefined) {
        return cookies;
    }
    for (const pair of value.split(";")) {
        const equals = pair.indexOf("=");
        if (equals <= 0) {
            continue;
        }
        const name = pair.slice(0, equals).trim();
        let rawValue = pair.slice(equals + 1).trim();
        if (!HEADER_TOKEN_PATTERN.test(name)) {
            continue;
        }
        if (rawValue.length >= 2 && rawValue.startsWith("\"") && rawValue.endsWith("\"")) {
            rawValue = rawValue.slice(1, -1);
        }
        cookies.set(name, decodePercentComponent(rawValue, "cookie", false));
    }
    return cookies;
}

function createCookiesLike(headers) {
    const cookies = parseCookieHeader(headers.get("cookie"));
    return Object.freeze({
        get(name) {
            if (typeof name !== "string") {
                throw new TypeError("Sloppy test host cookies.get name must be a string.");
            }
            return cookies.get(name) ?? null;
        },
    });
}

function createConfigOverlay(baseConfig, overrides, secrets) {
    const configOverrides = normalizeOverrideMap(overrides, "config");
    const secretOverrides = normalizeOverrideMap(secrets, "secret");
    const merged = Object.freeze({
        ...configOverrides,
        ...secretOverrides,
    });

    function hasOverride(key) {
        return Object.prototype.hasOwnProperty.call(merged, key);
    }

    function valueOrBase(key, fallback, required) {
        if (hasOverride(key)) {
            return merged[key];
        }
        if (fallback !== undefined) {
            return baseConfig.get(key, fallback);
        }
        return required ? baseConfig.require(key) : baseConfig.get(key);
    }

    return Object.freeze({
        get(key, fallback) {
            return valueOrBase(key, fallback, false);
        },
        has(key) {
            return hasOverride(key) || baseConfig.has(key);
        },
        require(key) {
            return valueOrBase(key, undefined, true);
        },
        getString(key, fallback) {
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (typeof value !== "string") {
                throw new TypeError(`Sloppy config key '${key}' must be a string.`);
            }
            return value;
        },
        getInt(key, fallback) {
            const value = Number(valueOrBase(key, fallback, fallback === undefined));
            if (!Number.isInteger(value)) {
                throw new TypeError(`Sloppy config key '${key}' must be an integer.`);
            }
            return value;
        },
        getNumber(key, fallback) {
            const value = Number(valueOrBase(key, fallback, fallback === undefined));
            if (!Number.isFinite(value)) {
                throw new TypeError(`Sloppy config key '${key}' must be a number.`);
            }
            return value;
        },
        getBool(key, fallback) {
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (typeof value === "boolean") {
                return value;
            }
            if (typeof value === "string" && /^(true|false)$/iu.test(value)) {
                return value.toLowerCase() === "true";
            }
            throw new TypeError(`Sloppy config key '${key}' must be a boolean.`);
        },
        getBoolean(key, fallback) {
            return this.getBool(key, fallback);
        },
        getDuration(key, fallback) {
            if (!hasOverride(key)) {
                return baseConfig.getDuration(key, fallback);
            }
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (typeof value === "number" && Number.isFinite(value) && value >= 0) {
                return value;
            }
            if (typeof value === "string") {
                const match = value.trim().match(/^(\d+(?:\.\d+)?)\s*(ms|s|m|h)$/iu);
                if (match !== null) {
                    const factors = { ms: 1, s: 1000, m: 60 * 1000, h: 60 * 60 * 1000 };
                    return Number(match[1]) * factors[match[2].toLowerCase()];
                }
            }
            throw new TypeError(`Sloppy config key '${key}' must be a duration in ms, s, m, or h.`);
        },
        getBytes(key, fallback) {
            if (!hasOverride(key)) {
                return baseConfig.getBytes(key, fallback);
            }
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (typeof value === "number" && Number.isInteger(value) && value >= 0) {
                return value;
            }
            if (typeof value === "string") {
                const match = value.trim().match(/^(\d+)\s*(b|kb|mb|gb|kib|mib|gib)$/iu);
                if (match !== null) {
                    const factors = {
                        b: 1,
                        kb: 1000,
                        mb: 1000 * 1000,
                        gb: 1000 * 1000 * 1000,
                        kib: 1024,
                        mib: 1024 * 1024,
                        gib: 1024 * 1024 * 1024,
                    };
                    return Number(match[1]) * factors[match[2].toLowerCase()];
                }
            }
            throw new TypeError(`Sloppy config key '${key}' must be a byte size.`);
        },
        getArray(key, fallback) {
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (!Array.isArray(value)) {
                throw new TypeError(`Sloppy config key '${key}' must be an array.`);
            }
            return Object.freeze([...value]);
        },
        getObject(key, fallback) {
            const value = valueOrBase(key, fallback, fallback === undefined);
            if (!isPlainObject(value)) {
                throw new TypeError(`Sloppy config key '${key}' must be an object.`);
            }
            return Object.freeze({ ...value });
        },
        getSecret(key) {
            const value = valueOrBase(key, undefined, true);
            return Object.freeze({
                value() {
                    return String(value);
                },
                toString() {
                    return "[Secret redacted]";
                },
                toJSON() {
                    return "[Secret redacted]";
                },
            });
        },
        bind(key, schema) {
            if (Object.keys(merged).some((entry) => entry === key || entry.startsWith(`${key}:`))) {
                const object = {};
                const prefix = `${key}:`;
                for (const [entryKey, entryValue] of Object.entries(merged)) {
                    if (entryKey.startsWith(prefix)) {
                        object[entryKey.slice(prefix.length)] = entryValue;
                    }
                }
                return Object.freeze({
                    ...baseConfig.bind(key, schema),
                    ...object,
                });
            }
            return baseConfig.bind(key, schema);
        },
    });
}

function disposeOverrideValues(values) {
    const pending = [];
    for (const value of values) {
        if (value === null || value === undefined) {
            continue;
        }
        const cleanup = value[Symbol.asyncDispose] ?? value[Symbol.dispose] ?? value.dispose ?? value.close;
        if (typeof cleanup === "function") {
            pending.push(Promise.resolve(cleanup.call(value)));
        }
    }
    return Promise.all(pending).then(() => undefined);
}

function createServiceOverlay(baseServices, serviceOverrides, providerOverrides, cacheOverrides) {
    const serviceMap = normalizeOverrideMap(serviceOverrides, "service");
    const providerMap = normalizeOverrideMap(providerOverrides, "provider");
    const cacheMap = normalizeOverrideMap(cacheOverrides, "cache");
    const merged = new Map(Object.entries(serviceMap));
    for (const [name, provider] of Object.entries(providerMap)) {
        if (provider === null || typeof provider !== "object") {
            throw new TypeError(`Sloppy TestHost provider override '${name}' must be an object.`);
        }
        merged.set(name, provider);
        merged.set(`data.${name}`, provider);
    }
    for (const [name, cache] of Object.entries(cacheMap)) {
        if (!isCache(cache)) {
            throw new TypeError(`Sloppy TestHost cache override '${name}' must be a Cache instance.`);
        }
        merged.set(Cache.token(name), cache);
    }

    function wrapScope(scope) {
        return Object.freeze({
            capabilities: scope.capabilities,
            get(token) {
                if (merged.has(token)) {
                    return merged.get(token);
                }
                return scope.get(token);
            },
            tryGet(token) {
                if (merged.has(token)) {
                    return merged.get(token);
                }
                return scope.tryGet(token);
            },
            dispose() {
                return scope.dispose();
            },
        });
    }

    return Object.freeze({
        get(token) {
            if (merged.has(token)) {
                return merged.get(token);
            }
            return baseServices.get(token);
        },
        tryGet(token) {
            if (merged.has(token)) {
                return merged.get(token);
            }
            return baseServices.tryGet(token);
        },
        createScope() {
            return wrapScope(baseServices.createScope());
        },
        dispose() {
            return disposeOverrideValues(merged.values());
        },
    });
}

function arrayPairsLookup(pairs, name) {
    for (let index = pairs.length - 1; index >= 0; index -= 1) {
        if (pairs[index][0] === name) {
            return pairs[index][1];
        }
    }
    return null;
}

function createFileLike(fieldName, name, contentType, bytes) {
    const fileBytes = new Uint8Array(bytes);
    return Object.freeze({
        fieldName,
        name,
        contentType,
        size: fileBytes.byteLength,
        bytes() {
            return new Uint8Array(fileBytes);
        },
        text() {
            return Text.utf8.decode(fileBytes);
        },
        async saveTo(path) {
            await File.writeBytes(path, fileBytes);
        },
    });
}

function createFormLike(fields, files = []) {
    const frozenFields = Object.freeze(fields.map(([name, value]) => Object.freeze([name, value])));
    const frozenFiles = Object.freeze(files.map(([name, file]) => Object.freeze([name, file])));
    return Object.freeze({
        get(name) {
            return arrayPairsLookup(frozenFields, name);
        },
        entries() {
            return frozenFields[Symbol.iterator]();
        },
        file(name) {
            return arrayPairsLookup(frozenFiles, name);
        },
    });
}

function parseFormUrlEncoded(bytes) {
    const fields = [];
    const text = Text.utf8.decode(bytes);
    if (text.length === 0) {
        return createFormLike(fields);
    }
    for (const pair of text.split("&")) {
        const equals = pair.indexOf("=");
        const name = equals < 0 ? pair : pair.slice(0, equals);
        const value = equals < 0 ? "" : pair.slice(equals + 1);
        fields.push([
            decodePercentComponent(name, "form field", true),
            decodePercentComponent(value, "form value", true),
        ]);
    }
    return createFormLike(fields);
}

function parsePartHeaders(text) {
    const headers = new Map();
    for (const line of text.split("\r\n")) {
        const colon = line.indexOf(":");
        if (colon <= 0) {
            continue;
        }
        headers.set(line.slice(0, colon).trim().toLowerCase(), line.slice(colon + 1).trim());
    }
    return headers;
}

function parseContentDisposition(value) {
    const output = {};
    for (const part of value.split(";").slice(1)) {
        const equals = part.indexOf("=");
        if (equals < 0) {
            continue;
        }
        const key = part.slice(0, equals).trim().toLowerCase();
        let fieldValue = part.slice(equals + 1).trim();
        if (fieldValue.length >= 2 && fieldValue.startsWith("\"") && fieldValue.endsWith("\"")) {
            fieldValue = fieldValue.slice(1, -1);
        }
        output[key] = fieldValue;
    }
    return output;
}

function parseMultipart(bytes, contentType) {
    const boundary = contentTypeParameter(contentType, "boundary");
    if (boundary === undefined || boundary.length === 0) {
        throw new TypeError("Sloppy test host multipart boundary is required.");
    }
    // Test-host multipart parsing round-trips file parts through UTF-8 text.
    // Do not use it to assert arbitrary binary upload byte fidelity.
    const text = Text.utf8.decode(bytes);
    const fields = [];
    const files = [];
    for (const rawPart of text.split(`--${boundary}`)) {
        if (rawPart === "" || rawPart === "--\r\n" || rawPart === "--") {
            continue;
        }
        const part = rawPart.startsWith("\r\n") ? rawPart.slice(2) : rawPart;
        const headerEnd = part.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            throw new TypeError("Sloppy test host multipart part is malformed.");
        }
        const headers = parsePartHeaders(part.slice(0, headerEnd));
        let body = part.slice(headerEnd + 4);
        if (body.endsWith("\r\n")) {
            body = body.slice(0, -2);
        }
        const disposition = parseContentDisposition(headers.get("content-disposition") ?? "");
        if (disposition.name === undefined) {
            continue;
        }
        if (disposition.filename !== undefined) {
            files.push([
                disposition.name,
                createFileLike(disposition.name, disposition.filename, headers.get("content-type") ?? "application/octet-stream", Text.utf8.encode(body)),
            ]);
        } else {
            fields.push([disposition.name, body]);
        }
    }
    return createFormLike(fields, files);
}

function createBodyObject(kind, bytes) {
    let consumed = false;
    let textCache;
    function readJson() {
        if (consumed) {
            throw new TypeError("Request body is already consumed.");
        }
        consumed = true;
        if (kind !== "json") {
            throw new TypeError("Request body is not available as JSON.");
        }
        textCache ??= Text.utf8.decode(bytes);
        return JSON.parse(textCache);
    }

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
            return readJson();
        },
        async validate(schema) {
            return Schema.validate(readJson(), schema);
        },
    };
    return Object.freeze(body);
}

function createRequestObject(method, targetParts, headers, bodyKind, bodyBytes) {
    let textCache;
    let formCache;
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
        form() {
            if (bodyKind !== "form") {
                throw new TypeError("Request body is not available as form data.");
            }
            formCache ??= parseFormUrlEncoded(bodyBytes);
            return formCache;
        },
        multipart() {
            if (bodyKind !== "multipart") {
                throw new TypeError("Request body is not available as multipart data.");
            }
            formCache ??= parseMultipart(bodyBytes, headers.get("content-type") ?? "");
            return formCache;
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

function createContext(app, hostState, method, targetParts, headers, route, matchedRoute, bodyKind, bodyBytes, options = undefined) {
    const request = createRequestObject(method, targetParts, headers, bodyKind, bodyBytes);
    const context = {
        services: hostState.services.createScope(),
        capabilities: app.capabilities,
        config: hostState.config,
        log: app.log,
        metrics: typeof app.__getMetricsRegistry === "function" ? app.__getMetricsRegistry() : undefined,
        diagnostics: hostState.diagnostics,
        user: options?.user,
        requireUser() {
            if (this.user?.authenticated !== true) {
                throw new Error("SLOPPY_E_AUTH_UNAUTHORIZED");
            }
            return this.user;
        },
        clock: hostState.clock,
        route,
        routePattern: matchedRoute?.pattern ?? null,
        query: parseQuery(targetParts.queryString),
        request,
        body: request.body,
        cookies: createCookiesLike(headers),
        connection: Object.freeze({
            id: "test-host",
            protocol: "http",
            scheme: "test",
            secure: false,
        }),
        signal: signalObject(),
        deadline: null,
        lifecycle: typeof app.__getLifecycle === "function"
            ? app.__getLifecycle()
            : Object.freeze({
                startupComplete: true,
                shuttingDown: false,
            }),
    };
    return Object.freeze(context);
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
    if (Array.isArray(result?.setCookies)) {
        for (const value of result.setCookies) {
            entries.push(["Set-Cookie", value]);
        }
    }
    return entries;
}

function assertExpectedResponseValue(actual, expected, subject) {
    if (expected instanceof RegExp) {
        if (!expected.test(String(actual ?? ""))) {
            throw new Error(`Sloppy test host expected ${subject} to match ${expected}, got ${actual}.`);
        }
        return;
    }
    if (!Object.is(actual, expected)) {
        throw new Error(`Sloppy test host expected ${subject} ${expected}, got ${actual}.`);
    }
}

function assertDeepJsonEqual(actual, expected, subject) {
    const actualText = serializeJson(actual);
    const expectedText = serializeJson(expected);
    if (actualText !== expectedText) {
        throw new Error(`Sloppy test host expected ${subject} ${expectedText}, got ${actualText}.`);
    }
}

function responseFromParts(status, headers, bodyBytes) {
    const body = new Uint8Array(bodyBytes);
    const response = {
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
        problem() {
            const value = JSON.parse(Text.utf8.decode(body));
            if (!isPlainObject(value) || !Number.isInteger(value.status)) {
                throw new TypeError("Sloppy test host response is not a ProblemDetails body.");
            }
            return value;
        },
        expectStatus(expected) {
            assertExpectedResponseValue(status, expected, "status");
            return response;
        },
        expectHeader(name, expected) {
            assertHeaderName(name, "response assertion");
            const actual = response.headers.get(name);
            if (actual === undefined) {
                throw new Error(`Sloppy test host expected response header '${name}'.`);
            }
            assertExpectedResponseValue(actual, expected, `header '${name}'`);
            return response;
        },
        expectJson(expectedOrSchema = undefined) {
            const value = response.json();
            if (expectedOrSchema !== undefined) {
                if (typeof expectedOrSchema?.validate === "function") {
                    Schema.validate(value, expectedOrSchema);
                } else {
                    assertDeepJsonEqual(value, expectedOrSchema, "JSON");
                }
            }
            return response;
        },
        expectText(expected = undefined) {
            const value = response.text();
            if (expected !== undefined) {
                assertExpectedResponseValue(value, expected, "text");
            }
            return response;
        },
        expectProblem(expected = {}) {
            const problem = response.problem();
            if (expected.status !== undefined) {
                assertExpectedResponseValue(problem.status, expected.status, "problem status");
            }
            if (expected.code !== undefined) {
                assertExpectedResponseValue(problem.code, expected.code, "problem code");
            }
            if (expected.title !== undefined) {
                assertExpectedResponseValue(problem.title, expected.title, "problem title");
            }
            return response;
        },
        expectNoBody() {
            if (body.byteLength !== 0) {
                throw new Error(`Sloppy test host expected no response body, got ${body.byteLength} byte(s).`);
            }
            return response;
        },
    };
    return Object.freeze(response);
}

function responseFromText(status, text, contentType = TEXT_CONTENT_TYPE) {
    return responseFromParts(status, [["Content-Type", contentType]], Text.utf8.encode(text));
}

function responseFromErrorStatus(app, status, context = undefined, fallbackText = `${status}\n`) {
    if (typeof app.__handleErrorStatus === "function") {
        const result = app.__handleErrorStatus(status, context);
        if (result !== undefined) {
            return responseFromResult(result);
        }
    }
    return responseFromText(status, fallbackText);
}

function problemCodeFromResponse(response) {
    const contentType = response.headers.get("content-type") ?? "";
    if (!isJsonMediaType(contentType) || !contentType.toLowerCase().includes("problem")) {
        return undefined;
    }
    try {
        const problem = response.problem();
        return problem.code;
    } catch {
        return undefined;
    }
}

function createMetricsStore() {
    const counters = new Map();
    function increment(name, labels = {}, amount = 1) {
        const key = serializeJson({ name, labels });
        counters.set(key, (counters.get(key) ?? 0) + amount);
    }
    const metrics = {
        increment,
        snapshot() {
            return Object.freeze(Array.from(counters.entries(), ([key, value]) => Object.freeze({
                ...JSON.parse(key),
                value,
            })));
        },
        expectCounter(name, expected, labels = {}) {
            const key = serializeJson({ name, labels });
            const actual = counters.get(key) ?? 0;
            if (actual !== expected) {
                throw new Error(`Sloppy TestHost expected counter '${name}' to be ${expected}, got ${actual}.`);
            }
            return metrics;
        },
    };
    return Object.freeze(metrics);
}

function createHealthHelpers(host) {
    return Object.freeze({
        async snapshot(path = "/health") {
            return (await host.get(path)).json();
        },
        async check(name, path = "/health") {
            const body = await this.snapshot(path);
            return body.checks?.find((entry) => entry.name === name);
        },
        async expect(name, status, path = "/health") {
            const check = await this.check(name, path);
            if (check === undefined) {
                throw new Error(`Sloppy TestHost expected health check '${name}'.`);
            }
            if (check.status !== status) {
                throw new Error(`Sloppy TestHost expected health check '${name}' to be '${status}', got '${check.status}'.`);
            }
            return this;
        },
        async expectStatus(status, path = "/health") {
            const body = await this.snapshot(path);
            if (body.status !== status) {
                throw new Error(`Sloppy TestHost expected health status '${status}', got '${body.status}'.`);
            }
            return this;
        },
    });
}

function createJobsHelpers(jobs = undefined) {
    if (jobs !== undefined && !isPlainObject(jobs)) {
        throw new TypeError("Sloppy TestHost jobs hooks must be a plain object.");
    }
    const queue = [];
    const jobsApi = {
        enqueue(name, payload = {}) {
            queue.push({ name, payload, status: "queued" });
            return jobsApi;
        },
        snapshot() {
            if (typeof jobs?.snapshot === "function") {
                return jobs.snapshot();
            }
            return Object.freeze(queue.map((job) => Object.freeze({ ...job })));
        },
        expectEnqueued(name, payload = undefined) {
            const found = this.snapshot().some((job) => job.name === name &&
                (payload === undefined || serializeJson(job.payload) === serializeJson(payload)));
            if (!found) {
                throw new Error(`Sloppy TestHost expected job '${name}' to be enqueued.`);
            }
            return jobsApi;
        },
        async runNext() {
            if (typeof jobs?.runNext === "function") {
                return jobs.runNext();
            }
            const job = queue.find((entry) => entry.status === "queued");
            if (job !== undefined) {
                job.status = "succeeded";
            }
            return job;
        },
        async runAllDue() {
            if (typeof jobs?.runAllDue === "function") {
                return jobs.runAllDue();
            }
            for (const job of queue) {
                if (job.status === "queued") {
                    job.status = "succeeded";
                }
            }
            return this.snapshot();
        },
        expectSucceeded(name) {
            if (!this.snapshot().some((job) => job.name === name && job.status === "succeeded")) {
                throw new Error(`Sloppy TestHost expected job '${name}' to succeed.`);
            }
            return jobsApi;
        },
    };
    return Object.freeze(jobsApi);
}

function pathToOpenApiPath(pattern) {
    return pattern.replace(/\{([A-Za-z_][0-9A-Za-z_]*)(?::[^}]+)?\}/gu, "{$1}");
}

function openApiFromRoutes(routes) {
    const paths = {};
    for (const route of routes) {
        const path = pathToOpenApiPath(route.pattern);
        paths[path] ??= {};
        paths[path][route.method.toLowerCase()] = {
            operationId: route.name,
            responses: {
                200: {
                    description: "OK",
                },
            },
            "x-slop-route": {
                pattern: route.pattern,
                name: route.name,
            },
        };
    }
    return Object.freeze({
        openapi: "3.0.3",
        info: Object.freeze({
            title: "Sloppy TestHost",
            version: "0.0.0-test",
        }),
        paths: Object.freeze(paths),
    });
}

function createOpenApiHelpers(loadDocument) {
    const helpers = async function openapi() {
        return loadDocument();
    };
    helpers.snapshot = helpers;
    helpers.expectRoute = async (method, path) => {
        const openApiDoc = await loadDocument();
        if (openApiDoc.paths?.[path]?.[method.toLowerCase()] === undefined) {
            throw new Error(`Sloppy TestHost expected OpenAPI route '${method.toUpperCase()} ${path}'.`);
        }
        return helpers;
    };
    helpers.expectResponse = async (method, path, status) => {
        const openApiDoc = await loadDocument();
        const operation = openApiDoc.paths?.[path]?.[method.toLowerCase()];
        if (operation?.responses?.[String(status)] === undefined) {
            throw new Error(`Sloppy TestHost expected OpenAPI response '${method.toUpperCase()} ${path} ${status}'.`);
        }
        return helpers;
    };
    helpers.expectComplete = async () => helpers;
    return Object.freeze(helpers);
}

function responseFromProblem(problem) {
    return responseFromParts(
        problem.status ?? 400,
        [["Content-Type", PROBLEM_CONTENT_TYPE]],
        Text.utf8.encode(serializeJson(problem)),
    );
}

function isUnsupportedMediaHelperError(error) {
    return error instanceof TypeError &&
        /^Request body is not available as (JSON|form data|multipart data)\.$/u.test(error.message);
}

function negotiatedResponse(response, requestHeaders, options) {
    if (options.contentNegotiation?.strictAccept !== true) {
        return response;
    }
    const contentType = response.headers.get("content-type");
    if (contentType === undefined || acceptsMediaType(requestHeaders.get("accept"), contentType)) {
        return response;
    }
    return responseFromText(406, "Not Acceptable\n");
}

function responseFromResultWithOptions(result, serializationOptions) {
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
    if (result.kind === "stream") {
        const chunks = Array.isArray(result.chunks) ? result.chunks : [];
        const length = chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
        const body = new Uint8Array(length);
        let offset = 0;
        for (const chunk of chunks) {
            body.set(copyBytes(chunk, "stream response chunk"), offset);
            offset += chunk.byteLength;
        }
        return responseFromParts(result.status, headers, body);
    }
    if (result.kind === "json" || result.kind === "problem") {
        const body = Object.prototype.hasOwnProperty.call(result, RAW_JSON_BODY)
            ? result[RAW_JSON_BODY]
            : (result.body === undefined ? null : result.body);
        return responseFromParts(
            result.status,
            headers,
            Text.utf8.encode(serializeJson(body, result.json ?? serializationOptions.json)),
        );
    }

    throw new TypeError(`Sloppy test host does not support result kind '${result.kind}'.`);
}

function responseFromResult(result) {
    return responseFromResultWithOptions(result, DEFAULT_SERIALIZATION_OPTIONS);
}

function finalizeResponse(response, method) {
    if (response.status === 204 || response.status === 304) {
        return responseFromParts(response.status, responseHeaderEntries(response, true), new Uint8Array(0));
    }
    if (method === "HEAD") {
        return responseFromParts(response.status, responseHeaderEntries(response), new Uint8Array(0));
    }
    return response;
}

function findRoute(routes, method, path) {
    if (method === "HEAD") {
        let methodMismatch = false;
        let getMatch = undefined;
        let getParams = undefined;
        for (const route of routes) {
            const params = matchRoutePattern(route.pattern, path);
            if (params === undefined) {
                continue;
            }
            if (route.method === "HEAD") {
                return { route, params, methodMismatch: false };
            }
            if (route.method === "GET" && getMatch === undefined) {
                getMatch = route;
                getParams = params;
                continue;
            }
            methodMismatch = true;
        }
        if (getMatch !== undefined) {
            return { route: getMatch, params: getParams, methodMismatch: false };
        }
        return { route: undefined, params: undefined, methodMismatch };
    }

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

function routeSegments(pattern) {
    return pattern === "/" ? [] : pattern.split("/").slice(1);
}

function routeSegmentRank(segment) {
    const param = parsePatternParam(segment);
    if (param === undefined) {
        return 3;
    }
    return param.type === "str" ? 1 : 2;
}

function compareRoutesByPrecedence(left, right) {
    const leftSegments = routeSegments(left.pattern);
    const rightSegments = routeSegments(right.pattern);
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
    return left.__sourceOrder - right.__sourceOrder;
}

function snapshotRoutes(app) {
    return Object.freeze(app.__getRoutes()
        .map((route, index) => Object.freeze({ ...route, __sourceOrder: index }))
        .sort(compareRoutesByPrecedence));
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

function mergeHeader(headers, name, value) {
    assertHeaderName(name, "request");
    assertHeaderValue(String(value), "request");
    headers[name] = String(value);
}

function appendQuery(target, value) {
    if (value === undefined) {
        return target;
    }
    const suffix = typeof value === "string"
        ? value.replace(/^\?/u, "")
        : new URLSearchParams(Object.entries(value).flatMap(([name, entry]) => {
            if (Array.isArray(entry)) {
                return entry.map((item) => [name, String(item)]);
            }
            return [[name, String(entry)]];
        })).toString();
    if (suffix.length === 0) {
        return target;
    }
    return `${target}${target.includes("?") ? "&" : "?"}${suffix}`;
}

function appendCookie(headers, name, value) {
    assertHeaderName(name, "cookie");
    const encoded = encodeURIComponent(String(value));
    const current = headers.Cookie ?? headers.cookie;
    const next = `${name}=${encoded}`;
    if (current === undefined) {
        headers.Cookie = next;
    } else if (headers.Cookie !== undefined) {
        headers.Cookie = `${headers.Cookie}; ${next}`;
    } else {
        headers.cookie = `${headers.cookie}; ${next}`;
    }
}

async function createJwt(claims, options = {}) {
    if (!isPlainObject(claims)) {
        throw new TypeError("Sloppy test host withJwt claims must be a plain object.");
    }
    if (typeof options.secret !== "string" || options.secret.length === 0) {
        throw new TypeError("Sloppy test host withJwt requires options.secret.");
    }
    const header = Base64Url.encode(Text.utf8.encode(serializeJson({ alg: "HS256", typ: "JWT" })));
    const payload = Base64Url.encode(Text.utf8.encode(serializeJson(claims)));
    const signature = await Hmac.sha256(Secret.fromUtf8(options.secret), `${header}.${payload}`);
    return `${header}.${payload}.${Base64Url.encode(signature)}`;
}

function createTestPrincipal(principal) {
    if (!isPlainObject(principal)) {
        throw new TypeError("Sloppy TestHost principal must be a plain object.");
    }
    const roles = Object.freeze([...(principal.roles ?? [])]);
    const scopes = Object.freeze([...(principal.scopes ?? [])]);
    const claims = Object.freeze({ ...(principal.claims ?? principal) });
    const user = {
        ...principal,
        authenticated: principal.authenticated ?? true,
        roles,
        scopes,
        claims,
        hasRole(role) {
            return typeof role === "string" && roles.includes(role);
        },
        hasScope(scope) {
            return typeof scope === "string" && scopes.includes(scope);
        },
        hasClaim(name, value = undefined) {
            if (typeof name !== "string" || !Object.prototype.hasOwnProperty.call(claims, name)) {
                return false;
            }
            return value === undefined ? true : Object.is(claims[name], value);
        },
    };
    return Object.freeze(user);
}

class RequestBuilder {
    constructor(host, method, target, options = undefined) {
        this._host = host;
        this._method = method;
        this._target = target;
        this._headers = {};
        this._body = {};
        this._timeoutMs = undefined;
        this._sent = undefined;
        this._jwt = undefined;
        if (options !== undefined) {
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy test host request options must be a plain object.");
            }
            this.headers(options.headers ?? {});
            this._body = { ...options };
            delete this._body.headers;
            if (options.timeoutMs !== undefined || options.timeout !== undefined) {
                this.timeout(options.timeoutMs ?? options.timeout);
            }
        }
    }

    header(name, value) {
        mergeHeader(this._headers, name, value);
        return this;
    }

    headers(values) {
        if (!isPlainObject(values)) {
            throw new TypeError("Sloppy test host headers() expects a plain object.");
        }
        for (const [name, value] of Object.entries(values)) {
            this.header(name, value);
        }
        return this;
    }

    query(value) {
        this._target = appendQuery(this._target, value);
        return this;
    }

    cookie(name, value) {
        appendCookie(this._headers, name, value);
        return this;
    }

    cookies(values) {
        if (!isPlainObject(values)) {
            throw new TypeError("Sloppy test host cookies() expects a plain object.");
        }
        for (const [name, value] of Object.entries(values)) {
            this.cookie(name, value);
        }
        return this;
    }

    json(value) {
        this._body = { json: value };
        return this;
    }

    text(value) {
        this._body = { text: String(value) };
        return this;
    }

    bytes(value) {
        this._body = { body: copyBytes(value, "bytes body") };
        this.header("Content-Type", "application/octet-stream");
        return this;
    }

    form(values) {
        if (!isPlainObject(values)) {
            throw new TypeError("Sloppy test host form() expects a plain object.");
        }
        this._body = { [TESTHOST_TEXT_BODY]: new URLSearchParams(Object.entries(values).map(([name, value]) => [name, String(value)])).toString() };
        this.header("Content-Type", "application/x-www-form-urlencoded");
        return this;
    }

    multipart() {
        throw new Error("Sloppy test host multipart builder is not supported by the current first-party API.");
    }

    timeout(ms) {
        if (!Number.isInteger(ms) || ms <= 0) {
            throw new TypeError("Sloppy test host timeout must be a positive integer millisecond value.");
        }
        this._timeoutMs = ms;
        return this;
    }

    bearer(token) {
        if (typeof token !== "string" || token.length === 0) {
            throw new TypeError("Sloppy test host bearer token must be a non-empty string.");
        }
        return this.header("Authorization", `Bearer ${token}`);
    }

    apiKey(key, options = {}) {
        if (typeof key !== "string" || key.length === 0) {
            throw new TypeError("Sloppy test host API key must be a non-empty string.");
        }
        const header = options.header ?? "x-api-key";
        return this.header(header, key);
    }

    asUser(principal) {
        this._body.user = createTestPrincipal(principal);
        return this;
    }

    withJwt(claims, options = {}) {
        if (typeof claims === "string") {
            return this.bearer(claims);
        }
        this._jwt = { claims, options };
        return this;
    }

    withSession(session, options = {}) {
        if (typeof session !== "string" || session.length === 0) {
            throw new TypeError("Sloppy test host session value must be a non-empty string.");
        }
        return this.cookie(options.name ?? "sloppy.session", session);
    }

    async send() {
        if (this._sent !== undefined) {
            return this._sent;
        }
        this._sent = (async () => {
            if (this._jwt !== undefined) {
                this.bearer(await createJwt(this._jwt.claims, this._jwt.options));
            }
            return this._host.request(this._method, this._target, {
                ...this._body,
                headers: this._headers,
                timeoutMs: this._timeoutMs,
            });
        })();
        return this._sent;
    }

    then(resolve, reject) {
        return this.send().then(resolve, reject);
    }

    catch(reject) {
        return this.send().catch(reject);
    }

    finally(callback) {
        return this.send().finally(callback);
    }

    async expectStatus(code) {
        return (await this.send()).expectStatus(code);
    }

    async expectHeader(name, expected) {
        return (await this.send()).expectHeader(name, expected);
    }

    async expectJson(expectedOrSchema) {
        return (await this.send()).expectJson(expectedOrSchema);
    }

    async expectText(expected) {
        return (await this.send()).expectText(expected);
    }

    async expectProblem(expected) {
        return (await this.send()).expectProblem(expected);
    }

    async expectNoBody() {
        return (await this.send()).expectNoBody();
    }
}

function byteLengthOfWebSocketMessage(kind, value) {
    if (kind === "binary") {
        return copyBytes(value, "websocket message").byteLength;
    }
    if (kind === "ping" || kind === "pong") {
        return value === undefined ? 0 : Text.utf8.encode(String(value)).byteLength;
    }
    return Text.utf8.encode(kind === "json" ? serializeJson(value) : String(value)).byteLength;
}

function createAsyncMessageQueue(onShift = undefined) {
    const values = [];
    const waiters = [];
    let closed = false;
    return {
        push(value) {
            if (closed) {
                return false;
            }
            const waiter = waiters.shift();
            if (waiter !== undefined) {
                waiter({ value, done: false });
            } else {
                values.push(value);
            }
            return true;
        },
        close() {
            if (closed) {
                return;
            }
            closed = true;
            while (waiters.length !== 0) {
                waiters.shift()({ value: undefined, done: true });
            }
        },
        async next() {
            if (values.length !== 0) {
                const value = values.shift();
                onShift?.(value);
                return { value, done: false };
            }
            if (closed) {
                return { value: undefined, done: true };
            }
            return new Promise((resolve) => {
                waiters.push((result) => {
                    if (!result.done) {
                        onShift?.(result.value);
                    }
                    resolve(result);
                });
            });
        },
        async take(timeoutMs = 1000, subject = "message") {
            let timer;
            const timeout = new Promise((_, reject) => {
                timer = setTimeout(() => {
                    reject(new Error(`Sloppy TestHost timed out waiting for WebSocket ${subject}.`));
                }, timeoutMs);
            });
            try {
                const result = await Promise.race([this.next(), timeout]);
                if (result.done) {
                    throw new Error(`Sloppy TestHost WebSocket closed while waiting for ${subject}.`);
                }
                return result.value;
            } finally {
                clearTimeout(timer);
            }
        },
        [Symbol.asyncIterator]() {
            return this;
        },
    };
}

function createWebSocketMessage(kind, value) {
    let jsonCache;
    if (kind === "json") {
        jsonCache = value;
    }
    return Object.freeze({
        kind,
        ...(kind === "text" ? { text: String(value) } : {}),
        ...(kind === "binary" ? { bytes: copyBytes(value, "websocket message") } : {}),
        ...(kind === "json" ? { text: serializeJson(value) } : {}),
        ...(kind === "ping" || kind === "pong" ? { text: value === undefined ? "" : String(value) } : {}),
        ...(kind === "close" ? { code: value.code, reason: String(value.reason ?? "") } : {}),
        json() {
            if (kind === "json") {
                return jsonCache;
            }
            if (kind !== "text") {
                throw new TypeError("Sloppy WebSocket message is not text JSON.");
            }
            jsonCache ??= JSON.parse(String(value));
            return jsonCache;
        },
        validate(schema) {
            return Schema.validate(this.json(), schema);
        },
    });
}

function websocketOriginsAllow(routeOptions, origin) {
    const origins = routeOptions.origins;
    if (origin === undefined || origin.length === 0 || origins === undefined || origins === "*") {
        return true;
    }
    return Array.isArray(origins) && origins.includes(origin);
}

function websocketReject(status, code, message) {
    const error = new Error(message);
    error.status = status;
    error.code = code;
    return error;
}

class TestWebSocket {
    constructor(state) {
        this._state = state;
    }

    get closed() {
        return this._state.closed;
    }

    get protocol() {
        return this._state.protocol;
    }

    _send(kind, value) {
        if (this._state.closed) {
            throw new Error("Sloppy TestHost WebSocket is closed.");
        }
        const bytes = byteLengthOfWebSocketMessage(kind, value);
        if (bytes > this._state.options.maxMessageBytes) {
            this._state.close(1009, "message too large");
            throw new Error("SLOPPY_E_WEBSOCKET_MESSAGE_TOO_LARGE");
        }
        this._state.touch?.();
        this._state.clientToServer.push(createWebSocketMessage(kind, value));
        this._state.metrics.increment("websocket.messages.in.total", {
            route: this._state.route,
            kind,
        });
        this._state.metrics.increment("websocket.bytes.in.total", {
            route: this._state.route,
            kind,
        }, bytes);
        return Promise.resolve();
    }

    sendText(text) {
        return this._send("text", String(text));
    }

    sendJson(value) {
        return this._send("json", value);
    }

    sendBytes(bytes) {
        return this._send("binary", bytes);
    }

    sendPing(payload = "") {
        return this._send("ping", payload);
    }

    sendPong(payload = "") {
        return this._send("pong", payload);
    }

    async expectText(expected, options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "text message");
        if (message.kind !== "text") {
            throw new Error(`Sloppy TestHost expected WebSocket text message, got '${message.kind}'.`);
        }
        assertExpectedResponseValue(message.text, expected, "WebSocket text");
        return this;
    }

    async expectJson(expectedOrSchema, options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "JSON message");
        const value = message.kind === "json" ? message.json() : message.json();
        if (Schema.isSchema(expectedOrSchema)) {
            Schema.validate(value, expectedOrSchema);
        } else {
            assertDeepJsonEqual(value, expectedOrSchema, "WebSocket JSON");
        }
        return this;
    }

    async expectBytes(expected, options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "binary message");
        if (message.kind !== "binary") {
            throw new Error(`Sloppy TestHost expected WebSocket binary message, got '${message.kind}'.`);
        }
        assertDeepJsonEqual([...message.bytes], [...copyBytes(expected, "expected WebSocket bytes")], "WebSocket bytes");
        return this;
    }

    async expectPing(options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "ping");
        if (message.kind !== "ping") {
            throw new Error(`Sloppy TestHost expected WebSocket ping, got '${message.kind}'.`);
        }
        return this;
    }

    async expectPong(options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "pong");
        if (message.kind !== "pong") {
            throw new Error(`Sloppy TestHost expected WebSocket pong, got '${message.kind}'.`);
        }
        return this;
    }

    async expectClose(code = undefined, options = {}) {
        const message = await this._state.serverToClient.take(options.timeoutMs, "close");
        if (message.kind !== "close") {
            throw new Error(`Sloppy TestHost expected WebSocket close, got '${message.kind}'.`);
        }
        if (code !== undefined) {
            assertExpectedResponseValue(message.code, code, "WebSocket close code");
        }
        return this;
    }

    close(code = 1000, reason = "") {
        this._state.close(code, reason);
        return Promise.resolve();
    }
}

class WebSocketConnectAttempt {
    constructor(start) {
        this._start = start;
        this._promise = undefined;
    }

    _connect() {
        if (this._promise === undefined) {
            this._promise = Promise.resolve().then(this._start);
            this._promise.catch(() => {});
        }
        return this._promise;
    }

    then(resolve, reject) {
        return this._connect().then(resolve, reject);
    }

    catch(reject) {
        return this._connect().catch(reject);
    }

    finally(callback) {
        return this._connect().finally(callback);
    }

    async expectRejected(status) {
        try {
            await this._connect();
        } catch (error) {
            assertExpectedResponseValue(error.status, status, "WebSocket rejection status");
            return undefined;
        }
        throw new Error(`Sloppy TestHost expected WebSocket rejection status ${status}.`);
    }
}

class WebSocketBuilder {
    constructor(host, target, options = undefined) {
        this._host = host;
        this._target = target;
        this._headers = {};
        this._timeoutMs = 1000;
        this._protocols = [];
        this._user = undefined;
        this._jwt = undefined;
        if (options !== undefined) {
            if (!isPlainObject(options)) {
                throw new TypeError("Sloppy TestHost WebSocket options must be a plain object.");
            }
            this.headers(options.headers ?? {});
            if (options.origin !== undefined) {
                this.origin(options.origin);
            }
            if (options.protocols !== undefined) {
                this.protocols(options.protocols);
            }
            if (options.timeoutMs !== undefined) {
                this.timeout(options.timeoutMs);
            }
        }
    }

    header(name, value) {
        mergeHeader(this._headers, name, value);
        return this;
    }

    headers(values) {
        if (!isPlainObject(values)) {
            throw new TypeError("Sloppy TestHost WebSocket headers() expects a plain object.");
        }
        for (const [name, value] of Object.entries(values)) {
            this.header(name, value);
        }
        return this;
    }

    origin(value) {
        if (typeof value !== "string" || value.length === 0) {
            throw new TypeError("Sloppy TestHost WebSocket origin must be a non-empty string.");
        }
        return this.header("Origin", value);
    }

    protocols(values) {
        if (!Array.isArray(values)) {
            throw new TypeError("Sloppy TestHost WebSocket protocols must be an array.");
        }
        this._protocols = values.map(String);
        if (this._protocols.length !== 0) {
            this.header("Sec-WebSocket-Protocol", this._protocols.join(", "));
        }
        return this;
    }

    timeout(ms) {
        if (!Number.isInteger(ms) || ms <= 0) {
            throw new TypeError("Sloppy TestHost WebSocket timeout must be a positive integer millisecond value.");
        }
        this._timeoutMs = ms;
        return this;
    }

    bearer(token) {
        if (typeof token !== "string" || token.length === 0) {
            throw new TypeError("Sloppy TestHost WebSocket bearer token must be a non-empty string.");
        }
        return this.header("Authorization", `Bearer ${token}`);
    }

    apiKey(key, options = {}) {
        if (typeof key !== "string" || key.length === 0) {
            throw new TypeError("Sloppy TestHost WebSocket API key must be a non-empty string.");
        }
        return this.header(options.header ?? "x-api-key", key);
    }

    withSession(session, options = {}) {
        if (typeof session !== "string" || session.length === 0) {
            throw new TypeError("Sloppy TestHost WebSocket session value must be a non-empty string.");
        }
        appendCookie(this._headers, options.name ?? "sloppy.session", session);
        return this;
    }

    withJwt(claims, options = {}) {
        if (typeof claims === "string") {
            return this.bearer(claims);
        }
        this._jwt = { claims, options };
        return this;
    }

    asUser(principal) {
        this._user = createTestPrincipal(principal);
        return this;
    }

    connect() {
        return new WebSocketConnectAttempt(async () => {
            if (this._jwt !== undefined) {
                this.bearer(await createJwt(this._jwt.claims, this._jwt.options));
            }
            return this._host.websocketConnect(this._target, {
                headers: this._headers,
                protocols: this._protocols,
                timeoutMs: this._timeoutMs,
                user: this._user,
            });
        });
    }
}

function createFluentHost(base, mode = "inProcess", defaults = {}) {
    const host = {
        mode,
        request(method, target, options) {
            const merged = {
                ...(defaults.options ?? {}),
                ...(options ?? {}),
                headers: {
                    ...(defaults.headers ?? {}),
                    ...(options?.headers ?? {}),
                },
            };
            if (defaults.user !== undefined && merged.user === undefined) {
                merged.user = defaults.user;
            }
            return base.request(method, target, merged);
        },
        websocketConnect(target, options) {
            const merged = {
                ...(defaults.options ?? {}),
                ...(options ?? {}),
                headers: {
                    ...(defaults.headers ?? {}),
                    ...(options?.headers ?? {}),
                },
            };
            if (defaults.user !== undefined && merged.user === undefined) {
                merged.user = defaults.user;
            }
            return base.websocketConnect(target, merged);
        },
        get(target, options) {
            return new RequestBuilder(host, "GET", target, options);
        },
        post(target, options) {
            return new RequestBuilder(host, "POST", target, options);
        },
        put(target, options) {
            return new RequestBuilder(host, "PUT", target, options);
        },
        patch(target, options) {
            return new RequestBuilder(host, "PATCH", target, options);
        },
        delete(target, options) {
            return new RequestBuilder(host, "DELETE", target, options);
        },
        options(target, options) {
            return new RequestBuilder(host, "OPTIONS", target, options);
        },
        head(target, options) {
            return new RequestBuilder(host, "HEAD", target, options);
        },
        websocket(target, options) {
            return new WebSocketBuilder(host, target, options);
        },
        asUser(principal) {
            return createFluentHost(base, mode, {
                ...defaults,
                user: createTestPrincipal(principal),
            });
        },
        withHeader(name, value) {
            assertHeaderName(name, "request");
            assertHeaderValue(String(value), "request");
            return createFluentHost(base, mode, {
                ...defaults,
                headers: {
                    ...(defaults.headers ?? {}),
                    [name]: String(value),
                },
            });
        },
        close() {
            return base.close();
        },
        dispose() {
            return base.close();
        },
        async [Symbol.asyncDispose]() {
            await base.close();
        },
        diagnostics: base.diagnostics,
        health: undefined,
        metrics: base.metrics,
        jobs: base.jobs,
        openapi: base.openapi,
        baseUrl: base.baseUrl,
        port: base.port,
    };
    host.health = createHealthHelpers(host);
    return Object.freeze(host);
}

function createTestHost(app, options = {}) {
    if (app === null || typeof app !== "object" || typeof app.__getRoutes !== "function") {
        throw new TypeError("Sloppy createTestHost expects a Sloppy app.");
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy TestHost options must be a plain object.");
    }

    app.freeze();
    const routes = snapshotRoutes(app);
    const serializationOptions = typeof app.__getSerializationOptions === "function"
        ? app.__getSerializationOptions()
        : DEFAULT_SERIALIZATION_OPTIONS;
    let closed = false;
    let activeRequests = 0;
    const activeSockets = new Set();
    let closePromise = undefined;
    let drainWaiters = [];
    const secretValues = Object.values(options.secrets ?? {});
    const diagnostics = createDiagnosticsStore(secretValues);
    const metrics = createMetricsStore();
    const jobs = createJobsHelpers(options.jobs);
    const hostState = Object.freeze({
        config: createConfigOverlay(app.config, options.config, options.secrets),
        services: createServiceOverlay(app.services, options.services, options.providers, options.caches),
        clock: options.clock,
        diagnostics,
    });

    function appMetricsRegistry() {
        return typeof app.__getMetricsRegistry === "function" ? app.__getMetricsRegistry() : undefined;
    }

    function recordHttpMetric(metricRegistry, labels, response, durationMs, requestBytes) {
        if (metricRegistry === undefined) {
            return;
        }
        const responseBytes = response.bytes().byteLength;
        metricRegistry.counter("http.requests.total", { description: "HTTP requests processed by the app host." }).inc(labels);
        metricRegistry.counter("http.route.hits", { description: "HTTP route hits by route pattern." }).inc(labels);
        metricRegistry.counter("http.request.bytes", { description: "HTTP request body bytes processed by the app host." }).inc(labels, requestBytes);
        metricRegistry.counter("http.response.bytes", { description: "HTTP response body bytes written by the app host." }).inc(labels, responseBytes);
        metricRegistry.histogram("http.request.duration.ms", { description: "HTTP request duration in milliseconds." }).observe(labels, durationMs);
        metricRegistry.counter("http.status.total", { description: "HTTP responses by status code and class." }).inc({
            ...labels,
            status: String(response.status),
            statusClass: `${Math.trunc(response.status / 100)}xx`,
        });
        if (response.status >= 500) {
            metricRegistry.counter("http.errors.total", { description: "HTTP responses with 5xx status." }).inc(labels);
        }
    }

    function ignoreMetricError(callback) {
        try {
            callback();
        } catch {
        }
    }

    function finishRequest() {
        activeRequests -= 1;
        if (closed && activeRequests === 0 && activeSockets.size === 0) {
            const waiters = drainWaiters;
            drainWaiters = [];
            for (const resolve of waiters) {
                resolve();
            }
        }
    }

    function waitForDrain() {
        if (activeRequests === 0 && activeSockets.size === 0) {
            return Promise.resolve();
        }
        return new Promise((resolve) => {
            drainWaiters.push(resolve);
        });
    }

    function finishSocket(state) {
        activeSockets.delete(state);
        if (closed && activeRequests === 0 && activeSockets.size === 0) {
            const waiters = drainWaiters;
            drainWaiters = [];
            for (const resolve of waiters) {
                resolve();
            }
        }
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
            const bodyBytes = normalizeRequestBodyWithOptions(normalizedOptions, headerEntries, serializationOptions);
            const headers = createHeadersLike(headerEntries);
            const match = findRoute(routes, normalizedMethod, targetParts.path);
            diagnostics.record({
                code: "SLOPPY_TESTHOST_REQUEST",
                subsystem: "http",
                severity: "debug",
                message: "TestHost request started.",
                fields: { method: normalizedMethod, path: targetParts.path },
            });

            const policy = typeof app.__getErrorPolicy === "function" ? app.__getErrorPolicy() : undefined;
            if (policy !== undefined && bodyBytes.byteLength > policy.maxBodyBytes) {
                diagnostics.record({ code: "SLOPPY_E_REQUEST_BODY_TOO_LARGE", subsystem: "http", severity: "warn" });
                return finalizeResponse(responseFromErrorStatus(app, 413, undefined, "Payload Too Large\n"), normalizedMethod);
            }

            if (match.route === undefined) {
                diagnostics.record({
                    code: match.methodMismatch ? "SLOPPY_E_METHOD_NOT_ALLOWED" : "SLOPPY_E_ROUTE_NOT_FOUND",
                    subsystem: "routing",
                    severity: "warn",
                    fields: { method: normalizedMethod, path: targetParts.path },
                });
                const response = finalizeResponse(match.methodMismatch
                    ? responseFromText(405, "Method Not Allowed\n")
                    : policy?.missingRoute === true
                        ? responseFromErrorStatus(app, 404, undefined, "Not Found\n")
                        : responseFromText(404, "Not Found\n"), normalizedMethod);
                metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                return response;
            }

            const bodyKind = bodyKindForRequest(headers, bodyBytes);
            if (bodyKind === "malformed-json") {
                diagnostics.record({ code: "SLOPPY_E_JSON_INVALID", subsystem: "http", severity: "warn" });
                const response = finalizeResponse(responseFromProblem(validationProblem([
                    {
                        path: [],
                        code: "json.invalid",
                        message: "Request body is not valid JSON.",
                    },
                ])), normalizedMethod);
                metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                return response;
            }
            if (bodyKind === "malformed-multipart") {
                diagnostics.record({ code: "SLOPPY_E_MULTIPART_INVALID", subsystem: "http", severity: "warn" });
                const response = finalizeResponse(responseFromText(400, "Malformed Multipart\n"), normalizedMethod);
                metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                return response;
            }
            if (bodyKind === "unsupported" && bodyBytes.byteLength !== 0) {
                diagnostics.record({ code: "SLOPPY_E_UNSUPPORTED_MEDIA_TYPE", subsystem: "http", severity: "warn" });
                const response = finalizeResponse(responseFromErrorStatus(app, 415, undefined, "Unsupported Media Type\n"), normalizedMethod);
                metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                return response;
            }

            const context = createContext(app, hostState, normalizedMethod, targetParts, headers, match.params, match.route, bodyKind, bodyBytes, normalizedOptions);
            const metricRegistry = appMetricsRegistry();
            const metricLabels = Object.freeze({
                method: normalizedMethod,
                route: match.route.pattern,
            });
            ignoreMetricError(() => {
                metricRegistry?.gauge("http.requests.active", { description: "HTTP requests currently active in the app host." }).inc(metricLabels);
            });
            const started = nowMs();
            try {
                try {
                    const response = finalizeResponse(negotiatedResponse(
                        responseFromResultWithOptions(await match.route.handler(context), serializationOptions),
                        headers,
                        serializationOptions,
                    ), normalizedMethod);
                    const problemCode = problemCodeFromResponse(response);
                    if (problemCode !== undefined) {
                        diagnostics.record({ code: problemCode, subsystem: "http", severity: response.status >= 500 ? "error" : "warn" });
                    }
                    metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                    ignoreMetricError(() => {
                        recordHttpMetric(metricRegistry, metricLabels, response, Math.max(0, nowMs() - started), bodyBytes.byteLength);
                    });
                    return response;
                } catch (error) {
                    if (isUnsupportedMediaHelperError(error)) {
                        diagnostics.record({ code: "SLOPPY_E_UNSUPPORTED_MEDIA_TYPE", subsystem: "http", severity: "warn" });
                        const response = finalizeResponse(responseFromText(415, "Unsupported Media Type\n"), normalizedMethod);
                        metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
                        ignoreMetricError(() => {
                            recordHttpMetric(metricRegistry, metricLabels, response, Math.max(0, nowMs() - started), bodyBytes.byteLength);
                        });
                        return response;
                    }
                    diagnostics.record({
                        code: "SLOPPY_E_HANDLER_ERROR",
                        subsystem: "http",
                        severity: "error",
                        message: error.message,
                    });
                    throw error;
                }
            } finally {
                try {
                    ignoreMetricError(() => {
                        metricRegistry?.gauge("http.requests.active").dec(metricLabels);
                    });
                } finally {
                    await context.services.dispose();
                }
            }
        } finally {
            finishRequest();
        }
    }

    async function websocketConnect(target, options = undefined) {
        if (closed) {
            throw new Error("Sloppy test host is closed.");
        }
        const normalizedOptions = normalizeOptions(options);
        const targetParts = splitTarget(target);
        const headerEntries = headerEntriesFromObject(normalizedOptions.headers, "request");
        const headers = createHeadersLike(headerEntries);
        const match = findRoute(routes, "GET", targetParts.path);
        diagnostics.record({
            code: "SLOPPY_TESTHOST_WEBSOCKET_CONNECT",
            subsystem: "websocket",
            severity: "debug",
            message: "TestHost WebSocket connection started.",
            fields: { route: match.route?.pattern ?? null },
        });
        if (match.route === undefined || match.route.kind !== "websocket") {
            metrics.increment("websocket.upgrades.rejected.total", { outcome: "not-found" });
            throw websocketReject(match.route === undefined ? 404 : 405, "SLOPPY_E_WEBSOCKET_ROUTE_NOT_FOUND", "WebSocket route was not found.");
        }

        const routeOptions = match.route.metadata.realtime?.websocket ??
            match.route.handler?.[WEBSOCKET_ROUTE_OPTIONS] ??
            Object.freeze({ maxMessageBytes: 64 * 1024, maxSendQueueBytes: 1024 * 1024, closeTimeoutMs: 5000 });
        const origin = headers.get("origin");
        if (!websocketOriginsAllow(routeOptions, origin)) {
            diagnostics.record({
                code: "SLOPPY_E_WEBSOCKET_ORIGIN_REJECTED",
                subsystem: "websocket",
                severity: "warn",
                fields: { route: match.route.pattern },
            });
            metrics.increment("websocket.upgrades.rejected.total", { outcome: "origin" });
            throw websocketReject(403, "SLOPPY_E_WEBSOCKET_ORIGIN_REJECTED", "WebSocket origin is not allowed.");
        }

        const requestedProtocols = Array.isArray(normalizedOptions.protocols)
            ? normalizedOptions.protocols
            : (headers.get("sec-websocket-protocol") ?? "")
                .split(",")
                .map((value) => value.trim())
                .filter((value) => value.length !== 0);
        let protocol = "";
        if (Array.isArray(routeOptions.protocols) && routeOptions.protocols.length !== 0) {
            protocol = requestedProtocols.find((value) => routeOptions.protocols.includes(value)) ?? "";
            if (protocol.length === 0) {
                metrics.increment("websocket.upgrades.rejected.total", { outcome: "protocol" });
                throw websocketReject(400, "SLOPPY_E_WEBSOCKET_PROTOCOL_REJECTED", "WebSocket subprotocol is not allowed.");
            }
        }

        const clientToServer = createAsyncMessageQueue();
        let queuedServerBytes = 0;
        const serverToClient = createAsyncMessageQueue((message) => {
            if (message.kind === "text") {
                queuedServerBytes = Math.max(0, queuedServerBytes - byteLengthOfWebSocketMessage("text", message.text));
            } else if (message.kind === "json") {
                queuedServerBytes = Math.max(0, queuedServerBytes - byteLengthOfWebSocketMessage("json", message.json()));
            } else if (message.kind === "binary") {
                queuedServerBytes = Math.max(0, queuedServerBytes - byteLengthOfWebSocketMessage("binary", message.bytes));
            } else if (message.kind === "ping" || message.kind === "pong") {
                queuedServerBytes = Math.max(0, queuedServerBytes - byteLengthOfWebSocketMessage(message.kind, message.text));
            }
        });
        let socketContext;
        let accepted = false;
        let finished = false;
        let heartbeatTimer;
        let idleTimer;
        let acceptResolve;
        const acceptedPromise = new Promise((resolve) => {
            acceptResolve = resolve;
        });
        const state = {
            options: routeOptions,
            route: match.route.pattern,
            metrics,
            clientToServer,
            serverToClient,
            protocol,
            closed: false,
            touch() {
                if (!Number.isInteger(routeOptions.idleTimeoutMs) || routeOptions.idleTimeoutMs <= 0 || state.closed) {
                    return;
                }
                clearTimeout(idleTimer);
                idleTimer = setTimeout(() => {
                    state.close(1001, "idle timeout");
                }, routeOptions.idleTimeoutMs);
            },
            close(code = 1000, reason = "") {
                if (state.closed) {
                    return;
                }
                state.closed = true;
                clearInterval(heartbeatTimer);
                clearTimeout(idleTimer);
                clientToServer.close();
                serverToClient.push(createWebSocketMessage("close", { code, reason }));
                serverToClient.close();
                metrics.increment("websocket.close.total", {
                    route: match.route.pattern,
                    code: String(code),
                });
            },
        };
        const socket = {
            get ctx() {
                return socketContext;
            },
            __setContext(ctx) {
                socketContext = ctx;
            },
            get closed() {
                return state.closed;
            },
            get protocol() {
                return protocol;
            },
            id: `test-ws-${activeSockets.size + 1}`,
            remoteAddress: "test-host",
            get request() {
                return socketContext?.request;
            },
            async accept() {
                if (state.closed) {
                    throw new Error("Sloppy WebSocket is closed.");
                }
                if (!accepted) {
                    accepted = true;
                    metrics.increment("websocket.upgrades.total", { route: match.route.pattern, outcome: "accepted" });
                    if (Number.isInteger(routeOptions.heartbeatMs) && routeOptions.heartbeatMs > 0) {
                        heartbeatTimer = setInterval(() => {
                            if (state.closed) {
                                return;
                            }
                            serverToClient.push(createWebSocketMessage("ping", ""));
                            metrics.increment("websocket.messages.out.total", {
                                route: match.route.pattern,
                                kind: "ping",
                            });
                        }, routeOptions.heartbeatMs);
                    }
                    state.touch();
                    acceptResolve();
                }
            },
            async close(code = 1000, reason = "") {
                state.close(code, reason);
            },
            async sendText(text) {
                return sendFromServer("text", String(text));
            },
            async sendJson(value) {
                return sendFromServer("json", value);
            },
            async sendBytes(bytes) {
                return sendFromServer("binary", bytes);
            },
            async sendPing(payload = "") {
                return sendFromServer("ping", payload);
            },
            async sendPong(payload = "") {
                return sendFromServer("pong", payload);
            },
            messages() {
                return clientToServer;
            },
        };

        function sendFromServer(kind, value) {
            if (!accepted) {
                throw new Error("SLOPPY_E_WEBSOCKET_NOT_ACCEPTED: call socket.accept() before sending.");
            }
            if (state.closed) {
                throw new Error("SLOPPY_E_WEBSOCKET_CLOSED");
            }
            const bytes = byteLengthOfWebSocketMessage(kind, value);
            if (bytes > routeOptions.maxMessageBytes) {
                state.close(1009, "message too large");
                throw new Error("SLOPPY_E_WEBSOCKET_MESSAGE_TOO_LARGE");
            }
            if (queuedServerBytes + bytes > routeOptions.maxSendQueueBytes) {
                metrics.increment("websocket.backpressure.total", { route: match.route.pattern, outcome: routeOptions.slowClientPolicy ?? "error" });
                if (routeOptions.slowClientPolicy === "close") {
                    state.close(1013, "send queue full");
                    return Promise.resolve();
                }
                throw new Error("SLOPPY_E_WEBSOCKET_BACKPRESSURE");
            }
            queuedServerBytes += bytes;
            const message = createWebSocketMessage(kind, value);
            serverToClient.push(message);
            metrics.increment("websocket.messages.out.total", { route: match.route.pattern, kind });
            metrics.increment("websocket.bytes.out.total", { route: match.route.pattern, kind }, bytes);
            return Promise.resolve();
        }

        const context = createContext(
            app,
            hostState,
            "GET",
            targetParts,
            headers,
            match.params,
            match.route,
            "none",
            new Uint8Array(0),
            normalizedOptions,
        );
        socketContext = {
            ...context,
            __sloppyWebSocketHandshake: true,
            __sloppyWebSocket: socket,
            connection: Object.freeze({
                id: socket.id,
                protocol: "websocket",
                scheme: "test",
                secure: false,
            }),
        };

        activeSockets.add(state);
        const handlerPromise = Promise.resolve(match.route.handler(socketContext)).then(
            (value) => {
                finished = true;
                if (!accepted) {
                    return value;
                }
                if (!state.closed) {
                    state.close(1000, "handler complete");
                }
                return undefined;
            },
            (error) => {
                finished = true;
                diagnostics.record({
                    code: "SLOPPY_E_WEBSOCKET_HANDLER_ERROR",
                    subsystem: "websocket",
                    severity: "error",
                    message: error.message,
                    fields: { route: match.route.pattern },
                });
                state.close(1011, "handler error");
                return undefined;
            },
        ).finally(async () => {
            try {
                await socketContext.services.dispose();
            } finally {
                finishSocket(state);
            }
        });

        const timeoutMs = normalizedOptions.timeoutMs ?? 1000;
        let timer;
        const timeoutPromise = new Promise((_, reject) => {
            timer = setTimeout(() => {
                reject(websocketReject(504, "SLOPPY_E_WEBSOCKET_ACCEPT_TIMEOUT", "WebSocket handler did not accept before timeout."));
            }, timeoutMs);
        });
        try {
            const outcome = await Promise.race([
                acceptedPromise.then(() => Object.freeze({ kind: "accepted" })),
                handlerPromise.then((value) => Object.freeze({ kind: "handler", value })),
                timeoutPromise,
            ]);
            if (outcome.kind === "handler" && !accepted) {
                const response = outcome.value === undefined
                    ? undefined
                    : responseFromResultWithOptions(outcome.value, serializationOptions);
                throw websocketReject(
                    response?.status ?? 500,
                    response === undefined
                        ? "SLOPPY_E_WEBSOCKET_REJECTED"
                        : (problemCodeFromResponse(response) ?? "SLOPPY_E_WEBSOCKET_REJECTED"),
                    "WebSocket upgrade was rejected.",
                );
            }
        } finally {
            clearTimeout(timer);
        }
        if (!accepted) {
            throw websocketReject(500, "SLOPPY_E_WEBSOCKET_NOT_ACCEPTED", "WebSocket was not accepted.");
        }
        metrics.increment("websocket.connections.total", { route: match.route.pattern });
        return Object.freeze(new TestWebSocket(state));
    }

    const host = {
        request,
        websocketConnect,
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
        websocket(target, options) {
            return new WebSocketBuilder(host, target, options);
        },
        async close() {
            if (closePromise !== undefined) {
                return closePromise;
            }
            if (typeof app.__beginShutdown === "function") {
                app.__beginShutdown();
            }
            closed = true;
            for (const socket of [...activeSockets]) {
                socket.close(1001, "test host closed");
            }
            closePromise = (async () => {
                await waitForDrain();
                await hostState.services.dispose();
                return app.services.dispose();
            })();
            return closePromise;
        },
        diagnostics,
        metrics,
        jobs,
        openapi: createOpenApiHelpers(() => openApiFromRoutes(routes)),
    };
    return Object.freeze(host);
}

function findBytes(bytes, pattern) {
    outer:
    for (let index = 0; index <= bytes.byteLength - pattern.byteLength; index += 1) {
        for (let offset = 0; offset < pattern.byteLength; offset += 1) {
            if (bytes[index + offset] !== pattern[offset]) {
                continue outer;
            }
        }
        return index;
    }
    return -1;
}

function parseHttpResponseBytes(bytes) {
    const separators = [
        Text.utf8.encode("\r\n\r\n"),
        Text.utf8.encode("\r\r\n\r\r\n"),
        Text.utf8.encode("\n\n"),
    ];
    let split = -1;
    let separatorLength = 0;
    for (const separator of separators) {
        split = findBytes(bytes, separator);
        if (split >= 0) {
            separatorLength = separator.byteLength;
            break;
        }
    }
    if (split < 0) {
        throw new Error("Sloppy TestHost could not parse the HTTP response emitted by sloppy run.");
    }
    const head = Text.utf8.decode(bytes.slice(0, split)).replace(/\r+\n/gu, "\n");
    const body = bytes.slice(split + separatorLength);
    const lines = head.split("\n");
    const statusMatch = /^HTTP\/\d(?:\.\d)?\s+(\d{3})\b/u.exec(lines[0] ?? "");
    if (statusMatch === null) {
        throw new Error(`Sloppy TestHost received an invalid HTTP status line: ${lines[0] ?? ""}`);
    }
    const headers = [];
    for (const line of lines.slice(1)) {
        const colon = line.indexOf(":");
        if (colon <= 0) {
            continue;
        }
        headers.push([line.slice(0, colon).trim(), line.slice(colon + 1).trim()]);
    }
    return responseFromParts(Number.parseInt(statusMatch[1], 10), headers, body);
}

function cliPath(options = {}) {
    if (options.cliPath !== undefined) {
        if (typeof options.cliPath !== "string" || options.cliPath.length === 0) {
            throw new TypeError("Sloppy TestHost cliPath must be a non-empty string.");
        }
        return options.cliPath;
    }
    return SloppyProcess.info().executablePath;
}

function pathRunArgs(kind, targetPath, mode) {
    const args = ["run"];
    if (kind === "artifacts") {
        args.push("--artifacts", targetPath);
    } else {
        args.push(targetPath);
    }
    if (mode === "loopback") {
        return args;
    }
    return args;
}

function requestHeaderArgs(headers) {
    const args = [];
    for (const [name, value] of headers) {
        if (name.toLowerCase() === "content-length") {
            continue;
        }
        args.push("--header", `${name}: ${value}`);
    }
    return args;
}

async function withRequestBodyFile(options, callback) {
    const headerEntries = headerEntriesFromObject(options.headers, "request");
    const bytes = normalizeRequestBody(options, headerEntries);
    const headers = headerEntries.filter(([name]) => name.toLowerCase() !== "content-length");
    if (bytes.byteLength === 0) {
        return callback(headers, undefined);
    }
    const root = options.tempDirectory ?? SloppySystem.tempDirectory ?? ".sloppy/testhost";
    await Directory.create(root, { recursive: true });
    const tempDir = await Directory.createTemp(root, { prefix: "request-" });
    const bodyPath = `${tempDir.replace(/[\\/]$/u, "")}/body.bin`;
    try {
        await File.writeBytes(bodyPath, bytes);
        return await callback(headers, bodyPath);
    } finally {
        await Directory.delete(tempDir, { recursive: true }).catch(() => {});
    }
}

async function openApiFromCli(kind, targetPath, options = {}) {
    const command = cliPath(options);
    const args = kind === "artifacts"
        ? ["openapi", "--artifacts", targetPath]
        : ["openapi", `${targetPath.replace(/[\\/]$/u, "")}/artifacts`];
    const result = await SloppyProcess.run(command, args, {
        cwd: options.cwd,
        env: options.env,
        capture: "bytes",
        timeoutMs: options.openapiTimeoutMs ?? options.timeoutMs ?? 30000,
        maxStdoutBytes: options.maxStdoutBytes ?? 16 * 1024 * 1024,
        maxStderrBytes: options.maxStderrBytes ?? 1024 * 1024,
    });
    if (result.exitCode !== 0) {
        const stderr = result.stderr instanceof Uint8Array ? Text.utf8.decode(result.stderr) : String(result.stderr ?? "");
        throw new Error(`Sloppy TestHost OpenAPI failed with exit code ${result.exitCode}.${stderr.length === 0 ? "" : `\n${stderr.trimEnd()}`}`);
    }
    return JSON.parse(Text.utf8.decode(result.stdout));
}

function createProcessHelpers(kind, targetPath, options) {
    return Object.freeze({
        diagnostics: createDiagnosticsStore(Object.values(options.secrets ?? {})),
        metrics: createMetricsStore(),
        jobs: createJobsHelpers(options.jobs),
        openapi: createOpenApiHelpers(() => openApiFromCli(kind, targetPath, options)),
    });
}

function unsupportedProcessWebSocketConnect(helpers, mode) {
    return async function websocketConnect() {
        helpers.diagnostics.record({
            code: "SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED",
            subsystem: "websocket",
            severity: "warn",
            message: `Sloppy TestHost ${mode} WebSocket connections are not supported by this runtime lane.`,
        });
        throw websocketReject(
            501,
            "SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED",
            `Sloppy TestHost ${mode} WebSocket connections are not supported by this runtime lane.`,
        );
    };
}

function createProcessOnceHost(kind, targetPath, options = {}) {
    const command = cliPath(options);
    const helpers = createProcessHelpers(kind, targetPath, options);
    let closed = false;
    async function request(method, target, requestOptions = undefined) {
        if (closed) {
            throw new Error("Sloppy TestHost one-off CLI host is closed.");
        }
        const normalizedMethod = normalizeMethod(method);
        splitTarget(target);
        const normalizedOptions = normalizeOptions(requestOptions);
        return withRequestBodyFile({
            ...normalizedOptions,
            tempDirectory: normalizedOptions.tempDirectory ?? options.tempDirectory,
        }, async (headers, bodyPath) => {
            const args = [
                ...pathRunArgs(kind, targetPath, "inProcess"),
                "--once",
                normalizedMethod,
                target,
                ...requestHeaderArgs(headers),
            ];
            if (bodyPath !== undefined) {
                args.push("--body-file", bodyPath);
            }
            const result = await SloppyProcess.run(command, args, {
                cwd: options.cwd,
                env: options.env,
                capture: "bytes",
                timeoutMs: normalizedOptions.timeoutMs ?? options.timeoutMs ?? 30000,
                maxStdoutBytes: options.maxStdoutBytes ?? 16 * 1024 * 1024,
                maxStderrBytes: options.maxStderrBytes ?? 1024 * 1024,
            });
            if (result.exitCode !== 0) {
                const stdout = result.stdout instanceof Uint8Array ? Text.utf8.decode(result.stdout) : String(result.stdout ?? "");
                const stderr = result.stderr instanceof Uint8Array ? Text.utf8.decode(result.stderr) : String(result.stderr ?? "");
                helpers.diagnostics.record({
                    code: "SLOPPY_E_TESTHOST_PROCESS_REQUEST",
                    subsystem: "process",
                    severity: "error",
                    message: stderr.trimEnd(),
                    fields: { exitCode: result.exitCode, stdout, stderr },
                });
                throw new Error(`Sloppy TestHost request failed with exit code ${result.exitCode}.${stderr.length === 0 ? "" : `\n${stderr.trimEnd()}`}`);
            }
            const response = finalizeResponse(parseHttpResponseBytes(result.stdout), normalizedMethod);
            helpers.metrics.increment("http.requests.total", { method: normalizedMethod, status: String(response.status) });
            return response;
        });
    }
    return Object.freeze({
        request,
        websocketConnect: unsupportedProcessWebSocketConnect(helpers, "one-off CLI"),
        close() {
            closed = true;
        },
        ...helpers,
    });
}

const LOOPBACK_PORT_MIN = 49152;
const LOOPBACK_PORT_MAX = 65535;

function validateLoopbackPort(port) {
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        throw new TypeError("Sloppy TestHost loopback port must be an integer from 1 to 65535.");
    }
    return port;
}

function randomLoopbackPort() {
    const bytes = Random.bytes(2);
    const value = (bytes[0] << 8) | bytes[1];
    return LOOPBACK_PORT_MIN + (value % (LOOPBACK_PORT_MAX - LOOPBACK_PORT_MIN + 1));
}

async function reserveLoopbackPort(host, options = {}) {
    if (options.port !== undefined) {
        const port = validateLoopbackPort(options.port);
        const listener = await TcpListener.listen({ host, port, backlog: 1 });
        return { port, listener };
    }
    const attempts = options.portReservationAttempts ?? 64;
    for (let attempt = 0; attempt < attempts; attempt += 1) {
        const port = randomLoopbackPort();
        try {
            const listener = await TcpListener.listen({ host, port, backlog: 1 });
            return { port, listener };
        } catch {
            // A different process can own the sampled port; try another reserved candidate.
        }
    }
    throw new Error("Sloppy TestHost could not reserve an available loopback port.");
}

async function releaseLoopbackReservation(reservation) {
    if (reservation?.listener === undefined) {
        return;
    }
    await reservation.listener.close().catch(async () => {
        await reservation.listener.abort().catch(() => {});
    });
}

function loopbackAuthority(host, port) {
    return `${String(host).includes(":") ? `[${host}]` : host}:${port}`;
}

async function readProcessPipeText(pipe, maxBytes) {
    if (pipe === undefined || typeof pipe.readText !== "function") {
        return "";
    }
    try {
        return await pipe.readText(maxBytes);
    } catch {
        return "";
    }
}

async function processExitIfAvailable(child) {
    try {
        const result = await child.wait({ timeoutMs: 0 });
        return result?.timedOut === true ? undefined : result;
    } catch {
        return undefined;
    }
}

async function processOutputSnapshot(child, options = {}) {
    const maxStdoutBytes = options.maxStdoutBytes ?? 64 * 1024;
    const maxStderrBytes = options.maxStderrBytes ?? 64 * 1024;
    const [stdout, stderr] = await Promise.all([
        readProcessPipeText(child.stdout, maxStdoutBytes),
        readProcessPipeText(child.stderr, maxStderrBytes),
    ]);
    return { stdout, stderr };
}

function recordLoopbackStartupFailure(helpers, details) {
    helpers.diagnostics.record({
        code: "SLOPPY_E_TESTHOST_LOOPBACK_STARTUP",
        subsystem: "process",
        severity: "error",
        message: details.message,
        fields: {
            host: details.host,
            port: details.port,
            exitCode: details.exitCode,
            stdout: details.stdout,
            stderr: details.stderr,
        },
    });
}

function isRetryableLoopbackStartupFailure(error) {
    return /listen|bind|address|port|in use|EADDRINUSE|denied/iu.test(String(error?.message ?? error));
}

async function waitForLoopbackReady(host, port, child, helpers, options = {}) {
    const timeoutMs = options.startTimeoutMs ?? 10000;
    const stabilityMs = options.startStabilityMs ?? 250;
    const authority = loopbackAuthority(host, port);
    const startedAt = Date.now();
    while (Date.now() - startedAt < timeoutMs) {
        const exit = await processExitIfAvailable(child);
        if (exit !== undefined) {
            const output = await processOutputSnapshot(child, options);
            const message = `Sloppy TestHost loopback server exited before startup with exit code ${exit.exitCode}.`;
            recordLoopbackStartupFailure(helpers, {
                message,
                host,
                port,
                exitCode: exit.exitCode,
                ...output,
            });
            throw new Error(`${message}${output.stderr.length === 0 ? "" : `\n${output.stderr.trimEnd()}`}`);
        }
        try {
            const probe = await HttpClient.get(`http://${authority}/__sloppy_testhost_ready__`, {
                timeoutMs: 100,
            });
            if (probe.status < 100) {
                await new Promise((resolve) => setTimeout(resolve, 25));
                continue;
            }
        } catch {
            await new Promise((resolve) => setTimeout(resolve, 25));
            continue;
        }
        await new Promise((resolve) => setTimeout(resolve, stabilityMs));
        const stableExit = await processExitIfAvailable(child);
        if (stableExit !== undefined) {
            const output = await processOutputSnapshot(child, options);
            const message = `Sloppy TestHost loopback server exited during startup with exit code ${stableExit.exitCode}.`;
            recordLoopbackStartupFailure(helpers, {
                message,
                host,
                port,
                exitCode: stableExit.exitCode,
                ...output,
            });
            throw new Error(`${message}${output.stderr.length === 0 ? "" : `\n${output.stderr.trimEnd()}`}`);
        }
        return;
    }
    const output = await processOutputSnapshot(child, options);
    const message = "Sloppy TestHost loopback server did not start before timeout.";
    recordLoopbackStartupFailure(helpers, { message, host, port, ...output });
    throw new Error(`${message}${output.stderr.length === 0 ? "" : `\n${output.stderr.trimEnd()}`}`);
}

async function createProcessLoopbackHost(kind, targetPath, options = {}) {
    const command = cliPath(options);
    const helpers = createProcessHelpers(kind, targetPath, options);
    const host = options.host ?? "127.0.0.1";
    const startupAttempts = options.port === undefined ? (options.portReservationAttempts ?? 64) : 1;
    let port;
    let child;
    for (let attempt = 0; attempt < startupAttempts; attempt += 1) {
        let reservation;
        try {
            reservation = await reserveLoopbackPort(host, options);
        } catch (error) {
            const failedPort = options.port === undefined ? undefined : validateLoopbackPort(options.port);
            recordLoopbackStartupFailure(helpers, {
                message: `Sloppy TestHost loopback port reservation failed.${error?.message === undefined ? "" : ` ${error.message}`}`,
                host,
                port: failedPort,
            });
            throw error;
        }
        port = reservation.port;
        await releaseLoopbackReservation(reservation);
        child = await SloppyProcess.start(command, [
            ...pathRunArgs(kind, targetPath, "loopback"),
            "--host",
            host,
            "--port",
            String(port),
        ], {
            cwd: options.cwd,
            env: options.env,
            stdout: "pipe",
            stderr: "pipe",
        });
        try {
            await waitForLoopbackReady(host, port, child, helpers, options);
            break;
        } catch (error) {
            await child.terminate().catch(() => {});
            await child.wait({ timeoutMs: options.stopTimeoutMs ?? 5000 }).catch(() => {});
            await child.dispose().catch(() => {});
            child = undefined;
            if (options.port === undefined && attempt + 1 < startupAttempts && isRetryableLoopbackStartupFailure(error)) {
                continue;
            }
            throw error;
        }
    }
    if (child === undefined || port === undefined) {
        throw new Error("Sloppy TestHost loopback server did not start.");
    }
    let closed = false;

    const baseUrl = `http://${loopbackAuthority(host, port)}`;
    return Object.freeze({
        baseUrl,
        port,
        async request(method, target, requestOptions = undefined) {
            if (closed) {
                throw new Error("Sloppy TestHost loopback host is closed.");
            }
            const exit = await processExitIfAvailable(child);
            if (exit !== undefined) {
                const output = await processOutputSnapshot(child, options);
                helpers.diagnostics.record({
                    code: "SLOPPY_E_TESTHOST_LOOPBACK_EXITED",
                    subsystem: "process",
                    severity: "error",
                    message: `Sloppy TestHost loopback server exited with code ${exit.exitCode}.`,
                    fields: { exitCode: exit.exitCode, stdout: output.stdout, stderr: output.stderr },
                });
                throw new Error(`Sloppy TestHost loopback server exited with code ${exit.exitCode}.`);
            }
            const normalizedMethod = normalizeMethod(method);
            const normalizedOptions = normalizeOptions(requestOptions);
            const headerEntries = headerEntriesFromObject(normalizedOptions.headers, "request");
            const body = normalizeRequestBody(normalizedOptions, headerEntries);
            const requestHeaders = Object.fromEntries(
                headerEntries.filter(([name]) => name.toLowerCase() !== "content-length"),
            );
            const response = await HttpClient.request({
                url: `${baseUrl}${target}`,
                method: normalizedMethod,
                headers: requestHeaders,
                bytes: body.byteLength === 0 ? undefined : body,
                timeoutMs: normalizedOptions.timeoutMs ?? options.timeoutMs,
            });
            const testResponse = finalizeResponse(responseFromParts(response.status, responseHeaderEntries(response), await response.bytes()), normalizedMethod);
            helpers.metrics.increment("http.requests.total", { method: normalizedMethod, status: String(testResponse.status) });
            return testResponse;
        },
        async close() {
            if (closed) {
                return;
            }
            closed = true;
            await child.terminate().catch(() => {});
            await child.wait({ timeoutMs: options.stopTimeoutMs ?? 5000 }).catch(() => {});
            await child.dispose().catch(() => {});
        },
        websocketConnect: unsupportedProcessWebSocketConnect(helpers, "loopback"),
        ...helpers,
    });
}

function runtimeHostToFluent(runtimeHost, mode) {
    return Object.freeze({
        ...createFluentHost({
            request(method, target, options) {
                return runtimeHost.request(method, target, options);
            },
            websocketConnect(target, options) {
                if (typeof runtimeHost.websocketConnect === "function") {
                    return runtimeHost.websocketConnect(target, options);
                }
                return unsupportedProcessWebSocketConnect(runtimeHost, mode)(target, options);
            },
            close() {
                return runtimeHost.close?.();
            },
            diagnostics: runtimeHost.diagnostics,
            metrics: runtimeHost.metrics,
            jobs: runtimeHost.jobs,
            openapi: runtimeHost.openapi,
            baseUrl: runtimeHost.baseUrl,
            port: runtimeHost.port,
        }, mode),
        baseUrl: runtimeHost.baseUrl,
        port: runtimeHost.port,
    });
}

async function runtimeOrProcessHost(kind, targetPath, mode, options) {
    const bridgeName = kind === "artifacts"
        ? (mode === "loopback" ? "fromArtifactsLoopback" : "fromArtifacts")
        : (mode === "loopback" ? "fromPackageLoopback" : "fromPackage");
    const bridge = globalThis.__sloppy?.testHost;
    if (bridge !== undefined && typeof bridge[bridgeName] === "function") {
        return runtimeHostToFluent(await bridge[bridgeName](targetPath, options), mode);
    }
    const host = mode === "loopback"
        ? await createProcessLoopbackHost(kind, targetPath, options)
        : createProcessOnceHost(kind, targetPath, options);
    return runtimeHostToFluent(host, mode);
}

async function createArtifactHost(targetPath, options = {}) {
    if (typeof targetPath !== "string" || targetPath.length === 0) {
        throw new TypeError("Sloppy TestHost artifact/package path must be a non-empty string.");
    }
    const mode = options.mode ?? "inProcess";
    if (mode === "inProcess" || mode === "loopback") {
        return runtimeOrProcessHost("artifacts", targetPath, mode, options);
    }
    throw new Error(`Sloppy TestHost mode '${mode}' is not supported.`);
}

async function createPackageHost(targetPath, options = {}) {
    if (typeof targetPath !== "string" || targetPath.length === 0) {
        throw new TypeError("Sloppy TestHost artifact/package path must be a non-empty string.");
    }
    const mode = options.mode ?? "inProcess";
    if (mode === "inProcess" || mode === "loopback") {
        return runtimeOrProcessHost("package", targetPath, mode, options);
    }
    throw new Error(`Sloppy TestHost mode '${mode}' is not supported.`);
}

function ensureSupportedCreateMode(mode) {
    if (mode === "inProcess") {
        return;
    }
    if (mode === "loopback") {
        throw new Error("Sloppy TestHost loopback mode requires fromArtifacts() or fromPackage() so the real runtime server can start.");
    }
    throw new Error(`Sloppy TestHost mode '${mode}' is not supported.`);
}

const FakeClock = Object.freeze({
    fixed(value) {
        const start = value instanceof Date ? value.getTime() : Date.parse(String(value));
        if (!Number.isFinite(start)) {
            throw new TypeError("Sloppy FakeClock.fixed expects a Date or ISO timestamp.");
        }
        let current = start;
        return Object.freeze({
            now() {
                return new Date(current);
            },
            monotonicNowMs() {
                return current - start;
            },
            advanceBy(duration) {
                const ms = duration?.milliseconds ?? duration?.ms ??
                    (duration?.seconds ?? 0) * 1000 +
                    (duration?.minutes ?? 0) * 60 * 1000 +
                    (duration?.hours ?? 0) * 60 * 60 * 1000;
                if (!Number.isFinite(ms)) {
                    throw new TypeError("Sloppy FakeClock.advanceBy duration must be finite.");
                }
                current += ms;
            },
            delay(ms) {
                if (!Number.isFinite(ms)) {
                    throw new TypeError("Sloppy FakeClock.delay expects a finite millisecond value.");
                }
                current += ms;
                return Promise.resolve();
            },
        });
    },
});

const TestData = Object.freeze({
    sqliteMemory(options = {}) {
        return Object.freeze({
            kind: "sqlite",
            database: ":memory:",
            migrations: options.migrations,
            seed: options.seed,
            open() {
                const db = data.sqlite.open({ database: ":memory:" });
                return Promise.resolve()
                    .then(() => options.migrations === undefined ? undefined : Migrations.apply(db, {
                        provider: "sqlite",
                        path: options.migrations,
                    }))
                    .then(() => typeof options.seed === "function" ? options.seed(db) : undefined)
                    .then(() => db);
            },
        });
    },
    sqliteTempFile(options = {}) {
        return Object.freeze({
            kind: "sqlite",
            async open() {
                const directory = options.directory ?? ".sloppy/testhost";
                const dir = await Directory.createTemp(directory, { prefix: "sqlite-" });
                const database = `${dir.replace(/[\\/]$/u, "")}/test.db`;
                const db = data.sqlite.open({ database });
                const originalClose = db.close?.bind(db);
                return Object.freeze({
                    ...db,
                    close() {
                        originalClose?.();
                        return Directory.delete(dir, { recursive: true });
                    },
                });
            },
        });
    },
});

const TestHost = Object.freeze({
    async create(app, options = {}) {
        ensureSupportedCreateMode(options.mode ?? "inProcess");
        return createFluentHost(createTestHost(app, options), "inProcess");
    },
    fromArtifacts(pathValue, options = {}) {
        return createArtifactHost(pathValue, options);
    },
    fromPackage(pathValue, options = {}) {
        return createPackageHost(pathValue, options);
    },
});

const Testing = Object.freeze({
    createHost: createTestHost,
    TestHost,
    FakeClock,
    TestData,
});

export { createTestHost, FakeClock, TestData, TestHost, Testing };
