import { Text } from "./codec.js";

const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
const BYTES_CONTENT_TYPE = "application/octet-stream";
const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";
const STREAM_CONTENT_TYPE = "application/octet-stream";
const STREAM_MAX_CHUNK_BYTES = 65536;
const STREAM_MAX_TOTAL_BYTES = 131072;
const FAST_RESULT_KIND = "__sloppyFastResult";
const FAST_JSON_TEXT = "__sloppyJsonText";
const RAW_JSON_BODY = "__sloppyRawJsonBody";
const FAST_TEXT_OK = 1;
const FAST_NO_CONTENT = 2;
const FAST_JSON = 3;
const FAST_CREATED = 4;
const FAST_JSON_MAX_LENGTH = 256;
const DEFAULT_JSON_OPTIONS = Object.freeze({
    casing: "preserve",
    includeNulls: true,
    dateFormat: "iso8601",
    bigint: "string",
    bytes: "base64",
});

function resolveStatus(options) {
    const status = options?.status ?? 200;

    if (!Number.isInteger(status) || status < 100 || status > 999) {
        throw new TypeError("Sloppy Results status must be an integer from 100 to 999.");
    }

    return status;
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function normalizeJsonOptions(options = undefined) {
    if (options === undefined) {
        return DEFAULT_JSON_OPTIONS;
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy JSON options must be a plain object.");
    }

    const normalized = {
        casing: options.casing !== undefined
            ? options.casing
            : DEFAULT_JSON_OPTIONS.casing,
        includeNulls: options.includeNulls !== undefined
            ? options.includeNulls
            : DEFAULT_JSON_OPTIONS.includeNulls,
        dateFormat: options.dateFormat !== undefined
            ? options.dateFormat
            : DEFAULT_JSON_OPTIONS.dateFormat,
        bigint: options.bigint !== undefined
            ? options.bigint
            : DEFAULT_JSON_OPTIONS.bigint,
        bytes: options.bytes !== undefined
            ? options.bytes
            : DEFAULT_JSON_OPTIONS.bytes,
    };

    if (normalized.casing !== "preserve" && normalized.casing !== "camelCase") {
        throw new TypeError("Sloppy JSON casing must be preserve or camelCase.");
    }
    if (typeof normalized.includeNulls !== "boolean") {
        throw new TypeError("Sloppy JSON includeNulls must be a boolean.");
    }
    if (normalized.dateFormat !== "iso8601") {
        throw new TypeError("Sloppy JSON dateFormat currently supports iso8601.");
    }
    if (normalized.bigint !== "string" && normalized.bigint !== "error") {
        throw new TypeError("Sloppy JSON bigint must be string or error.");
    }
    if (normalized.bytes !== "base64" && normalized.bytes !== "array") {
        throw new TypeError("Sloppy JSON bytes must be base64 or array.");
    }

    return Object.freeze(normalized);
}

function jsonKey(key, options) {
    if (options.casing !== "camelCase") {
        return key;
    }
    return key.replace(/[-_]+([A-Za-z0-9])/gu, (_, ch) => ch.toUpperCase());
}

function base64Encode(bytes) {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let output = "";
    for (let index = 0; index < bytes.byteLength; index += 3) {
        const a = bytes[index];
        const b = index + 1 < bytes.byteLength ? bytes[index + 1] : 0;
        const c = index + 2 < bytes.byteLength ? bytes[index + 2] : 0;
        const combined = (a << 16) | (b << 8) | c;
        output += alphabet[(combined >> 18) & 63];
        output += alphabet[(combined >> 12) & 63];
        output += index + 1 < bytes.byteLength ? alphabet[(combined >> 6) & 63] : "=";
        output += index + 2 < bytes.byteLength ? alphabet[combined & 63] : "=";
    }
    return output;
}

function bytesView(value) {
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
    return undefined;
}

function defineJsonProperty(output, key, value) {
    Object.defineProperty(output, key, {
        value,
        enumerable: true,
        configurable: true,
        writable: true,
    });
}

function normalizeErrorObject(error, options, seen) {
    const output = {};
    defineJsonProperty(output, "name", typeof error.name === "string" && error.name.length > 0 ? error.name : "Error");
    defineJsonProperty(output, "message", String(error.message ?? ""));
    for (const [key, nested] of Object.entries(error)) {
        if (key === "name" || key === "message" || key === "stack") {
            continue;
        }
        const value = normalizeJsonValue(nested, options, seen, key);
        if (value !== undefined && (value !== null || options.includeNulls)) {
            defineJsonProperty(output, jsonKey(key, options), value);
        }
    }
    return Object.freeze(output);
}

function normalizeJsonValue(value, options, seen, path) {
    if (value === undefined) {
        return undefined;
    }
    if (typeof value === "function" || typeof value === "symbol") {
        throw new TypeError(`Sloppy JSON cannot serialize ${typeof value} at ${path}.`);
    }
    if (value === null || typeof value === "string" || typeof value === "boolean") {
        return value;
    }
    if (typeof value === "number") {
        if (!Number.isFinite(value)) {
            throw new TypeError(`Sloppy JSON numbers must be finite at ${path}.`);
        }
        return value;
    }
    if (typeof value === "bigint") {
        if (options.bigint === "error") {
            throw new TypeError(`Sloppy JSON cannot serialize BigInt at ${path}.`);
        }
        return value.toString();
    }
    if (value instanceof Date) {
        const timestamp = value.getTime();
        if (!Number.isFinite(timestamp)) {
            throw new TypeError(`Sloppy JSON cannot serialize invalid Date at ${path}.`);
        }
        return value.toISOString();
    }

    const bytes = bytesView(value);
    if (bytes !== undefined) {
        return options.bytes === "array" ? Object.freeze(Array.from(bytes)) : base64Encode(bytes);
    }

    if (value !== null && typeof value === "object") {
        if (seen.has(value)) {
            throw new TypeError(`Sloppy JSON cannot serialize circular reference at ${path}.`);
        }
        seen.add(value);
        try {
            if (Array.isArray(value)) {
                return Object.freeze(value.map((item, index) => {
                    const itemValue = normalizeJsonValue(item, options, seen, `${path}[${index}]`);
                    return itemValue === undefined ? null : itemValue;
                }));
            }
            if (value instanceof Error) {
                return normalizeErrorObject(value, options, seen);
            }

            const output = {};
            for (const [key, nested] of Object.entries(value)) {
                const normalized = normalizeJsonValue(nested, options, seen, `${path}.${key}`);
                if (normalized === undefined || (normalized === null && !options.includeNulls)) {
                    continue;
                }
                defineJsonProperty(output, jsonKey(key, options), normalized);
            }
            return Object.freeze(output);
        } finally {
            seen.delete(value);
        }
    }

    throw new TypeError(`Sloppy JSON cannot serialize ${typeof value} at ${path}.`);
}

function normalizeJsonBody(value, options = undefined) {
    if (value === undefined) {
        return null;
    }
    return normalizeJsonValue(value, normalizeJsonOptions(options), new Set(), "$");
}

function normalizeJsonDescriptorBody(value, options = undefined) {
    return value === undefined ? undefined : normalizeJsonBody(value, options);
}

function serializeJson(value, options = undefined) {
    const normalized = normalizeJsonBody(value, options);
    return JSON.stringify(normalized);
}

function isHeaderNameChar(value) {
    return (value >= "A" && value <= "Z") ||
        (value >= "a" && value <= "z") ||
        (value >= "0" && value <= "9") ||
        value === "!" ||
        value === "#" ||
        value === "$" ||
        value === "%" ||
        value === "&" ||
        value === "'" ||
        value === "*" ||
        value === "+" ||
        value === "-" ||
        value === "." ||
        value === "^" ||
        value === "_" ||
        value === "`" ||
        value === "|" ||
        value === "~";
}

function isHeaderNameSafe(name) {
    if (name.length === 0) {
        return false;
    }

    for (const ch of name) {
        if (!isHeaderNameChar(ch)) {
            return false;
        }
    }

    return true;
}

function isManagedResponseHeader(name) {
    const lowered = name.toLowerCase();
    return lowered === "connection" ||
        lowered === "content-type" ||
        lowered === "content-length" ||
        lowered === "transfer-encoding" ||
        lowered === "keep-alive";
}

function isHeaderValueSafe(value) {
    for (let index = 0; index < value.length; index += 1) {
        const code = value.charCodeAt(index);
        if ((code < 0x20 && code !== 0x09) || code === 0x7F) {
            return false;
        }
    }

    return true;
}

function assertHeaderValueSafe(value, label) {
    if (typeof value !== "string" || !isHeaderValueSafe(value)) {
        throw new TypeError(`Sloppy Results ${label} must be a safe HTTP header value.`);
    }
}

function assertCookieAttributeValueSafe(value, label) {
    assertHeaderValueSafe(value, label);
    if (value.includes(";")) {
        throw new TypeError(`Sloppy Results ${label} must not contain ';'.`);
    }
}

function assertCookieValueSafe(value, label) {
    if (typeof value !== "string" || /[\x00-\x20\x7F;,]/u.test(value)) {
        throw new TypeError(`Sloppy Results ${label} must be a safe Set-Cookie value.`);
    }
}

function copyHeaders(options) {
    const headers = options?.headers;

    if (headers === undefined) {
        return undefined;
    }

    if (!isPlainObject(headers)) {
        throw new TypeError("Sloppy Results headers must be a plain object when provided.");
    }

    const copied = {};
    for (const [name, value] of Object.entries(headers)) {
        if (!isHeaderNameSafe(name) || isManagedResponseHeader(name)) {
            throw new TypeError("Sloppy Results headers must use safe unmanaged HTTP header names.");
        }
        assertHeaderValueSafe(value, `header '${name}'`);
        Object.defineProperty(copied, name, {
            value,
            enumerable: true,
            writable: true,
            configurable: true,
        });
    }

    return Object.freeze(copied);
}

function copySetCookies(options) {
    const setCookies = options?.setCookies;

    if (setCookies === undefined) {
        return undefined;
    }
    if (!Array.isArray(setCookies)) {
        throw new TypeError("Sloppy Results setCookies must be an array when provided.");
    }

    return Object.freeze(setCookies.map((value) => {
        if (typeof value !== "string") {
            throw new TypeError("Sloppy Results setCookies entries must be strings.");
        }
        assertHeaderValueSafe(value, "Set-Cookie");
        return value;
    }));
}

function copyBytes(value) {
    if (value instanceof ArrayBuffer) {
        return new Uint8Array(value.slice(0));
    }

    if (ArrayBuffer.isView(value)) {
        const storage = value["buf" + "fer"];
        return new Uint8Array(storage.slice(value.byteOffset, value.byteOffset + value.byteLength));
    }

    throw new TypeError("Sloppy Results.bytes body must be binary data or a typed array view.");
}

function resolveContentType(options, defaultContentType) {
    const contentType = options?.contentType ?? defaultContentType;

    if (typeof contentType !== "string" || contentType.length === 0) {
        throw new TypeError("Sloppy Results contentType must be a non-empty string.");
    }

    if (/[\x00-\x1F\x7F]/.test(contentType)) {
        throw new TypeError("Sloppy Results contentType must not contain control characters.");
    }

    return contentType;
}

function maybeFastJsonText(body) {
    if (body !== null && typeof body === "object") {
        return undefined;
    }

    let jsonText;

    try {
        jsonText = serializeJson(body);
    } catch {
        return undefined;
    }

    return typeof jsonText === "string" && jsonText.length <= FAST_JSON_MAX_LENGTH
        ? jsonText
        : undefined;
}

function createResult(kind, body, contentType, options, extra, fast, rawJsonBody) {
    const setCookies = copySetCookies(options);
    const descriptor = {
        __sloppyResult: true,
        kind,
        status: resolveStatus(options),
        contentType,
        headers: copyHeaders(options),
        ...extra,
    };

    if (setCookies !== undefined) {
        descriptor.setCookies = setCookies;
    }

    if (body !== undefined) {
        descriptor.body = body;
    }

    if (fast !== undefined) {
        Object.defineProperty(descriptor, FAST_RESULT_KIND, {
            value: fast.kind,
        });
        if (fast.jsonText !== undefined) {
            Object.defineProperty(descriptor, FAST_JSON_TEXT, {
                value: fast.jsonText,
            });
        }
    }

    if (rawJsonBody !== undefined) {
        Object.defineProperty(descriptor, RAW_JSON_BODY, {
            value: rawJsonBody,
        });
    }
    if (options?.json !== undefined) {
        Object.defineProperty(descriptor, "json", {
            value: normalizeJsonOptions(options.json),
        });
    }

    Object.defineProperty(descriptor, "cookie", {
        value(name, value, cookieOptions) {
            return withCookie(descriptor, name, value, cookieOptions);
        },
    });

    return Object.freeze(descriptor);
}

function encodeCookieValue(value) {
    try {
        return encodeURIComponent(String(value));
    } catch {
        throw new TypeError("Sloppy Results cookie value must be safe.");
    }
}

function normalizeSameSite(value) {
    if (value === undefined) {
        return undefined;
    }
    if (typeof value !== "string") {
        throw new TypeError("Sloppy Results cookie sameSite must be lax, strict, or none.");
    }
    const lowered = value.toLowerCase();
    if (lowered === "lax") {
        return "Lax";
    }
    if (lowered === "strict") {
        return "Strict";
    }
    if (lowered === "none") {
        return "None";
    }
    throw new TypeError("Sloppy Results cookie sameSite must be lax, strict, or none.");
}

function buildSetCookie(name, value, options = undefined) {
    if (typeof name !== "string" || !isHeaderNameSafe(name)) {
        throw new TypeError("Sloppy Results cookie name must be a safe HTTP token.");
    }
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Results cookie options must be a plain object.");
    }

    const encodedValue = encodeCookieValue(value);
    assertCookieValueSafe(encodedValue, "cookie value");
    const parts = [`${name}=${encodedValue}`];

    if (options?.path !== undefined) {
        assertCookieAttributeValueSafe(options.path, "cookie path");
        parts.push(`Path=${options.path}`);
    }
    if (options?.domain !== undefined) {
        assertCookieAttributeValueSafe(options.domain, "cookie domain");
        parts.push(`Domain=${options.domain}`);
    }
    const maxAgeSeconds = options?.maxAgeSeconds ?? options?.maxAge;
    if (maxAgeSeconds !== undefined) {
        if (!Number.isInteger(maxAgeSeconds)) {
            throw new TypeError("Sloppy Results cookie maxAgeSeconds must be an integer.");
        }
        parts.push(`Max-Age=${maxAgeSeconds}`);
    }
    if (options?.expires !== undefined) {
        const expires = options.expires instanceof Date ? options.expires.toUTCString() : String(options.expires);
        assertCookieAttributeValueSafe(expires, "cookie expires");
        parts.push(`Expires=${expires}`);
    }
    const sameSite = normalizeSameSite(options?.sameSite);
    if (sameSite !== undefined) {
        parts.push(`SameSite=${sameSite}`);
    }
    if (options?.httpOnly === true) {
        parts.push("HttpOnly");
    }
    if (options?.secure === true) {
        parts.push("Secure");
    }

    return parts.join("; ");
}

