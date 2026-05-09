const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
const BYTES_CONTENT_TYPE = "application/octet-stream";
const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";
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
    const descriptor = {
        __sloppyResult: true,
        kind,
        status: resolveStatus(options),
        contentType,
        headers: copyHeaders(options),
        ...extra,
    };

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

    return Object.freeze(descriptor);
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
    status,
    problem,
    text,
    json,
    html,
    bytes,
});
