import assert from "node:assert/strict";

import { disposeAll, onceAsync } from "../../stdlib/sloppy/internal/disposable.js";
import {
    SECRET_REDACTION,
    boundedText,
    redactHeaders,
    redactObject,
    redactTextSecrets,
    redactUrlTemplate,
} from "../../stdlib/sloppy/internal/redaction.js";
import {
    isHttpToken,
    optionalBoolean,
    optionalInteger,
    optionalNonNegativeInteger,
    optionalPositiveInteger,
    requireHttpToken,
    requireNonEmptyString,
    requirePlainObject,
    requirePositiveFiniteNumber,
} from "../../stdlib/sloppy/internal/validation.js";

assert.equal(requirePlainObject(Object.freeze({ ok: true }), "plain expected").ok, true);
assert.throws(() => requirePlainObject([], "plain expected"), /plain expected/u);
assert.equal(requireNonEmptyString("name", "string expected"), "name");
assert.throws(() => requireNonEmptyString("", "string expected"), /string expected/u);
assert.equal(optionalBoolean(undefined, "bool expected", true), true);
assert.equal(optionalBoolean(false, "bool expected", true), false);
assert.throws(() => optionalBoolean("true", "bool expected"), /bool expected/u);
assert.equal(optionalInteger(7, "int expected"), 7);
assert.throws(() => optionalInteger(7.5, "int expected"), /int expected/u);
assert.equal(optionalPositiveInteger(undefined, "positive expected", 3), 3);
assert.equal(optionalPositiveInteger(3, "positive expected"), 3);
assert.throws(() => optionalPositiveInteger(0, "positive expected"), /positive expected/u);
assert.equal(optionalNonNegativeInteger(0, "non-negative expected"), 0);
assert.throws(() => optionalNonNegativeInteger(-1, "non-negative expected"), /non-negative expected/u);
assert.equal(requirePositiveFiniteNumber(1.2, "finite expected"), 2);
assert.throws(() => requirePositiveFiniteNumber(0, "finite expected"), /finite expected/u);
assert.equal(isHttpToken("x-api-key"), true);
assert.equal(isHttpToken("bad header"), false);
assert.equal(requireHttpToken("authorization", "token expected"), "authorization");
assert.throws(() => requireHttpToken("bad header", "token expected"), /token expected/u);

assert.equal(SECRET_REDACTION, "[REDACTED]");
assert.equal(boundedText("abcdef", 4), "cdef");
assert.equal(boundedText("abcdef", 4, "head"), "abcd");
assert.equal(redactTextSecrets("value has secret", ["secret"]), "value has [REDACTED]");
assert.deepEqual(redactHeaders({ Authorization: "bearer secret", "x-safe": "ok" }), {
    Authorization: "[REDACTED]",
    "x-safe": "ok",
});
assert.equal(redactUrlTemplate("/users", { api_key: "secret", page: "1" }), "/users?api_key=[REDACTED]&page={value}");
assert.deepEqual(redactObject({
    password: "secret",
    nested: {
        token: "abc",
        value: "secret text",
        count: 3,
    },
}, { secrets: ["secret"] }), {
    password: "[REDACTED]",
    nested: {
        token: "[REDACTED]",
        value: "[REDACTED] text",
        count: 3,
    },
});
assert.deepEqual(redactObject({ count: 3 }, { stringifyPrimitives: true }), { count: "3" });

const disposed = [];
await disposeAll([
    { dispose: async () => disposed.push("a") },
    undefined,
    { dispose: () => disposed.push("b") },
]);
assert.deepEqual(disposed, ["a", "b"]);
await assert.rejects(
    () => disposeAll([
        { dispose: () => { throw new Error("first"); } },
        { dispose: () => { throw new Error("second"); } },
    ]),
    AggregateError,
);

let count = 0;
const closeOnce = onceAsync(async () => {
    count += 1;
});
await Promise.all([closeOnce(), closeOnce(), closeOnce()]);
assert.equal(count, 1);
