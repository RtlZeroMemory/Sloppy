import { copyUint8Array } from "./bytes.js";
import {
    assertHeaderToken as assertSharedHeaderToken,
    assertHeaderValue as assertSharedHeaderValue,
} from "./headers.js";
import { isPlainObject, requirePlainObject } from "./validation.js";

export function assertHeaderName(name, subject) {
    assertSharedHeaderToken(name, `Sloppy test host ${subject}`);
}

export function assertHeaderValue(value, subject) {
    assertSharedHeaderValue(value, `Sloppy test host ${subject}`);
}

export function copyBytes(value, subject) {
    const bytes = copyUint8Array(value);
    if (bytes === undefined) {
        throw new TypeError(`Sloppy test host ${subject} must be a Uint8Array, ArrayBuffer, or ArrayBuffer view.`);
    }
    return bytes;
}

export function headerEntriesFromObject(headers, subject) {
    if (headers === undefined) {
        return [];
    }

    requirePlainObject(headers, `Sloppy test host ${subject} headers must be a plain object.`);

    const entries = [];
    for (const [name, value] of Object.entries(headers)) {
        assertHeaderName(name, subject);
        assertHeaderValue(value, subject);
        entries.push([name, value]);
    }
    return entries;
}

export function normalizeHeaderEntries(entries) {
    const normalized = [];
    const indexes = new Map();
    for (const [name, value] of entries) {
        const lower = name.toLowerCase();
        if (lower === "set-cookie") {
            normalized.push([lower, value]);
            continue;
        }
        const existingIndex = indexes.get(lower);
        if (existingIndex === undefined) {
            indexes.set(lower, normalized.length);
            normalized.push([lower, value]);
        } else {
            normalized[existingIndex][1] = `${normalized[existingIndex][1]}, ${value}`;
        }
    }
    return normalized;
}

export function createHeadersLike(entries) {
    const normalized = Object.freeze(normalizeHeaderEntries(entries).map(([name, value]) => Object.freeze([name, value])));
    const byName = new Map();
    for (const [name, value] of normalized) {
        if (!byName.has(name)) {
            byName.set(name, value);
        }
    }
    return Object.freeze({
        get(name) {
            if (typeof name !== "string") {
                throw new TypeError("Sloppy test host headers.get name must be a string.");
            }
            return byName.get(name.toLowerCase());
        },
        entries() {
            return normalized[Symbol.iterator]();
        },
        [Symbol.iterator]() {
            return normalized[Symbol.iterator]();
        },
    });
}

export function headersToEntries(headers) {
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

export function responseHeaderEntries(response, omitEntityHeaders = false) {
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

export function setDefaultHeader(entries, name, value) {
    if (!hasHeader(entries, name)) {
        entries.push([name, value]);
    }
}
