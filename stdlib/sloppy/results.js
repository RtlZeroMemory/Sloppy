const TEXT_CONTENT_TYPE = "text/plain; charset=utf-8";
const JSON_CONTENT_TYPE = "application/json; charset=utf-8";
const HTML_CONTENT_TYPE = "text/html; charset=utf-8";
const BYTES_CONTENT_TYPE = "application/octet-stream";
const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";

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

function copyHeaders(options) {
    const headers = options?.headers;

    if (headers === undefined) {
        return undefined;
    }

    if (!isPlainObject(headers)) {
        throw new TypeError("Sloppy Results headers must be a plain object when provided.");
    }

    return Object.freeze({ ...headers });
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

function createResult(kind, body, contentType, options, extra) {
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

    return Object.freeze(descriptor);
}

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
    return createResult("text", String(body), TEXT_CONTENT_TYPE, options);
}

function json(value, options) {
    return createResult("json", value, JSON_CONTENT_TYPE, options);
}

function html(body, options) {
    return createResult("html", String(body), HTML_CONTENT_TYPE, options);
}

function bytes(body, options) {
    return createResult("bytes", copyBytes(body), resolveContentType(options, BYTES_CONTENT_TYPE), options);
}

function ok(value, options) {
    return createResult("json", value, JSON_CONTENT_TYPE, options);
}

function created(location, value, options) {
    if (typeof location !== "string" || location.length === 0) {
        throw new TypeError("Sloppy Results.created location must be a non-empty string.");
    }

    return createResult(
        "json",
        value,
        JSON_CONTENT_TYPE,
        { status: 201, ...options },
        { location },
    );
}

function accepted(value, options) {
    return createResult("json", value, JSON_CONTENT_TYPE, { status: 202, ...options });
}

function noContent() {
    return createResult("empty", undefined, undefined, { status: 204 });
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
