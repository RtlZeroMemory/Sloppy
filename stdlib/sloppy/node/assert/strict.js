import base, {
    AssertionError,
    deepEqual,
    deepStrictEqual,
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
    equal,
    ok,
    rejects,
    strictEqual,
    throws,
};

export default strict;
