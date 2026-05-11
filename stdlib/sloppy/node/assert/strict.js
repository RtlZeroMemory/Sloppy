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
const strict = {
    ...base,
    equal,
};

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
