import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import * as Stdlib from "../../stdlib/sloppy/index.js";
import {
    Base64,
    Base64Url,
    CancellationController,
    Deadline,
    Health,
    Hex,
    HttpClient,
    Metrics,
    ProblemDetails,
    Realtime,
    Results,
    Schema,
    Sloppy,
    Testing,
    Text,
    WorkQueue,
} from "../../stdlib/sloppy/index.js";

export const DEFAULT_JS_TARGETS = Object.freeze([
    "config-json",
    "openapi-plan",
    "headers",
    "query-string",
    "percent-decoding",
    "logging-json",
    "package-manifest",
    "route-table",
    "required-features",
    "http-client-options",
    "results-headers",
    "schema-validation",
    "json-serialization",
    "request-media",
    "realtime-metadata",
    "worker-queue",
    "h2-client-options",
    "stdlib-import-shapes",
    "ops-health-metrics",
]);

const SECRET_MARKER = "SECRET_JS_FUZZ_SHOULD_NOT_APPEAR";
const HTTP_METHODS = Object.freeze(["GET", "POST", "PUT", "PATCH", "DELETE"]);
const STD_LIB_EXPORTS = Object.freeze({
    root: ["Sloppy", "Results", "ProblemDetails", "HttpClient", "WorkQueue", "Time", "Deadline", "Health", "Metrics"],
    codec: ["Base64", "Base64Url", "Hex", "Text", "Binary", "Checksums", "Compression"],
    time: ["Time", "Deadline", "CancellationController", "TimeoutError", "CancelledError"],
    net: ["HttpClient", "TcpClient", "TcpListener", "TcpConnection", "LocalEndpoint", "NetworkAddress"],
    workers: ["WorkQueue", "WorkerPool", "Worker", "BackgroundService"],
});

function parseArgs(argv) {
    const options = {
        target: "",
        all: false,
        iterations: 1000,
        seed: 12345,
        failureRoot: "artifacts/fuzz/failures",
        reproCommand: "",
    };
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index];
        switch (arg) {
            case "--target":
                options.target = argv[++index] ?? "";
                break;
            case "--all":
                options.all = true;
                break;
            case "--iterations":
                options.iterations = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "--seed":
                options.seed = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "--failure-root":
                options.failureRoot = argv[++index] ?? options.failureRoot;
                break;
            case "--repro-command":
                options.reproCommand = argv[++index] ?? "";
                break;
            case "-h":
            case "--help":
                options.help = true;
                break;
            default:
                throw new Error(`unknown option '${arg}'`);
        }
    }
    if (!Number.isSafeInteger(options.iterations) || options.iterations < 1) {
        throw new Error("--iterations must be a positive integer");
    }
    if (!Number.isSafeInteger(options.seed)) {
        throw new Error("--seed must be an integer");
    }
    if (options.all && options.target.length !== 0) {
        throw new Error("use either --all or --target, not both");
    }
    if (!options.all && options.target.length === 0) {
        options.all = true;
    }
    return options;
}

function usage() {
    return [
        "Usage: node tests/fuzz/js_fuzz_targets.mjs [--target NAME|--all] [--iterations N] [--seed N]",
        "",
        `Targets: ${DEFAULT_JS_TARGETS.join(", ")}`,
    ].join("\n");
}

function makePrng(seed) {
    let state = seed >>> 0;
    return {
        nextU32() {
            state += 0x6d2b79f5;
            let value = state;
            value = Math.imul(value ^ (value >>> 15), value | 1);
            value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
            return (value ^ (value >>> 14)) >>> 0;
        },
        int(maxExclusive) {
            assert(maxExclusive > 0);
            return this.nextU32() % maxExclusive;
        },
        bool() {
            return (this.nextU32() & 1) === 1;
        },
        pick(values) {
            return values[this.int(values.length)];
        },
    };
}

