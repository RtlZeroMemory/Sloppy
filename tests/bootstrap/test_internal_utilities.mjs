import assert from "node:assert/strict";

import { copyArrayBuffer, copyBinaryLike, copyUint8Array } from "../../stdlib/sloppy/internal/bytes.js";
import { disposeAll, onceAsync } from "../../stdlib/sloppy/internal/disposable.js";
import {
    appendVaryHeader,
    assertHeaderToken,
    assertHeaderValue,
    createHeaderLookup,
    headerValue,
    isHeaderToken,
} from "../../stdlib/sloppy/internal/headers.js";
import { deepFreeze, snapshotJson } from "../../stdlib/sloppy/internal/json.js";
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
import {
    createDiagnosticsStore,
    normalizeOverrideMap,
} from "../../stdlib/sloppy/internal/testhost-diagnostics.js";
import {
    copyBytes,
    createHeadersLike,
    headerEntriesFromObject,
    headersToEntries,
    normalizeHeaderEntries,
    responseHeaderEntries,
    setDefaultHeader,
} from "../../stdlib/sloppy/internal/testhost-http.js";
import {
    dockerAvailable,
    dockerRequire,
    dockerRunOk,
    ensureImage,
    inspectContainer,
    mappedPortFromInspect,
    parseInspectJson,
} from "../../stdlib/sloppy/internal/testservices-docker.js";

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

const originalBuffer = new Uint8Array([0, 1, 2, 3, 4]).buffer;
const copiedBuffer = copyArrayBuffer(new DataView(originalBuffer, 1, 3));
new Uint8Array(originalBuffer)[2] = 9;
assert.deepEqual([...new Uint8Array(copiedBuffer)], [1, 2, 3]);
assert.deepEqual([...copyUint8Array(new Uint8Array([2, 1]).buffer)], [2, 1]);
const copiedDataView = copyBinaryLike(new DataView(new Uint8Array([8, 9]).buffer));
assert(copiedDataView instanceof DataView);
assert.deepEqual([...new Uint8Array(copiedDataView.buffer)], [8, 9]);
const copiedTypedArray = copyBinaryLike(new Uint16Array([7, 8]));
assert(copiedTypedArray instanceof Uint16Array);
assert.deepEqual([...copiedTypedArray], [7, 8]);
assert.equal(copyArrayBuffer("nope"), undefined);

const headerLookup = createHeaderLookup({ "X-Test": "ok", "content-type": "text/plain" });
assert.equal(headerLookup.get("x-test"), "ok");
assert.equal(headerLookup.get("Content-Type"), "text/plain");
assert.equal(isHeaderToken("x-api-key"), true);
assert.equal(isHeaderToken("bad header"), false);
assert.equal(assertHeaderToken("x-safe", "Shared"), "x-safe");
assert.equal(assertHeaderValue("safe value", "Shared"), "safe value");
assert.throws(() => assertHeaderToken("bad header", "Shared"), /safe HTTP tokens/u);
assert.throws(() => assertHeaderValue("bad\nvalue", "Shared"), /safe strings/u);
assert.equal(headerValue({ get: (name) => name === "X-Test" ? "direct" : undefined }, "X-Test"), "direct");
assert.equal(headerValue({ "X-Test": "object" }, "x-test"), "object");
assert.throws(() => createHeaderLookup(7), /headers must be response headers or a plain object/u);
const varyHeaders = {};
appendVaryHeader(varyHeaders, "Accept-Encoding");
appendVaryHeader(varyHeaders, "accept-encoding");
appendVaryHeader(varyHeaders, "Origin");
assert.deepEqual(varyHeaders, { Vary: "Accept-Encoding, Origin" });

const jsonSource = { nested: { value: 1 }, list: [1, 2], omitted: undefined };
const jsonSnapshot = snapshotJson(jsonSource);
jsonSource.nested.value = 2;
assert.deepEqual(jsonSnapshot, { nested: { value: 1 }, list: [1, 2] });
assert.equal(Object.isFrozen(jsonSnapshot), true);
assert.equal(Object.isFrozen(jsonSnapshot.nested), true);
assert.equal(snapshotJson(undefined), undefined);
assert.equal(deepFreeze(null), null);

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

assert.deepEqual(normalizeOverrideMap({ a: 1 }, "config"), { a: 1 });
assert.throws(() => normalizeOverrideMap([], "config"), /overrides must be a plain object/u);

const diagnostics = createDiagnosticsStore(["top-secret"]);
diagnostics.record({
    code: "SLOPPY_TEST",
    message: "message with top-secret",
    fields: {
        count: 3,
        token: "top-secret",
        nested: { apiKey: "top-secret" },
    },
});
assert.equal(diagnostics.latest().message, "message with [REDACTED]");
assert.deepEqual(diagnostics.latest().fields, {
    count: "3",
    token: "[REDACTED]",
    nested: { apiKey: "[REDACTED]" },
});
assert.equal(diagnostics.filter({ code: "SLOPPY_TEST" }).length, 1);
assert.equal(diagnostics.expectCode("SLOPPY_TEST"), diagnostics);
assert.equal(diagnostics.expectNoSecretLeaks(), diagnostics);
assert.throws(() => diagnostics.filter([]), /filter criteria must be a plain object/u);