function withCookie(descriptor, name, value, options) {
    const existing = Array.isArray(descriptor.setCookies) ? descriptor.setCookies : [];
    const setCookies = Object.freeze([...existing, buildSetCookie(name, value, options)]);
    const extra = {};
    if (descriptor.location !== undefined) {
        extra.location = descriptor.location;
    }
    if (descriptor.chunks !== undefined) {
        extra.chunks = Object.freeze([...descriptor.chunks]);
    }
    const rawJsonBody = Object.prototype.hasOwnProperty.call(descriptor, RAW_JSON_BODY)
        ? descriptor[RAW_JSON_BODY]
        : undefined;
    return createResult(
        descriptor.kind,
        descriptor.body,
        descriptor.contentType,
        {
            status: descriptor.status,
            headers: descriptor.headers,
            json: descriptor.json,
            setCookies,
        },
        Object.keys(extra).length === 0 ? undefined : extra,
        undefined,
        rawJsonBody,
    );
}

const TEXT_OK_RESULT = createResult("text", "ok", TEXT_CONTENT_TYPE, undefined, undefined, {
    kind: FAST_TEXT_OK,
});
const NO_CONTENT_RESULT = createResult("empty", undefined, undefined, { status: 204 }, undefined, {
    kind: FAST_NO_CONTENT,
});

