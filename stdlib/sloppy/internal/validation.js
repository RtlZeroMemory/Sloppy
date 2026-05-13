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
};