const sourceBuffer = new Uint8Array([0, 1, 2, 3, 4, 5]).buffer;
const sourceView = new DataView(sourceBuffer, 1, 4);
const copiedView = copyBytes(sourceView, "view");
new Uint8Array(sourceBuffer)[2] = 9;
assert.deepEqual([...copiedView], [1, 2, 3, 4]);
assert.deepEqual([...copyBytes(new Uint8Array([1, 2, 3]), "bytes")], [1, 2, 3]);
assert.throws(() => copyBytes("abc", "body"), /Uint8Array, ArrayBuffer, or ArrayBuffer view/u);

assert.deepEqual(headerEntriesFromObject({ "X-Test": "1" }, "request"), [["X-Test", "1"]]);
assert.throws(() => headerEntriesFromObject({ "Bad Header": "1" }, "request"), /safe HTTP tokens/u);
assert.throws(() => headerEntriesFromObject({ "X-Test": "bad\nvalue" }, "request"), /safe strings/u);

const normalizedHeaders = normalizeHeaderEntries([
    ["X-Test", "1"],
    ["x-test", "2"],
    ["Set-Cookie", "first=1"],
    ["set-cookie", "second=2"],
]);
assert.deepEqual(normalizedHeaders, [
    ["x-test", "1, 2"],
    ["set-cookie", "first=1"],
    ["set-cookie", "second=2"],
]);
const headersLike = createHeadersLike(normalizedHeaders);
assert.equal(headersLike.get("X-Test"), "1, 2");
assert.equal(headersLike.get("set-cookie"), "first=1");
assert.deepEqual(headersToEntries({ "x-a": "b" }), [["x-a", "b"]]);
assert.deepEqual(headersToEntries(headersLike), normalizedHeaders);
assert.throws(() => headersToEntries(1), /response headers or a plain object/u);

const defaultEntries = [["content-type", "text/plain"]];
setDefaultHeader(defaultEntries, "Content-Type", "application/json");
setDefaultHeader(defaultEntries, "Content-Length", "2");
assert.deepEqual(defaultEntries, [["content-type", "text/plain"], ["Content-Length", "2"]]);
assert.deepEqual(responseHeaderEntries({ headers: headersLike }, true), normalizedHeaders);

assert.equal(mappedPortFromInspect({
    NetworkSettings: {
        Ports: {
            "5432/tcp": [{ HostPort: "49170" }],
        },
    },
}, 5432), 49170);
assert.throws(() => mappedPortFromInspect({}, 5432), /mapped host port/u);
assert.deepEqual(parseInspectJson(JSON.stringify([{ id: "container" }])), { id: "container" });
assert.throws(() => parseInspectJson("[]"), /no container metadata/u);

const dockerCommands = [];
const dockerBackend = {
    async run(args) {
        dockerCommands.push(args);
        if (args[0] === "version") {
            return { exitCode: 0, stdout: "{\"Server\":{\"Version\":\"1\"}}", stderr: "", timedOut: false };
        }
        if (args[0] === "image" && args[1] === "inspect") {
            return { exitCode: 1, stdout: "", stderr: "missing", timedOut: false };
        }
        if (args[0] === "pull") {
            return { exitCode: 0, stdout: "pulled", stderr: "", timedOut: false };
        }
        if (args[0] === "inspect") {
            return {
                exitCode: 0,
                stdout: JSON.stringify([{
                    NetworkSettings: {
                        Ports: {
                            "6379/tcp": [{ HostPort: "49199" }],
                        },
                    },
                }]),
                stderr: "",
                timedOut: false,
            };
        }
        return { exitCode: 0, stdout: "", stderr: "", timedOut: false };
    },
};
assert.deepEqual(await dockerAvailable({ dockerBackend }), {
    ok: true,
    available: true,
    reason: undefined,
    version: { Server: { Version: "1" } },
});
assert.equal(await dockerRequire({ dockerBackend }).then((result) => result.ok), true);
await ensureImage(dockerBackend, "redis:7-alpine", {});
assert(dockerCommands.some((args) => args[0] === "pull" && args[1] === "redis:7-alpine"));
assert.equal((await inspectContainer(dockerBackend, "redis-container", 6379, {})).port, 49199);
assert.equal((await dockerRunOk(dockerBackend, ["noop"])).exitCode, 0);
await assert.rejects(
    () => dockerRunOk({ run: async () => ({ exitCode: 2, stdout: "", stderr: "boom", timedOut: false }) }, ["bad"]),
    /docker bad failed with exit code 2/u,
);
await assert.rejects(
    () => dockerRequire({ dockerBackend: { run: async () => ({ exitCode: 1, stdout: "", stderr: "offline", timedOut: false }) } }),
    /SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE/u,
);

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