function normalizeProblem(problemOrMessage, status) {
    if (problemOrMessage === undefined) {
        return Object.freeze({
            title: "Sloppy problem",
            status,
        });
    }

    if (typeof problemOrMessage === "string") {
        return Object.freeze({
            title: problemOrMessage,
            status,
        });
    }

    if (problemOrMessage === null || typeof problemOrMessage !== "object" || Array.isArray(problemOrMessage)) {
        throw new TypeError("Sloppy Results.problem value must be a string or plain problem object.");
    }

    return Object.freeze({
        status,
        ...problemOrMessage,
    });
}

function text(body, options) {
    const value = String(body);
    if (options === undefined && value === "ok") {
        return TEXT_OK_RESULT;
    }
    return createResult("text", value, TEXT_CONTENT_TYPE, options);
}

function json(value, options) {
    const body = normalizeJsonDescriptorBody(value, options?.json);
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
    return createResult(
        "json",
        body,
        JSON_CONTENT_TYPE,
        options,
        undefined,
        jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
        value,
    );
}

function html(body, options) {
    return createResult("html", String(body), HTML_CONTENT_TYPE, options);
}

function bytes(body, options) {
    return createResult("bytes", copyBytes(body), resolveContentType(options, BYTES_CONTENT_TYPE), options);
}

