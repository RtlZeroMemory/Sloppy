class AssertionError extends Error {
    constructor(options = {}) {
        super(options.message ?? "Assertion failed");
        this.name = "AssertionError";
        this.actual = options.actual;
        this.expected = options.expected;
        this.operator = options.operator;
        this.code = "ERR_ASSERTION";
    }
}

function internalFail(options) {
    throw new AssertionError(options);
}

function fail(actual = undefined, expected = undefined, message = undefined, operator = "!=") {
    if (arguments.length === 0) {
        internalFail({ message: "Failed", operator: "fail" });
    }
    if (arguments.length === 1) {
        if (actual instanceof Error) {
            throw actual;
        }
        internalFail({ message: actual === undefined ? "Failed" : actual, operator: "fail" });
    }
    internalFail({
        actual,
        expected,
        message: message ?? `${actual} ${operator} ${expected}`,
        operator,
    });
}

function ok(value, message = undefined) {
    if (!value) {
        internalFail({ actual: value, expected: true, operator: "ok", message });
    }
}

function equal(actual, expected, message = undefined) {
    if (actual != expected) {
        internalFail({ actual, expected, operator: "==", message });
    }
}

function strictEqual(actual, expected, message = undefined) {
    if (!Object.is(actual, expected)) {
        internalFail({ actual, expected, operator: "strictEqual", message });
    }
}

function notStrictEqual(actual, expected, message = undefined) {
    if (Object.is(actual, expected)) {
        internalFail({ actual, expected, operator: "notStrictEqual", message });
    }
}

function stable(value) {
    if (value === null || typeof value !== "object") {
        return JSON.stringify(value);
    }
    if (Array.isArray(value)) {
        return `[${value.map(stable).join(",")}]`;
    }
    return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${stable(value[key])}`).join(",")}}`;
}

function deepStrictEqual(actual, expected, message = undefined) {
    if (stable(actual) !== stable(expected)) {
        internalFail({ actual, expected, operator: "deepStrictEqual", message });
    }
}

const deepEqual = deepStrictEqual;

function matchExpected(error, expected) {
    if (expected === undefined) {
        return true;
    }
    if (typeof expected === "function") {
        try {
            if (error instanceof expected) {
                return true;
            }
        } catch {
        }
        try {
            return expected(error) === true;
        } catch {
            return false;
        }
    }
    if (expected instanceof RegExp) {
        return expected.test(String(error?.message ?? error));
    }
    if (expected !== null && typeof expected === "object") {
        return Object.entries(expected).every(([key, value]) => Object.is(error?.[key], value));
    }
    return false;
}

function mismatch(error, expected, operator, message) {
    internalFail({
        actual: error,
        expected,
        operator,
        message: message ?? `${operator} validation failed.`,
    });
}

function isPromiseLike(value) {
    return value !== null
        && (typeof value === "object" || typeof value === "function")
        && typeof value.then === "function";
}

function requirePromiseLike(value, operator) {
    if (!isPromiseLike(value)) {
        throw new TypeError(`${operator} expects a Promise or a function returning a Promise.`);
    }
    return value;
}

function throws(fn, expected = undefined, message = undefined) {
    if (typeof fn !== "function") {
        throw new TypeError("assert.throws expects a function.");
    }
    try {
        fn();
    } catch (error) {
        if (!matchExpected(error, expected)) {
            mismatch(error, expected, "throws", message);
        }
        return error;
    }
    internalFail({ actual: undefined, expected, operator: "throws", message });
}

function doesNotThrow(fn, expected = undefined, message = undefined) {
    if (typeof fn !== "function") {
        throw new TypeError("assert.doesNotThrow expects a function.");
    }
    if (typeof expected === "string" && message === undefined) {
        message = expected;
        expected = undefined;
    }
    try {
        fn();
    } catch (error) {
        if (expected === undefined || matchExpected(error, expected)) {
            mismatch(error, expected, "doesNotThrow", message);
        }
        throw error;
    }
}

async function rejects(fn, expected = undefined, message = undefined) {
    const promise = requirePromiseLike(typeof fn === "function" ? fn() : fn, "assert.rejects");
    try {
        await promise;
    } catch (error) {
        if (!matchExpected(error, expected)) {
            mismatch(error, expected, "rejects", message);
        }
        return error;
    }
    internalFail({ actual: undefined, expected, operator: "rejects", message });
}

async function doesNotReject(fn, expected = undefined, message = undefined) {
    if (typeof expected === "string" && message === undefined) {
        message = expected;
        expected = undefined;
    }
    const promise = requirePromiseLike(typeof fn === "function" ? fn() : fn, "assert.doesNotReject");
    try {
        await promise;
    } catch (error) {
        if (expected === undefined || matchExpected(error, expected)) {
            mismatch(error, expected, "doesNotReject", message);
        }
        throw error;
    }
}

function ifError(value) {
    if (value !== null && value !== undefined) {
        internalFail({ actual: value, expected: null, operator: "ifError", message: value?.message });
    }
}

function assert(value, message = undefined) {
    return ok(value, message);
}

Object.assign(assert, {
    AssertionError,
    deepEqual,
    deepStrictEqual,
    doesNotReject,
    doesNotThrow,
    equal,
    fail,
    ifError,
    notStrictEqual,
    ok,
    rejects,
    strictEqual,
    throws,
});

export {
    AssertionError,
    deepEqual,
    deepStrictEqual,
    doesNotReject,
    doesNotThrow,
    equal,
    fail,
    ifError,
    notStrictEqual,
    ok,
    rejects,
    strictEqual,
    throws,
};

export default assert;