function bytesFromPrng(random, maxLength = 512) {
    const bytes = new Uint8Array(random.int(maxLength + 1));
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = random.nextU32() & 0xff;
    }
    return bytes;
}

function textFromPrng(random, maxLength = 64) {
    const alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:/%+{}[](),; ";
    let output = "";
    const length = random.int(maxLength + 1);
    for (let index = 0; index < length; index += 1) {
        output += alphabet[random.int(alphabet.length)];
    }
    return output;
}

function jsonValue(random, depth = 0) {
    if (depth >= 3) {
        return random.pick([null, true, false, random.int(1000), textFromPrng(random, 16)]);
    }
    switch (random.int(6)) {
        case 0:
            return null;
        case 1:
            return random.bool();
        case 2:
            return random.int(100000);
        case 3:
            return textFromPrng(random, 32);
        case 4:
            return Array.from({ length: random.int(4) }, () => jsonValue(random, depth + 1));
        default: {
            const object = {};
            const count = random.int(4);
            for (let index = 0; index < count; index += 1) {
                object[`k${index}_${random.int(99)}`] = jsonValue(random, depth + 1);
            }
            return object;
        }
    }
}

function maybeThrows(fn, allowed = [TypeError, Error, RangeError]) {
    try {
        return { ok: true, value: fn() };
    } catch (error) {
        assert(allowed.some((type) => error instanceof type), `unexpected error type: ${error?.constructor?.name}`);
        return { ok: false, error };
    }
}

async function maybeRejects(fn) {
    try {
        return { ok: true, value: await fn() };
    } catch (error) {
        assert(error instanceof Error, "expected rejected Error instance");
        return { ok: false, error };
    }
}

function assertResultDescriptor(descriptor) {
    assert.equal(descriptor.__sloppyResult, true);
    assert.equal(Object.isFrozen(descriptor), true);
    assert.equal(typeof descriptor.status, "number");
}

function randomHeaderName(random, valid = true) {
    if (!valid) {
        return random.pick(["", "bad header", "bad\nheader", "content-type", "connection"]);
    }
    const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789-";
    const length = 1 + random.int(20);
    let name = "x-";
    for (let index = 0; index < length; index += 1) {
        name += alphabet[random.int(alphabet.length)];
    }
    return name;
}

function randomHeaderValue(random, valid = true) {
    if (!valid) {
        return random.pick(["bad\r\nvalue", "bad\0value", 1, {}]);
    }
    return textFromPrng(random, 32).replace(/[\r\n\0]/g, "_");
}

function randomRoutePattern(random) {
    const bases = ["/", "/items", "/items/{id:int}", "/users/{name}", "/reports/{slug}", "/{tenant}/users"];
    return random.pick(bases);
}

function assertBytesEqual(actual, expected) {
    assert.deepEqual(Array.from(actual), Array.from(expected));
}