async function stream(callback, options) {
    if (typeof callback !== "function") {
        throw new TypeError("Sloppy Results.stream callback must be a function.");
    }
    const chunks = [];
    let totalBytes = 0;
    let closed = false;
    function appendChunk(chunk) {
        if (chunk.byteLength > STREAM_MAX_CHUNK_BYTES) {
            throw new TypeError("Sloppy Results.stream chunk exceeds the bounded stream limit.");
        }
        if (totalBytes + chunk.byteLength > STREAM_MAX_TOTAL_BYTES) {
            throw new TypeError("Sloppy Results.stream body exceeds the bounded stream limit.");
        }
        totalBytes += chunk.byteLength;
        chunks.push(chunk);
    }
    const writer = Object.freeze({
        writeText(text) {
            if (closed) {
                throw new TypeError("Sloppy stream writer is closed.");
            }
            appendChunk(Text.utf8.encode(String(text)));
        },
        writeBytes(bytes) {
            if (closed) {
                throw new TypeError("Sloppy stream writer is closed.");
            }
            appendChunk(copyBytes(bytes));
        },
        close() {
            closed = true;
        },
    });
    try {
        await callback(writer);
    } finally {
        closed = true;
    }
    return createResult(
        "stream",
        Object.freeze(chunks),
        resolveContentType(options, STREAM_CONTENT_TYPE),
        options,
        { chunks: Object.freeze(chunks) },
    );
}

