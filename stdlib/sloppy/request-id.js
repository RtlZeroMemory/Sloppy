import { isPlainObject } from "./internal/shared.js";

const DEFAULT_HEADER = "x-request-id";
const MANAGED_RESPONSE_HEADERS = new Set([
    "connection",
    "content-type",
    "content-length",
    "transfer-encoding",
    "keep-alive",
]);

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

function headerNameSafe(name) {
    if (typeof name !== "string" || name.length === 0) {
        return false;
    }
    for (const ch of name) {
        if (!isHeaderNameChar(ch)) {
            return false;
        }
    }
    return true;
}

function responseHeaderAllowed(name) {
    return !MANAGED_RESPONSE_HEADERS.has(name.toLowerCase());
}

function headerValueSafe(value) {
    if (typeof value !== "string" || value.trim().length === 0) {
        return false;
    }
    for (let index = 0; index < value.length; index += 1) {
        const code = value.charCodeAt(index);
        if ((code < 0x20 && code !== 0x09) || code === 0x7F) {
            return false;
        }
    }
    return true;
}

function requestHeader(context, name) {
    const headers = context?.request?.headers;
    if (headers === undefined || headers === null) {
        return undefined;
    }
    if (typeof headers.get === "function") {
        return headers.get(name) ?? headers.get(name.toLowerCase()) ?? undefined;
    }
    if (isPlainObject(headers)) {
        const lowered = name.toLowerCase();
        for (const [key, value] of Object.entries(headers)) {
            if (key.toLowerCase() === lowered) {
                return value;
            }
        }
    }
    return undefined;
}

function appendResponseHeader(result, header, requestId) {
    if (result === null || typeof result !== "object") {
        return result;
    }

    const headers = {
        ...(isPlainObject(result.headers) ? result.headers : {}),
        [header]: requestId,
    };

    return Object.freeze({
        ...result,
        headers: Object.freeze(headers),
    });
}

function normalizeOptions(options) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy RequestId.defaults options must be a plain object.");
    }

    const header = options?.header ?? DEFAULT_HEADER;
    if (!headerNameSafe(header) || !responseHeaderAllowed(header)) {
        throw new TypeError("Sloppy RequestId header must be a safe unmanaged HTTP header name.");
    }

    const responseHeader = options?.responseHeader ?? true;
    if (typeof responseHeader !== "boolean") {
        throw new TypeError("Sloppy RequestId responseHeader option must be a boolean.");
    }

    const trustIncoming = options?.trustIncoming ?? false;
    if (typeof trustIncoming !== "boolean") {
        throw new TypeError("Sloppy RequestId trustIncoming option must be a boolean.");
    }

    const generator = options?.generator;
    if (generator !== undefined && typeof generator !== "function") {
        throw new TypeError("Sloppy RequestId generator option must be a function.");
    }

    return Object.freeze({
        header,
        responseHeader,
        trustIncoming,
        generator,
    });
}

function defaults(options) {
    const normalized = normalizeOptions(options);
    let counter = 0;
    const generate = normalized.generator ?? (() => {
        counter += 1;
        return `req-${counter}`;
    });

    async function requestIdMiddleware(context, next) {
        const incoming = normalized.trustIncoming
            ? requestHeader(context, normalized.header)
            : undefined;
        const requestId = headerValueSafe(incoming) ? incoming : generate();

        if (!headerValueSafe(requestId)) {
            throw new TypeError("Sloppy RequestId generator must return a safe non-empty value.");
        }

        Object.defineProperty(context, "requestId", {
            value: requestId,
            enumerable: true,
            writable: true,
            configurable: true,
        });

        const result = await next();
        return normalized.responseHeader
            ? appendResponseHeader(result, normalized.header, requestId)
            : result;
    }

    return Object.freeze(requestIdMiddleware);
}

export const RequestId = Object.freeze({
    defaults,
});
