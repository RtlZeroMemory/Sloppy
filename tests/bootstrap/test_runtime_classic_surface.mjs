import assert from "node:assert/strict";

import {
    Cache,
    ProblemDetails,
    Results,
    Schema,
    Text,
    schema,
} from "../../stdlib/sloppy/index.js";

await import("../../stdlib/sloppy/internal/runtime-classic.js");

const runtime = globalThis.__sloppy_runtime;
assert.equal(typeof runtime, "object");

const expectedExports = [
    "Results",
    "Cache",
    "Realtime",
    "schema",
    "Schema",
    "ProblemDetails",
    "Random",
    "Hash",
    "Hmac",
    "Password",
    "ConstantTime",
    "Secret",
    "NonCryptoHash",
    "Base64",
    "Base64Url",
    "Hex",
    "Text",
    "Binary",
    "Compression",
    "Checksums",
    "data",
    "Redis",
    "sql",
    "Migrations",
    "ProviderHealth",
    "orm",
    "table",
    "column",
    "relation",
    "Time",
    "Deadline",
    "CancellationController",
    "File",
    "Directory",
    "Path",
    "TcpClient",
    "TcpListener",
    "TcpConnection",
    "NetworkAddress",
    "HttpClient",
    "System",
    "Environment",
    "Process",
    "DockerCliBackend",
    "TestServices",
    "Webhooks",
    "BackgroundService",
    "WorkQueue",
    "WorkerPool",
    "Worker",
    "unsafeFfi",
    "t",
];

for (const name of expectedExports) {
    assert.notEqual(runtime[name], undefined, `runtime-classic should export ${name}`);
}

assert.equal(typeof runtime.Results.json, typeof Results.json);
assert.equal(typeof runtime.Cache.memory, typeof Cache.memory);
assert.equal(typeof runtime.ProblemDetails.defaults, typeof ProblemDetails.defaults);
assert.equal(typeof runtime.schema.string, typeof schema.string);
assert.equal(typeof runtime.Schema.object, typeof Schema.object);
assert.equal(typeof runtime.Text.utf8.encode, typeof Text.utf8.encode);

const response = runtime.Results.json({ ok: true });
assert.equal(response.kind, "json");
assert.equal(response.status, 200);

assert.deepEqual(runtime.schema.string().validate("ok"), schema.string().validate("ok"));
assert.deepEqual(runtime.Text.utf8.decode(runtime.Text.utf8.encode("hello")), "hello");