function ok(value, options) {
    const body = normalizeJsonDescriptorBody(value, options?.json);
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
    return createResult(
        "json",
        body,
        JSON_CONTENT_TYPE,
        options,
        undefined,
        jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
        value,
    );
}

function created(location, value, options) {
    if (typeof location !== "string" || location.length === 0) {
        throw new TypeError("Sloppy Results.created location must be a non-empty string.");
    }
    assertHeaderValueSafe(location, "created location");

    const mergedOptions = { status: 201, ...options };
    const body = normalizeJsonDescriptorBody(value, options?.json);
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;

    return createResult(
        "json",
        body,
        JSON_CONTENT_TYPE,
        mergedOptions,
        { location },
        jsonText === undefined ? undefined : { kind: FAST_CREATED, jsonText },
        value,
    );
}

function accepted(value, options) {
    return createResult(
        "json",
        normalizeJsonDescriptorBody(value, options?.json),
        JSON_CONTENT_TYPE,
        { status: 202, ...options },
        undefined,
        undefined,
        value,
    );
}

function noContent() {
    return NO_CONTENT_RESULT;
}

function notFound(valueOrProblem, options) {
    return createResult(
        "json",
        normalizeJsonDescriptorBody(valueOrProblem, options?.json),
        JSON_CONTENT_TYPE,
        { status: 404, ...options },
        undefined,
        undefined,
        valueOrProblem,
    );
}

function badRequest(valueOrProblem, options) {
    return createResult(
        "json",
        normalizeJsonDescriptorBody(valueOrProblem, options?.json),
        JSON_CONTENT_TYPE,
        { status: 400, ...options },
        undefined,
        undefined,
        valueOrProblem,
    );
}

function unauthorized(valueOrProblem, options) {
    return createResult(
        "json",
        normalizeJsonDescriptorBody(valueOrProblem, options?.json),
        JSON_CONTENT_TYPE,
        { status: 401, ...options },
        undefined,
        undefined,
        valueOrProblem,
    );
}

function status(statusCode, value, options) {
    if (value === undefined) {
        return createResult("empty", undefined, undefined, { ...options, status: statusCode });
    }

    return createResult(
        "json",
        normalizeJsonDescriptorBody(value, options?.json),
        JSON_CONTENT_TYPE,
        { ...options, status: statusCode },
        undefined,
        undefined,
        value,
    );
}

function problem(problemOrMessage, options) {
    const status = resolveStatus({ status: 500, ...options });
    const problemBody = normalizeProblem(problemOrMessage, status);
    const body = normalizeJsonDescriptorBody(problemBody, options?.json);
    return createResult(
        "problem",
        body,
        PROBLEM_CONTENT_TYPE,
        { ...options, status },
        undefined,
        undefined,
        problemBody,
    );
}

export const Results = Object.freeze({
    ok,
    created,
    accepted,
    noContent,
    notFound,
    badRequest,
    unauthorized,
    status,
    problem,
    text,
    json,
    html,
    bytes,
    stream,
});

export {
    DEFAULT_JSON_OPTIONS,
    normalizeJsonBody,
    normalizeJsonOptions,
    RAW_JSON_BODY,
    serializeJson,
};
