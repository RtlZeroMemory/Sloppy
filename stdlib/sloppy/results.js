import { Text } from "./codec.js";

const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
const BYTES_CONTENT_TYPE = "application/octet-stream";
const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";
const STREAM_CONTENT_TYPE = "application/octet-stream";
const FAST_RESULT_KIND = "__sloppyFastResult";
const FAST_JSON_TEXT = "__sloppyJsonText";
const FAST_TEXT_OK = 1;
const FAST_NO_CONTENT = 2;
const FAST_JSON = 3;
const FAST_CREATED = 4;
const FAST_JSON_MAX_LENGTH = 256;

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
        jsonText = body === undefined ? "null" : JSON.stringify(body);
    } catch {
        return undefined;
    }

    return typeof jsonText === "string" && jsonText.length <= FAST_JSON_MAX_LENGTH
        ? jsonText
        : undefined;
}

function createResult(kind, body, contentType, options, extra, fast) {
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

    Object.defineProperty(descriptor, "cookie", {
        value(name, value, cookieOptions) {
            return withCookie(descriptor, name, value, cookieOptions);
        },
    });

    return Object.freeze(descriptor);
}

function encodeCookieValue(value) {
    return encodeURIComponent(String(value));
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
        assertHeaderValueSafe(options.path, "cookie path");
        parts.push(`Path=${options.path}`);
    }
    if (options?.domain !== undefined) {
        assertHeaderValueSafe(options.domain, "cookie domain");
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
        assertHeaderValueSafe(expires, "cookie expires");
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
    return createResult(
        descriptor.kind,
        descriptor.body,
        descriptor.contentType,
        {
            status: descriptor.status,
            headers: descriptor.headers,
            setCookies,
        },
        descriptor.location === undefined ? undefined : { location: descriptor.location },
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
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
    return createResult(
        "json",
        value,
        JSON_CONTENT_TYPE,
        options,
        undefined,
        jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
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
    let closed = false;
    const writer = Object.freeze({
        writeText(text) {
            if (closed) {
                throw new TypeError("Sloppy stream writer is closed.");
            }
            chunks.push(Text.utf8.encode(String(text)));
        },
        writeBytes(bytes) {
            if (closed) {
                throw new TypeError("Sloppy stream writer is closed.");
            }
            chunks.push(copyBytes(bytes));
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
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;
    return createResult(
        "json",
        value,
        JSON_CONTENT_TYPE,
        options,
        undefined,
        jsonText === undefined ? undefined : { kind: FAST_JSON, jsonText },
    );
}

function created(location, value, options) {
    if (typeof location !== "string" || location.length === 0) {
        throw new TypeError("Sloppy Results.created location must be a non-empty string.");
    }
    assertHeaderValueSafe(location, "created location");

    const mergedOptions = { status: 201, ...options };
    const jsonText = options === undefined ? maybeFastJsonText(value) : undefined;

    return createResult(
        "json",
        value,
        JSON_CONTENT_TYPE,
        mergedOptions,
        { location },
        jsonText === undefined ? undefined : { kind: FAST_CREATED, jsonText },
    );
}

function accepted(value, options) {
    return createResult("json", value, JSON_CONTENT_TYPE, { status: 202, ...options });
}

function noContent() {
    return NO_CONTENT_RESULT;
}

function notFound(valueOrProblem, options) {
    return createResult("json", valueOrProblem, JSON_CONTENT_TYPE, { status: 404, ...options });
}

function badRequest(valueOrProblem, options) {
    return createResult("json", valueOrProblem, JSON_CONTENT_TYPE, { status: 400, ...options });
}

function unauthorized(valueOrProblem, options) {
    return createResult("json", valueOrProblem, JSON_CONTENT_TYPE, { status: 401, ...options });
}

function status(statusCode, value, options) {
    if (value === undefined) {
        return createResult("empty", undefined, undefined, { ...options, status: statusCode });
    }

    return createResult("json", value, JSON_CONTENT_TYPE, { ...options, status: statusCode });
}

function problem(problemOrMessage, options) {
    const status = resolveStatus({ status: 500, ...options });
    return createResult(
        "problem",
        normalizeProblem(problemOrMessage, status),
        PROBLEM_CONTENT_TYPE,
        { ...options, status },
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
