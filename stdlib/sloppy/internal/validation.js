import { isPlainObject } from "./shared.js";

const HTTP_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;

function requirePlainObject(value, message) {
    if (!isPlainObject(value)) {
        throw new TypeError(message);
    }
    return value;
}

function requireNonEmptyString(value, message) {
    if (typeof value !== "string" || value.length === 0) {
        throw new TypeError(message);
    }
    return value;
}

function optionalBoolean(value, message, defaultValue) {
    if (value === undefined) {
        return defaultValue;
    }
    if (typeof value !== "boolean") {
        throw new TypeError(message);
    }
    return value;
}

function optionalInteger(value, message, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value)) {
        throw new TypeError(message);
    }
    return value;
}

function optionalPositiveInteger(value, message, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value) || value <= 0) {
        throw new TypeError(message);
    }
    return value;
}

function optionalNonNegativeInteger(value, message, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value) || value < 0) {
        throw new TypeError(message);
    }
    return value;
}

function requirePositiveFiniteNumber(value, message) {
    if (!Number.isFinite(value) || value < 1) {
        throw new TypeError(message);
    }
    return Math.ceil(value);
}

function isHttpToken(value) {
    return typeof value === "string" && HTTP_TOKEN_PATTERN.test(value);
}

function requireHttpToken(value, message) {
    if (!isHttpToken(value)) {
        throw new TypeError(message);
    }
    return value;
}

function validateSqliteProviderOptions(options, { requireDatabase = false } = {}) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy sqlite provider options must be a plain object.");
    }
    const allowedKeys = new Set(["database", "queueCapacity"]);
    for (const key of Object.keys(options)) {
        if (!allowedKeys.has(key)) {
            throw new TypeError(`Sloppy sqlite provider option '${key}' is not supported.`);
        }
    }
    if (requireDatabase && (typeof options.database !== "string" || options.database.length === 0)) {
        throw new TypeError("Sloppy sqlite provider database option must be a non-empty string.");
    }
    if (
        Object.prototype.hasOwnProperty.call(options, "database") &&
        (typeof options.database !== "string" || options.database.includes("\0"))
    ) {
        throw new TypeError("Sloppy sqlite provider database option must be a string without NUL.");
    }
    if (
        Object.prototype.hasOwnProperty.call(options, "queueCapacity") &&
        (!Number.isInteger(options.queueCapacity) || options.queueCapacity < 1 || options.queueCapacity > 1_000_000)
    ) {
        throw new TypeError("Sloppy sqlite provider queueCapacity option must be a positive integer.");
    }
    return Object.freeze({ ...options });
}

export {
    isHttpToken,
    isPlainObject,
    optionalBoolean,
    optionalInteger,
    optionalNonNegativeInteger,
    optionalPositiveInteger,
    requireHttpToken,
    requireNonEmptyString,
    requirePlainObject,
    requirePositiveFiniteNumber,
    validateSqliteProviderOptions,
};
