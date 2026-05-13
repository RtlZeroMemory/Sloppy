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
    "RateLimit",
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
assert.equal(typeof runtime.RateLimit.fixedWindow, "function");
assert.equal(typeof runtime.RateLimit.memory, "function");

const response = runtime.Results.json({ ok: true });
assert.equal(response.kind, "json");
assert.equal(response.status, 200);

assert.deepEqual(runtime.schema.string().validate("ok"), schema.string().validate("ok"));
assert.deepEqual(runtime.Text.utf8.decode(runtime.Text.utf8.encode("hello")), "hello");
const ratePolicy = runtime.RateLimit.fixedWindow({
    limit: 1,
    windowMs: 1000,
    partitionBy: "global",
});
assert.equal(runtime.isRateLimitPolicy(ratePolicy), true);
const rateStore = runtime.RateLimit.memory({ maxKeys: 10 });
assert.equal(runtime.isRateLimitStore(rateStore), true);
assert.equal((await rateStore.check({ policy: ratePolicy, partitionHash: "global", cost: 1 })).allowed, true);
assert.equal((await rateStore.check({ policy: ratePolicy, partitionHash: "global", cost: 1 })).allowed, false);
rateStore.dispose();

const dockerCommands = [];
const dockerBackend = {
    async run(args) {
        dockerCommands.push(args);
        if (args[0] === "version") {
            return { exitCode: 0, stdout: "{\"Server\":{\"Version\":\"classic\"}}", stderr: "", timedOut: false };
        }
        return { exitCode: 0, stdout: "", stderr: "", timedOut: false };
    },
};
assert.deepEqual(await runtime.TestServices.docker.available({ dockerBackend }), {
    ok: true,
    available: true,
    reason: undefined,
    version: { Server: { Version: "classic" } },
});
assert.equal(await runtime.TestServices.docker.require({ dockerBackend }).then((result) => result.ok), true);
assert.deepEqual(dockerCommands[0], ["version", "--format", "{{json .}}"]);

const unavailable = await runtime.TestServices.docker.available({
    dockerBackend: {
        async run() {
            return { exitCode: 1, stdout: "", stderr: "offline", timedOut: false };
        },
    },
});
assert.equal(unavailable.ok, false);
await assert.rejects(
    () => runtime.TestServices.docker.require({
        dockerBackend: {
            async run() {
                return { exitCode: 1, stdout: "", stderr: "offline", timedOut: false };
            },
        },
    }),
    /SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE/u,
);
