import { isPlainObject } from "./validation.js";

const HTTP_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;

export function isHeaderToken(value) {
    return typeof value === "string" && HTTP_TOKEN_PATTERN.test(value);
}

export function assertHeaderToken(value, subject) {
    if (!isHeaderToken(value)) {
        throw new TypeError(`${subject} header names must be safe HTTP tokens.`);
    }
    return value;
}

export function assertHeaderValue(value, subject) {
    if (typeof value !== "string" || /[\x00-\x08\x0A-\x1F\x7F]/u.test(value)) {
        throw new TypeError(`${subject} header values must be safe strings.`);
    }
    return value;
}

export function createHeaderLookup(headers = {}) {
    const values = new Map();
    if (typeof headers?.entries === "function") {
        for (const [name, value] of headers.entries()) {
            values.set(String(name).toLowerCase(), value);
        }
    } else if (isPlainObject(headers)) {
        for (const [name, value] of Object.entries(headers)) {
            values.set(name.toLowerCase(), value);
        }
    } else {
        throw new TypeError("Sloppy headers must be response headers or a plain object.");
    }
    return Object.freeze({
        get(name) {
            return values.get(String(name).toLowerCase());
        },
    });
}

export function headerValue(headers, name) {
    if (headers === undefined || headers === null) {
        return undefined;
    }
    if (typeof headers.get === "function") {
        return headers.get(name) ?? headers.get(String(name).toLowerCase()) ?? undefined;
    }
    return createHeaderLookup(headers).get(name);
}

export function appendVaryHeader(headers, value) {
    const current = headerValue(headers, "vary");
    if (current === undefined || String(current).trim().length === 0) {
        headers.Vary = value;
        return;
    }
    const values = current.split(",").map((part) => part.trim().toLowerCase());
    if (!values.includes(value.toLowerCase())) {
        headers.Vary = `${current}, ${value}`;
    }
}
