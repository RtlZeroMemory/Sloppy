import { isPlainObject } from "./shared.js";

const SECRET_REDACTION = "[REDACTED]";
const SECRET_KEY_PATTERN = /password|passwd|pwd|secret|token|authorization|cookie|set-cookie|apikey|clientsecret|privatekey|passphrase|connectionstring/iu;
const SENSITIVE_HEADER_PATTERN = /^(authorization|cookie|set-cookie|x-api-key|api-key)$/iu;
const SENSITIVE_QUERY_PATTERN = /(token|secret|password|authorization|api[_-]?key)/iu;

function boundedText(value, max = 12000, placement = "tail") {
    const text = String(value ?? "");
    if (text.length <= max) {
        return text;
    }
    return placement === "head" ? text.slice(0, max) : text.slice(text.length - max);
}

function redactTextSecrets(value, secrets, replacement = SECRET_REDACTION) {
    let text = String(value ?? "");
    for (const secret of secrets ?? []) {
        if (typeof secret === "string" && secret.length > 0) {
            text = text.replaceAll(secret, replacement);
        }
    }
    return text;
}

function redactHeaders(headers = {}, replacement = SECRET_REDACTION) {
    const output = {};
    for (const [name, value] of Object.entries(headers)) {
        output[name] = SENSITIVE_HEADER_PATTERN.test(name) ? replacement : value;
    }
    return Object.freeze(output);
}

function redactUrlTemplate(path, query = undefined, replacement = SECRET_REDACTION) {
    if (query === undefined || query === null) {
        return path;
    }
    const keys = Object.keys(query);
    if (keys.length === 0) {
        return path;
    }
    const rendered = keys
        .sort()
        .map((key) => `${encodeURIComponent(key)}=${SENSITIVE_QUERY_PATTERN.test(key) ? replacement : "{value}"}`)
        .join("&");
    return `${path}?${rendered}`;
}

function redactObject(value, options = undefined) {
    const replacement = options?.replacement ?? SECRET_REDACTION;
    const redactText = options?.redactText ?? ((text) => redactTextSecrets(text, options?.secrets, replacement));
    const stringifyPrimitives = options?.stringifyPrimitives === true;

    function visit(entry) {
        if (entry === null || entry === undefined) {
            return entry;
        }
        if (typeof entry === "string") {
            return redactText(entry);
        }
        if (typeof entry === "number" || typeof entry === "boolean") {
            return stringifyPrimitives ? redactText(entry) : entry;
        }
        if (Array.isArray(entry)) {
            return Object.freeze(entry.map(visit));
        }
        if (isPlainObject(entry)) {
            const safe = {};
            for (const [key, nested] of Object.entries(entry)) {
                safe[key] = SECRET_KEY_PATTERN.test(key) ? replacement : visit(nested);
            }
            return Object.freeze(safe);
        }
        return redactText(entry);
    }

    return visit(value);
}

export {
    SECRET_REDACTION,
    boundedText,
    redactHeaders,
    redactObject,
    redactTextSecrets,
    redactUrlTemplate,
};