function queryComponent(random) {
    const raw = textFromPrng(random, 16);
    if (random.bool()) {
        return encodeURIComponent(raw);
    }
    return raw.replace(/ /g, "+").replace(/[?#&=]/g, "_");
}

function validateManifestShape(manifest) {
    assert.equal(typeof manifest, "object");
    if ("schema" in manifest) {
        assert.equal(typeof manifest.schema, "string");
    }
    if ("files" in manifest) {
        assert(Array.isArray(manifest.files));
    }
    if ("bin" in manifest) {
        assert.equal(typeof manifest.bin, "object");
    }
}

const targets = Object.freeze({
    "config-json"(random) {
        const builder = Sloppy.createBuilder();
        const secretKey = random.pick(["Password", "Token", "ApiKey", "ConnectionString"]);
        const configObject = {
            App: {
                Name: textFromPrng(random, 20) || "app",
                Port: String(1024 + random.int(50000)),
                Enabled: random.bool() ? "true" : "false",
                Tags: [textFromPrng(random, 8), String(random.int(99))],
                Nested: { Value: jsonValue(random, 1) },
                Secrets: { [secretKey]: SECRET_MARKER },
            },
        };
        maybeThrows(() => builder.config.addObject(configObject));
        const name = builder.config.getString("App:Name", "fallback");
        assert.equal(typeof name, "string");
        maybeThrows(() => builder.config.getInt("App:Port", 3000));
        maybeThrows(() => builder.config.getBool("App:Enabled", false));
        maybeThrows(() => builder.config.getArray("App:Tags", []));
        maybeThrows(() => builder.config.bind("App", {
            name: { type: "string", default: "fallback" },
            port: { type: "integer", default: 3000, min: 1, max: 65535 },
            enabled: { type: "boolean", default: false },
        }));
        assert.equal(typeof builder.config.__snapshot(), "object");
        maybeThrows(() => builder.config.addObject(random.pick([null, [], "bad", { "": true }])));
    },

    "openapi-plan"(random) {
        const app = Sloppy.create();
        const count = 1 + random.int(6);
        for (let index = 0; index < count; index += 1) {
            maybeThrows(() => {
                const method = random.pick(HTTP_METHODS);
                const pattern = randomRoutePattern(random);
                app[`map${method[0]}${method.slice(1).toLowerCase()}`](
                    pattern,
                    () => Results.ok({ index, value: textFromPrng(random, 8) }),
                ).withName(`Route.${index}.${method}`);
            });
        }
        const contributions = app.__getPlanContributions();
        assert.equal(typeof contributions, "object");
        for (const route of app.__getRoutes()) {
            assert.equal(typeof route.method, "string");
            assert.equal(typeof route.pattern, "string");
        }
        JSON.stringify({
            routes: contributions.routes,
            capabilities: contributions.capabilities,
            partialPlan: {
                version: random.pick([1, "1", null]),
                routes: random.bool() ? contributions.routes : jsonValue(random, 1),
            },
        });
    },

    headers(random) {
        const validHeaders = {};
        const invalidHeaders = {};
        for (let index = 0; index < 1 + random.int(4); index += 1) {
            validHeaders[randomHeaderName(random, true)] = randomHeaderValue(random, true);
            invalidHeaders[randomHeaderName(random, false)] = randomHeaderValue(random, random.bool());
        }
        const result = Results.text(textFromPrng(random, 32), { headers: validHeaders });
        assertResultDescriptor(result);
        maybeThrows(() => Results.json({ ok: true }, { headers: invalidHeaders }));

        const descriptor = HttpClient.create({
            headers: {
                authorization: "Bearer descriptor-value",
                [randomHeaderName(random, true)]: randomHeaderValue(random, true),
            },
            secretHeaders: ["authorization"],
        }).__sloppyHttpClientOptions;
        assert.equal(JSON.stringify(descriptor).includes(SECRET_MARKER), false);
    },

    async "query-string"(random) {
        const app = Sloppy.create();
        app.mapGet("/items/{id:int}", ({ query, route }) => Results.ok({ id: route.id ?? "0", query }));
        const host = Testing.createHost(app);
        try {
            const pairs = [];
            const count = random.int(5);
            for (let index = 0; index < count; index += 1) {
                pairs.push(`${queryComponent(random)}=${queryComponent(random)}`);
            }
            if (random.bool()) {
                pairs.push(`repeat=${queryComponent(random)}`);
                pairs.push(`repeat=${queryComponent(random)}`);
            }
            const target = `/items/${1 + random.int(999)}?${pairs.join(random.pick(["&", "&&"]))}`;
            const result = await maybeRejects(() => host.get(target));
            if (result.ok) {
                assert.equal(result.value.status, 200);
                const value = await result.value.json();
                assert.equal(typeof value.query, "object");
            }
        } finally {
            await host.close();
        }
    },

    async "percent-decoding"(random) {
        const app = Sloppy.create();
        app.mapGet("/echo/{slug}", ({ route, query }) => Results.ok({ route, query }));
        const host = Testing.createHost(app);
        const encoded = random.pick(["abc", "%7Bok%7D", "%zz", "%E0%A4%A", "%", "%2", "%00", "hello+world"]);
        try {
            await maybeRejects(() => host.get(`/echo/${encoded}?value=${encoded}&empty=&bare`));
        } finally {
            await host.close();
        }
    },

    "logging-json"(random) {
        const builder = Sloppy.createBuilder();
        const sink = builder.logging.addMemorySink({ capacity: 4 });
        builder.logging.addRedactionKey("customSecret");
        builder.logging.setMinimumLevel("debug");
        const app = builder.build();
        for (const key of ["authorization", "cookie", "token", "password", "connectionString", "apiKey", "customSecret"]) {
            maybeThrows(() => app.log.info(`event ${textFromPrng(random, 20)}`, {
                [key]: SECRET_MARKER,
                message: `quote " newline\n control ${random.int(100)}`,
            }));
        }
        maybeThrows(() => app.log.info("bad fields", { nested: { no: true } }));
        const serialized = JSON.stringify(sink.entries());
        assert.equal(serialized.includes(SECRET_MARKER), false);
        assert(sink.overwritten() >= 0);
    },

    async "package-manifest"(random) {
        const manifestPath = random.pick([
            "packages/npm/sloppy/package.json",
            "packages/npm/sloppy-win32-x64/package.json",
            "stdlib/sloppy/bootstrap.manifest.json",
        ]);
        const text = await readFile(manifestPath, "utf8");
        const parsed = JSON.parse(text);
        validateManifestShape(parsed);
        const mutated = { ...parsed };
        if (random.bool()) {
            delete mutated.version;
        }
        if (random.bool()) {
            mutated.platform = random.pick(["win32-x64", "linux-x64", 42, null]);
        }
        JSON.stringify({ manifestPath, parsed, mutated, staleArtifact: `.sloppy/${textFromPrng(random, 12)}` });
    },

    async "route-table"(random) {
        const app = Sloppy.create();
        app.mapGet("/items", () => Results.ok({ route: "static" })).withName("Items.List");
        app.mapGet("/items/{id:int}", ({ route }) => Results.ok({ route: "param", id: route.id })).withName("Items.Get");
        app.mapPost("/items", ({ request }) => Results.created("/items/1", { length: request.contentLength ?? 0 }));
        maybeThrows(() => app.mapGet(random.pick(["/items", "/bad//path", "{missing-root}"]), () => Results.ok({})));
        const host = Testing.createHost(app);
        try {
            const response = await host.request(
                random.pick(["GET", "POST", "DELETE", "HEAD"]),
                random.pick(["/items", "/items/1", "/missing", "/items/not-int"]),
                { json: { value: textFromPrng(random, 12) } },
            );
            assert(Number.isInteger(response.status));
            if (response.status === 200) {
                const body = await response.json();
                assert(["static", "param"].includes(body.route));
            }
        } finally {
            await host.close();
        }
    },

    async "required-features"(random) {
        const known = new Set([
            "stdlib.fs",
            "stdlib.time",
            "stdlib.crypto",
            "stdlib.codec",
            "stdlib.net",
            "stdlib.os",
            "stdlib.httpclient",
            "stdlib.workers",
        ]);
        const features = Array.from({ length: random.int(6) }, () =>
            random.bool() ? random.pick([...known]) : `unknown.${textFromPrng(random, 8)}`);
        for (const feature of features) {
            assert.equal(typeof feature, "string");
            if (!known.has(feature)) {
                assert.match(feature, /^unknown\./);
            }
        }
        await maybeRejects(() => HttpClient.get(`http://127.0.0.1:${1 + random.int(65534)}/`, {
            timeoutMs: random.pick([0, 1, 10]),
        }));
        maybeThrows(() => Deadline.after(random.pick([0, 1, 10, Number.NaN, -1])));
    },

    async "http-client-options"(random) {
        const options = {
            method: random.pick(["GET", "POST", "PUT", ""]),
            headers: random.bool() ? { [randomHeaderName(random, true)]: randomHeaderValue(random, true) } : { "Bad\nName": "x" },
            timeoutMs: random.pick([0, 1, 10, -1, "soon"]),
            maxResponseBytes: random.pick([0, 1, "1kb", "4 parsecs"]),
            redirects: random.pick([false, true, { max: random.int(4), allowPost: random.bool() }, { max: 99 }]),
            body: random.pick([undefined, "text", new Uint8Array([1, 2, 3]), Symbol("bad")]),
        };
        await maybeRejects(() => HttpClient.request(`http://127.0.0.1:${1 + random.int(65534)}/`, options));
    },

    "results-headers"(random) {
        assertResultDescriptor(Results.ok(jsonValue(random, 1)));
        assertResultDescriptor(Results.noContent());
        maybeThrows(() => Results.created(`/created/${random.int(1000)}`, jsonValue(random, 1), {
            headers: { [randomHeaderName(random, true)]: randomHeaderValue(random, true) },
        }));
        maybeThrows(() => Results.status(random.pick([100, 200, 299, 600, -1]), jsonValue(random, 1)));
        maybeThrows(() => Results.text("bad", {
            headers: { [randomHeaderName(random, false)]: randomHeaderValue(random, false) },
        }));
        maybeThrows(() => Results.problem({ title: "problem", extension: jsonValue(random, 1) }, {
            status: random.pick([400, 500, 99, 1000]),
        }));
        assert.deepEqual(ProblemDetails.defaults(), {
            __sloppyErrorPolicy: true,
            __sloppyProblemDetails: true,
            detail: "never",
        });
    },

    "schema-validation"(random) {
        const Profile = Schema.object({
            name: Schema.string().min(1).max(32),
            email: Schema.string().email().optional(),
            age: Schema.integer().min(0).max(130).default(42),
            tags: Schema.array(Schema.string().max(12)).default([]),
            mode: Schema.enum(["alpha", "beta", "stable"]).nullable(),
        });
        const candidate = {
            name: random.bool() ? textFromPrng(random, 16) || "Ada" : random.pick(["", 7, null]),
            email: random.bool() ? "ada@example.test" : random.pick(["not-email", undefined, 42]),
            age: random.bool() ? random.int(140) : random.pick([undefined, "old", Number.NaN]),
            tags: random.bool() ? [textFromPrng(random, 8), "tag"] : random.pick([undefined, ["this-tag-is-too-long"], "tag"]),
            mode: random.pick(["alpha", "beta", "stable", null, "unknown"]),
        };
        const result = Profile.validate(candidate);
        assert.equal(typeof result.ok, "boolean");
        assert.equal(Object.isFrozen(Profile.metadata), true);
        assert.doesNotThrow(() => JSON.stringify(Profile.metadata));
        if (result.ok) {
            assert.equal(typeof result.value.name, "string");
            assert.equal(Number.isInteger(result.value.age), true);
        } else {
            assert(Array.isArray(result.issues));
            for (const current of result.issues) {
                assert(Array.isArray(current.path));
                assert.equal(typeof current.code, "string");
            }
            const problem = Schema.validationProblem(result.issues);
            assert.equal(problem.code, "SLOPPY_E_VALIDATION_FAILED");
            assert.equal(JSON.stringify(problem).includes(SECRET_MARKER), false);
        }
        maybeThrows(() => Schema.object({ bad: "not-a-schema" }));
        maybeThrows(() => Schema.string().pattern("not-regexp"));
        maybeThrows(() => Schema.integer().default(1.5));
    },

    async "json-serialization"(random) {
        const bytes = bytesFromPrng(random, 16);
        const value = {
            created_at: new Date(Date.UTC(2026, random.int(11), 1 + random.int(20))),
            count: BigInt(random.int(10000)),
            bytes,
            nested: { keep_null: null, omit_me: undefined },
            error: Object.assign(new Error("expected"), { code: "SLOPPY_E_EXPECTED" }),
        };
        const options = {
            json: {
                casing: random.pick(["preserve", "camelCase"]),
                includeNulls: random.bool(),
                bigint: random.pick(["string", "error"]),
                bytes: random.pick(["base64", "array"]),
            },
        };
        maybeThrows(() => Results.json(value, options));
        maybeThrows(() => Results.ok({ bad: random.pick([Number.NaN, Infinity, -Infinity]) }));
        const circular = {};
        circular.self = circular;
        assert.equal(maybeThrows(() => Results.json(circular)).ok, false);
        const streamed = await maybeRejects(() => Results.stream((writer) => {
            writer.writeText(textFromPrng(random, 16));
            writer.writeBytes(bytes);
            if (random.bool()) {
                writer.close();
            }
        }));
        if (streamed.ok) {
            assertResultDescriptor(streamed.value);
            assert.equal(streamed.value.kind, "stream");
        }
        await maybeRejects(() => Results.stream((writer) => {
            writer.writeBytes(new Uint8Array(65537));
        }));
    },

    async "request-media"(random) {
        const app = Sloppy.create();
        app.useErrors({ maxBodyBytes: 256 });
        app.useContentNegotiation({ strictAccept: true });
        const Input = Schema.object({
            name: Schema.string().min(1),
            age: Schema.integer().optional(),
        });
        app.post("/schema", async (ctx) => {
            const input = await ctx.body.validate(Input);
            return Results.ok({
                name: input.name,
                age: input.age ?? null,
                session: ctx.cookies.get("sid"),
            }, {
                json: { casing: "camelCase", includeNulls: false },
            });
        }).accepts(Input).returns(Input);
        app.post("/form", (ctx) => Results.ok({ name: ctx.request.form().get("name") }));
        app.post("/upload", (ctx) => {
            const file = ctx.request.multipart().file("file");
            return Results.text(file === null ? "missing" : file.text());
        });
        const host = Testing.createHost(app);
        try {
            const valid = await host.post("/schema", {
                headers: {
                    accept: random.pick(["application/json", "*/*", "application/*"]),
                    cookie: "sid=abc123; ignored",
                },
                json: { name: textFromPrng(random, 8) || "Ada", age: random.int(100) },
            });
            assert.equal(valid.status, 200);
            assert.equal((await valid.json()).session, "abc123");

            const invalid = await host.post("/schema", {
                headers: { accept: "application/problem+json, application/json" },
                json: { name: "" },
            });
            assert.equal(invalid.status, 400);
            assert.equal((await invalid.json()).code, "SLOPPY_E_VALIDATION_FAILED");

            const unacceptable = await host.post("/schema", {
                headers: { accept: "text/html" },
                json: { name: "Ada" },
            });
            assert.equal(unacceptable.status, 406);

            const form = await maybeRejects(() => host.post("/form", {
                headers: { "content-type": "application/x-www-form-urlencoded" },
                body: `name=${encodeURIComponent(textFromPrng(random, 16) || "Ada")}`,
            }));
            if (form.ok) {
                assert.equal(form.value.status, 200);
            }

            const boundary = `b${random.int(10000)}`;
            const multipart = [
                `--${boundary}`,
                'Content-Disposition: form-data; name="file"; filename="note.txt"',
                "Content-Type: text/plain",
                "",
                textFromPrng(random, 20) || "hello",
                `--${boundary}--`,
                "",
            ].join("\r\n");
            const upload = await host.post("/upload", {
                headers: { "content-type": `multipart/form-data; boundary=${boundary}` },
                body: multipart,
            });
            assert.equal(upload.status, 200);

            const unsupported = await host.post("/schema", {
                headers: { "content-type": "application/xml" },
                body: "<name>Ada</name>",
            });
            assert.equal(unsupported.status, 415);
        } finally {
            await host.close();
        }
    },

    async "realtime-metadata"(random) {
        const app = Sloppy.create();
        app.sse("/events", async (_ctx, stream) => {
            stream.comment("ready");
            stream.event("message", { value: textFromPrng(random, 12) || "ok" }, { id: "1" });
            if (random.bool()) {
                stream.heartbeat();
            }
        }).withName("Events.Stream");
        app.ws("/socket", () => undefined).withName("Socket.Connect");
        const routes = app.__getRoutes();
        assert.equal(routes.find((route) => route.name === "Events.Stream")?.kind, "sse");
        assert.equal(routes.find((route) => route.name === "Socket.Connect")?.kind, "websocket");
        const host = Testing.createHost(app);
        try {
            const events = await host.get("/events");
            assert.equal(events.status, 200);
            assert.equal(events.headers.get("x-slop-realtime"), "sse");
            assert.match(events.text(), /event: message/);
            const socket = await host.get("/socket");
            assert.equal(socket.status, 501);
            assert.equal(socket.headers.get("x-slop-realtime"), "websocket");
            assert.equal((await socket.json()).code, "SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE");
        } finally {
            await host.close();
        }
        const hub = Realtime.hub(`hub${random.int(1000)}`);
        const connection = hub.register();
        connection.join("admins");
        await hub.group("admins").sendJson({ ok: true });
        assert.equal(hub.__debug().connections[0].messages[0].json.ok, true);
        assert.equal(connection.close(1000, "done"), true);
    },

    async "worker-queue"(random) {
        const queue = WorkQueue.create(`q${random.int(99999)}`, {
            maxQueued: 1 + random.int(4),
            concurrency: 1 + random.int(2),
            overflow: random.pick(["reject", "backpressure"]),
            retry: { maxAttempts: 1 + random.int(3), backoffMs: 0 },
        });
        queue.process(async (job) => {
            if (job.data.fail && job.attempt === 1) {
                throw new Error("planned failure");
            }
            return { seen: job.data };
        });
        const jobs = [];
        const count = 1 + random.int(3);
        for (let index = 0; index < count; index += 1) {
            jobs.push(maybeRejects(() => queue.enqueue({ index, payload: textFromPrng(random, 12), fail: index === 0 && random.bool() })));
        }
        await Promise.all(jobs);
        const controller = new CancellationController();
        controller.cancel("cancelled before enqueue");
        await maybeRejects(() => queue.enqueue({ cancelled: true }, { signal: controller.signal }));
        await queue.stop();
        await queue.stop();
    },

    async "h2-client-options"(random) {
        await maybeRejects(() => HttpClient.request("http://example.test/", {
            protocol: "h2",
            timeoutMs: 1,
        }));
        await maybeRejects(() => HttpClient.request("https://example.test/", {
            protocol: "h2c",
            timeoutMs: 1,
        }));
        await maybeRejects(() => HttpClient.request("http://example.test/", {
            protocol: random.pick(["auto", "http/1.1", "h2c"]),
            tls: random.pick([undefined, {}, { insecureSkipVerify: true }, { alpnProtocols: ["h2"] }]),
            timeoutMs: 1,
        }));
    },

    "stdlib-import-shapes"(random) {
        const groupName = random.pick(Object.keys(STD_LIB_EXPORTS));
        const exportName = random.pick(STD_LIB_EXPORTS[groupName]);
        assert(["function", "object"].includes(typeof Stdlib[exportName]), `${exportName} should be exported`);
        const generatedImport = `import { ${exportName} } from "sloppy${groupName === "root" ? "" : `/${groupName}`}";`;
        assert.match(generatedImport, /^import \{ [A-Za-z0-9]+ \} from "sloppy/);
        assert.equal(generatedImport.includes("node:"), false);

        const bytes = bytesFromPrng(random, 64);
        assertBytesEqual(Base64.decode(Base64.encode(bytes)), bytes);
        assertBytesEqual(Base64Url.decode(Base64Url.encode(bytes), { padding: "forbidden" }), bytes);
        assertBytesEqual(Hex.decode(Hex.encode(bytes)), bytes);
        assert.equal(Text.utf8.decode(Text.utf8.encode(textFromPrng(random, 16)), { fatal: true }).length >= 0, true);
    },

    async "ops-health-metrics"(random) {
        const metrics = Metrics.createRegistry({ maxLabelSetsPerMetric: 4 });
        const counter = metrics.counter(`fuzz.metric.${random.int(100)}`);
        const histogram = metrics.histogram("fuzz.duration.ms", { buckets: [1, 5, 10, 50] });
        for (let index = 0; index < 8; index += 1) {
            counter.inc(1, {
                route: random.pick(["/users/{id}", "/orders/{id}", "/health"]),
                code: String(200 + random.int(5)),
            });
            histogram.observe(random.int(100), { route: "/users/{id}" });
        }
        const health = Health.createRegistry();
        health
            .check("self", Health.self(), { tags: ["live", "ready"], critical: true })
            .check("custom", () => ({
                status: random.pick(["healthy", "degraded", "unhealthy"]),
                data: {
                    token: SECRET_MARKER,
                    value: textFromPrng(random, 16),
                },
            }), { tags: ["ready"], critical: random.bool(), cacheMs: random.int(5) });
        const result = await health.evaluate(random.pick(["health", "live", "ready", "startup"]));
        const prometheus = metrics.renderPrometheus();
        assert.equal(JSON.stringify(result).includes(SECRET_MARKER), false);
        assert(["healthy", "degraded", "unhealthy"].includes(result.status));
        assert.equal(prometheus.includes("fuzz_metric_"), true);
    },
});

async function writeFailureArtifact(root, target, payload) {
    const directory = path.join(root, target);
    await mkdir(directory, { recursive: true });
    const file = path.join(directory, `${payload.seed}-${payload.iteration}.json`);
    await writeFile(file, JSON.stringify(payload, null, 2), "utf8");
    return file;
}

export async function runTargets(options) {
    const selected = options.all ? DEFAULT_JS_TARGETS : [options.target];
    for (const name of selected) {
        if (!Object.hasOwn(targets, name)) {
            throw new Error(`unknown target '${name}'`);
        }
        for (let iteration = 0; iteration < options.iterations; iteration += 1) {
            const iterationSeed = (options.seed + Math.imul(iteration + 1, 0x9e3779b1)) >>> 0;
            const random = makePrng(iterationSeed);
            try {
                await targets[name](random, iteration);
            } catch (error) {
                const repro = options.reproCommand ||
                    `node tests/fuzz/js_fuzz_targets.mjs --target ${name} --iterations ${iteration + 1} --seed ${options.seed}`;
                const artifact = await writeFailureArtifact(options.failureRoot, name, {
                    target: name,
                    seed: options.seed,
                    iteration,
                    iterationSeed,
                    repro,
                    error: {
                        name: error?.name,
                        message: error?.message,
                        stack: error?.stack,
                    },
                });
                error.message = `${error.message} (seed=${options.seed} target=${name} iteration=${iteration} artifact=${artifact})`;
                throw error;
            }
        }
        console.log(`js-fuzz ${name} pass iterations=${options.iterations} seed=${options.seed}`);
    }
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
        console.log(usage());
        return;
    }
    await runTargets(options);
}

if (process.argv[1] !== undefined && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) {
    main().catch((error) => {
        console.error(error?.stack ?? String(error));
        process.exitCode = 1;
    });
}
