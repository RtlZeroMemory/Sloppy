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

function fail(options) {
    throw new AssertionError(options);
}

function ok(value, message = undefined) {
    if (!value) {
        fail({ actual: value, expected: true, operator: "ok", message });
    }
}

function equal(actual, expected, message = undefined) {
    if (actual != expected) {
        fail({ actual, expected, operator: "==", message });
    }
}

function strictEqual(actual, expected, message = undefined) {
    if (!Object.is(actual, expected)) {
        fail({ actual, expected, operator: "strictEqual", message });
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
        fail({ actual, expected, operator: "deepStrictEqual", message });
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
    fail({
        actual: error,
        expected,
        operator,
        message: message ?? `${operator} validation failed.`,
    });
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
    fail({ actual: undefined, expected, operator: "throws", message });
}

async function rejects(fn, expected = undefined, message = undefined) {
    try {
        await (typeof fn === "function" ? fn() : fn);
    } catch (error) {
        if (!matchExpected(error, expected)) {
            mismatch(error, expected, "rejects", message);
        }
        return error;
    }
    fail({ actual: undefined, expected, operator: "rejects", message });
}

function assert(value, message = undefined) {
    return ok(value, message);
}

Object.assign(assert, {
    AssertionError,
    deepEqual,
    deepStrictEqual,
    equal,
    ok,
    rejects,
    strictEqual,
    throws,
});

export {
    AssertionError,
    deepEqual,
    deepStrictEqual,
    equal,
    ok,
    rejects,
    strictEqual,
    throws,
};

export default assert;
