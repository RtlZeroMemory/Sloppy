import base, {
    AssertionError,
    deepEqual,
    deepStrictEqual,
    doesNotReject,
    doesNotThrow,
    fail,
    ifError,
    notStrictEqual,
    ok,
    rejects,
    strictEqual,
    throws,
} from "../assert.js";

const equal = strictEqual;
function strict(value, message = undefined) {
    return ok(value, message);
}

Object.assign(strict, base, { equal });

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

export default strict;
