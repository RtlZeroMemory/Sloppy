import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";

import {
    Base64,
    Base64Url,
    Binary,
    CancellationController,
    Deadline,
    Hex,
    HttpClient,
    column,
    orm,
    ProblemDetails,
    RateLimit,
    Realtime,
    Results,
    schema,
    Sloppy,
    table,
    TestHost,
    Text,
    Time,
    WorkQueue,
} from "../../../stdlib/sloppy/index.js";

const DEFAULT_PROPERTY_TARGETS = Object.freeze([
    "codec",
    "results",
    "time",
    "http-client",
    "static-files",
    "rate-limit",
    "realtime",
    "workers",
    "logging",
    "config",
    "orm",
]);

const SECRET_MARKER = "SECRET_PROPERTY_VALUE_SHOULD_NOT_APPEAR";

function parseArgs(argv) {
    const options = {
        seed: 12345,
        iterations: 1000,
        target: "",
        all: false,
        failureRoot: "artifacts/property/failures",
    };
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index];
        switch (arg) {
            case "--seed":
                options.seed = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "--iterations":
                options.iterations = Number.parseInt(argv[++index] ?? "", 10);
                break;
            case "--target":
                options.target = argv[++index] ?? "";
                break;
            case "--all":
                options.all = true;
                break;
            case "--failure-root":
                options.failureRoot = argv[++index] ?? options.failureRoot;
                break;
            case "-h":
            case "--help":
                options.help = true;
                break;
            default:
                throw new Error(`unknown option '${arg}'`);
        }
    }
    if (!Number.isSafeInteger(options.seed)) {
        throw new Error("--seed must be an integer");
    }
    if (!Number.isSafeInteger(options.iterations) || options.iterations < 1) {
        throw new Error("--iterations must be a positive integer");
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
        "Usage: node tests/bootstrap/property/run_property_tests.mjs [--target NAME|--all] [--seed N] [--iterations N]",
        "",
        `Targets: ${DEFAULT_PROPERTY_TARGETS.join(", ")}`,
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

function randomBytes(random, maxLength = 4096) {
    const bytes = new Uint8Array(random.int(maxLength + 1));
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = random.nextU32() & 0xff;
    }
    return bytes;
}

function randomText(random, maxLength = 96) {
    let output = "";
    const length = random.int(maxLength + 1);
    for (let index = 0; index < length; index += 1) {
        const bucket = random.int(6);
        if (bucket === 0) {
            output += "\0";
        } else if (bucket === 1) {
            output += String.fromCodePoint(0x20 + random.int(0x5f));
        } else if (bucket === 2) {
            output += String.fromCodePoint(0x80 + random.int(0x700));
        } else if (bucket === 3) {
            output += String.fromCodePoint(0x800 + random.int(0x7000));
        } else if (bucket === 4) {
            output += String.fromCodePoint(0x10000 + random.int(0x10000));
        } else {
            output += random.pick(["authorization", "cookie", "token", "password", "apiKey"]);
        }
    }
    return output;
}

function assertBytesEqual(actual, expected) {
    assert.deepEqual(Array.from(actual), Array.from(expected));
}

function assertThrows(fn, pattern = /./) {
    assert.throws(fn, (error) => {
        assert.match(String(error?.message ?? error), pattern);
        return true;
    });
}

function assertThrowsCode(fn, code) {
    assert.throws(fn, (error) => {
        assert.equal(error.code, code);
        return true;
    });
}

async function assertRejects(fn, pattern = /./) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error?.message ?? error), pattern);
        return true;
    });
}

function assertResultDescriptor(result) {
    assert.equal(result.__sloppyResult, true);
    assert.equal(Object.isFrozen(result), true);
    assert.equal(typeof result.status, "number");
}

function jsonValue(random, depth = 0) {
    if (depth > 3) {
        return random.pick([null, true, false, random.int(100000), randomText(random, 24)]);
    }
    switch (random.int(6)) {
        case 0:
            return null;
        case 1:
            return random.bool();
        case 2:
            return random.int(1000000);
        case 3:
            return randomText(random, 32);
        case 4:
            return Array.from({ length: random.int(5) }, () => jsonValue(random, depth + 1));
        default: {
            const object = {};
            const count = random.int(5);
            for (let index = 0; index < count; index += 1) {
                object[`k${index}_${random.int(999)}`] = jsonValue(random, depth + 1);
            }
            return object;
        }
    }
}

const properties = Object.freeze({
    codec(random) {
        const bytes = randomBytes(random);
        assertBytesEqual(Base64.decode(Base64.encode(bytes)), bytes);
        assertBytesEqual(Base64Url.decode(Base64Url.encode(bytes), { padding: "forbidden" }), bytes);
        assertBytesEqual(Base64Url.decode(Base64Url.encode(bytes, { padding: true }), { padding: "optional" }), bytes);
        assertBytesEqual(Base64Url.decode(Base64Url.encode(bytes, { padding: true }), { padding: "required" }), bytes);
        assertBytesEqual(Hex.decode(Hex.encode(bytes)), bytes);

        const text = randomText(random);
        assert.equal(Text.utf8.decode(Text.utf8.encode(text), { fatal: true }), text);
        assertThrows(() => Text.utf8.decode(random.pick([
            new Uint8Array([0xc0, 0xaf]),
            new Uint8Array([0xe2, 0x82]),
            new Uint8Array([0xed, 0xa0, 0x80]),
        ]), { fatal: true }), /SLOPPY_E_CODEC_MALFORMED_UTF8/);
        assertThrows(() => Hex.decode("f"), /SLOPPY_E_CODEC_INVALID_HEX/);
        assertThrows(() => Hex.decode("00xz"), /SLOPPY_E_CODEC_INVALID_HEX/);

        const u8 = random.int(0x100);
        const u16 = random.int(0x10000);
        const u32 = random.nextU32();
        const i32 = (random.nextU32() | 0);
        const writer = Binary.writer({ initialCapacity: 1, maxCapacity: 64 });
        writer.u8(u8).u16le(u16).u16be(u16).u32le(u32).u32be(u32).i32le(i32).i32be(i32);
        const reader = Binary.reader(writer.toBytes());
        assert.equal(reader.u8(), u8);
        assert.equal(reader.u16le(), u16);
        assert.equal(reader.u16be(), u16);
        assert.equal(reader.u32le(), u32);
        assert.equal(reader.u32be(), u32);
        assert.equal(reader.i32le(), i32);
        assert.equal(reader.i32be(), i32);
        assert.equal(reader.remaining(), 0);
        assertThrows(() => reader.u8(), /SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS/);

        const capped = Binary.writer({ initialCapacity: 1, maxCapacity: 2 });
        capped.u8(1).u8(2);
        assertThrows(() => capped.u8(3), /BinaryWriter\.u8/);
    },

    results(random) {
        const headers = {};
        const headerCount = random.int(4);
        for (let index = 0; index < headerCount; index += 1) {
            headers[`x-prop-${index}`] = randomText(random, 24).replace(/[\r\n\0]/g, "_");
        }
        const body = jsonValue(random);
        const text = Results.text(randomText(random, 64), { status: 200 + random.int(50), headers });
        assertResultDescriptor(text);
        assert.equal(text.contentType, "text/plain; charset=utf-8");

        const json = Results.json(body, { headers });
        assertResultDescriptor(json);
        assert.deepEqual(json.body, body);
        assert.equal(json.contentType, "application/json; charset=utf-8");

        const created = Results.created(`/items/${random.int(1000)}`, body);
        assert.equal(created.status, 201);
        assert.match(created.location, /^\/items\//);

        const noContent = Results.noContent();
        assert.equal(noContent.status, 204);
        assert.equal(Object.prototype.hasOwnProperty.call(noContent, "body"), false);

        const problem = Results.problem({ title: "broken", detail: randomText(random, 20), traceId: String(random.int(9999)) }, {
            status: 500,
        });
        assert.equal(problem.kind, "problem");
        assert.equal(problem.body.status, 500);
        assert.equal(problem.contentType, "application/problem+json; charset=utf-8");

        assert.deepEqual(ProblemDetails.defaults({ detail: "never" }), {
            __sloppyErrorPolicy: true,
            __sloppyProblemDetails: true,
            detail: "never",
        });
        assertThrows(() => ProblemDetails.defaults({ detail: "sometimes" }), /detail policy/);
        assertThrows(() => Results.ok("bad", { status: 99 }), /status/);
        assertThrows(() => Results.ok("bad", { headers: { "bad header": "1" } }), /header names/);
        assertThrows(() => Results.ok("bad", { headers: { "Content-Type": "text/plain" } }), /header names/);
        assertThrows(() => Results.ok("bad", { headers: { "x-test": "a\r\nb" } }), /header value/);
        assertThrows(() => Results.created("/bad\r\nlocation", body), /header value/);
    },

    async time(random) {
        assert.equal(Deadline.never().remainingMs(), Infinity);
        const deadline = Deadline.after(1 + random.int(20));
        assert.equal(deadline.kind, "after");
        assert(deadline.remainingMs() >= 0);

        const controller = new CancellationController();
        let first = 0;
        let second = 0;
        controller.signal.addEventListener("abort", () => { first += 1; });
        controller.signal.addEventListener("abort", () => { second += 1; });
        assert.equal(controller.cancel("caller"), true);
        assert.equal(controller.cancel("again"), false);
        assert.equal(first, 1);
        assert.equal(second, 1);
        assert.equal(controller.signal.aborted, true);
        assert.equal(controller.signal.reason, "caller");

        const fastClock = Time.fakeClock();
        try {
            assert.equal(await Time.timeout(Promise.resolve("fast"), { afterMs: 10, clock: fastClock }), "fast");
        } finally {
            fastClock.dispose();
        }

        const slowClock = Time.fakeClock();
        try {
            const slow = Time.timeout(new Promise(() => {}), { afterMs: 5, clock: slowClock });
            slowClock.advanceBy(5);
            await assertRejects(() => slow, /exceeded its deadline|SLOPPY_E_TIME_TIMEOUT/);
        } finally {
            slowClock.dispose();
        }

        const orderedClock = Time.fakeClock({ now: 1000 });
        try {
            const order = [];
            const firstDelay = orderedClock.delay(10).then(() => order.push("first"));
            const secondDelay = orderedClock.delay(5).then(() => order.push("second"));
            orderedClock.advanceBy(5);
            await secondDelay;
            orderedClock.advanceBy(5);
            await firstDelay;
            assert.deepEqual(order, ["second", "first"]);
        } finally {
            orderedClock.dispose();
        }
    },

    async "http-client"(random) {
        const method = random.pick(["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"]);
        const client = HttpClient.create({
            headers: { "x-default": "1", authorization: "Bearer redacted-descriptor-input" },
            redirects: { max: random.int(4), allowPost: random.bool(), crossOriginSensitiveHeaders: "strip" },
            secretHeaders: ["x-default"],
            maxResponseBytes: random.pick([0, 1, "4kb"]),
            pool: { maxConnectionsPerOrigin: 1 + random.int(8), idleTimeoutMs: random.int(1000) },
            tls: { insecureSkipVerify: random.bool(), clientPrivateKeyPassphrase: SECRET_MARKER },
        });
        const descriptor = client.__sloppyHttpClientOptions;
        assert.equal(descriptor.tls.hasClientPrivateKeyPassphrase, true);
        assert.equal(JSON.stringify(descriptor).includes(SECRET_MARKER), false);

        await assertRejects(() => HttpClient.request("ftp://example.test/", { method }), /http:\/\/ and https:\/\//);
        await assertRejects(() => HttpClient.request("http://example.test/", { method: "BAD METHOD" }), /method/);
        await assertRejects(() => HttpClient.request("http://example.test/", { redirects: { max: 99 } }), /redirects\.max/);
        await assertRejects(() => HttpClient.request("http://example.test/", { redirects: "follow" }), /redirects/);
        await assertRejects(() => HttpClient.request("http://example.test/", { tls: { caPath: 1 } }), /tls option caPath/);
        await assertRejects(() => HttpClient.request("http://example.test/", { tls: { insecureSkipVerify: true } }), /tls options/);
        await assertRejects(() => HttpClient.request("http://example.test/", { maxResponseBytes: "4 parsecs" }), /size option/);
        await assertRejects(
            () => HttpClient.request("http://example.test/", { body: Symbol("bad") }),
            /body|outbound HTTP client transport|SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
        );
        await assertRejects(
            () => HttpClient.request("http://example.test/", { protocol: "h2" }),
            /h2 requires an https|outbound HTTP client transport|SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
        );
        await assertRejects(
            () => HttpClient.request("https://example.test/", { protocol: "h2c" }),
            /h2c requires an http|outbound HTTP client transport|SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
        );
        await assertRejects(() => HttpClient.request("http://example.test/", { headers: { "bad name": "x" } }), /header name/);
        await assertRejects(() => HttpClient.request("http://example.test/", { headers: { "x-test": "a\r\nb" } }), /header value/);
    },

    async "static-files"(random) {
        const previousCwd = process.cwd();
        const root = await mkdtemp(path.join(tmpdir(), "sloppy-static-property-"));
        const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789-_.";
        let bytes = "";
        for (let index = 0; index < 8 + random.int(48); index += 1) {
            bytes += alphabet[random.int(alphabet.length)];
        }
        try {
            await mkdir(path.join(root, "public"), { recursive: true });
            await writeFile(path.join(root, "public", "asset.txt"), bytes);
            await writeFile(path.join(root, "public", ".hidden"), "hidden");
            process.chdir(root);

            const app = Sloppy.create();
            app.staticFiles("/assets", {
                root: "public",
                dotfiles: "deny",
                range: true,
            });
            const host = await TestHost.create(app);
            try {
                const response = await host.get("/assets/asset.txt");
                response.expectStatus(200).expectText(bytes);
                const etag = response.headers.get("etag");
                assert.equal(typeof etag, "string");

                const conditional = await host.get("/assets/asset.txt", {
                    headers: { "if-none-match": etag },
                });
                conditional.expectStatus(304).expectNoBody();

                const start = random.int(Math.max(0, bytes.length - 1));
                const ranged = await host.get("/assets/asset.txt", {
                    headers: { range: `bytes=${start}-` },
                });
                ranged.expectStatus(206).expectText(bytes.slice(start));

                const traversal = await host.get("/assets/%2e%2e/secret.txt");
                traversal.expectStatus(403);
                const hidden = await host.get("/assets/.hidden");
                hidden.expectStatus(403);
            } finally {
                await host.close();
            }
        } finally {
            process.chdir(previousCwd);
            await rm(root, { recursive: true, force: true });
        }
    },

    async "rate-limit"(random) {
        const safeHeader = `X-Prop-${random.int(99999)}`;
        assert.equal(RateLimit.partition.header(safeHeader).metadata.name, safeHeader.toLowerCase());
        assertThrows(() => RateLimit.partition.header(random.pick(["bad header", "x\nbad", ""])), /HTTP token/);

        const directIp = RateLimit.partition.ip();
        const trustedIp = RateLimit.partition.ip({ trustProxy: true });
        const headers = new Map([["x-forwarded-for", "203.0.113.77, 10.0.0.2"]]);
        const ctx = {
            connection: { remoteAddress: "198.51.100.9" },
            request: { headers },
            route: { tenant: "core" },
            user: {
                authenticated: true,
                sub: "user-1",
                claims: { tenant: "core" },
            },
        };
        assert.equal(directIp.resolve(ctx), "ip:198.51.100.9");
        assert.equal(trustedIp.resolve(ctx), "ip:203.0.113.77");
        assert.equal(RateLimit.partition.routeParam("tenant").resolve(ctx), "core");
        assert.equal(RateLimit.partition.claim("tenant").resolve(ctx), "core");

        const store = RateLimit.memory({
            name: `property-${random.nextU32()}`,
            maxKeys: 8,
        });
        const windowMs = 1 + random.int(5000);
        const policy = RateLimit.fixedWindow({
            name: `fixed-${random.nextU32()}`,
            limit: 1,
            windowMs,
            partitionBy: RateLimit.partition.ip(),
        });
        const first = await store.check({ policy, cost: 1, partitionHash: "same", nowMs: 0 });
        assert.equal(first.allowed, true);
        assert.equal(first.remaining, 0);
        const denied = await store.check({ policy, cost: 1, partitionHash: "same", nowMs: 0 });
        assert.equal(denied.allowed, false);
        assert.ok(denied.retryAfterMs >= 1);
        assert.ok(denied.retryAfterMs <= windowMs);
        const reset = await store.check({ policy, cost: 1, partitionHash: "same", nowMs: windowMs });
        assert.equal(reset.allowed, true);

        const bucket = RateLimit.tokenBucket({
            name: `bucket-${random.nextU32()}`,
            capacity: 1,
            refillPerSecond: 1 + random.int(10),
            partitionBy: "global",
        });
        const bucketStore = RateLimit.memory({ name: `bucket-store-${random.nextU32()}` });
        assert.equal((await bucketStore.check({ policy: bucket, cost: 1, partitionHash: "global", nowMs: 0 })).allowed, true);
        const bucketDenied = await bucketStore.check({ policy: bucket, cost: 1, partitionHash: "global", nowMs: 0 });
        assert.equal(bucketDenied.allowed, false);
        assert.ok(bucketDenied.retryAfterMs >= 1);
    },

    realtime(random, iteration) {
        const maxLength = 1 + random.int(32);
        const text = "x".repeat(random.int(maxLength + 1));
        const Channel = Realtime.channel(`property${iteration}`, {
            client: {
                sendValue: schema.object({
                    text: schema.string().maxLength(maxLength),
                }),
            },
            server: {
                didReceive: schema.object({
                    text: schema.string().maxLength(maxLength),
                }),
            },
        });

        const parsed = Channel.parseClientMessage(JSON.stringify({
            type: "sendValue",
            data: { text },
            id: `client-${iteration}`,
        }));
        assert.deepEqual(parsed, {
            type: "sendValue",
            data: { text },
            id: `client-${iteration}`,
        });

        const server = Channel.serializeServerMessage("didReceive", { text });
        assert.deepEqual(server, { type: "didReceive", data: { text } });
        assert.equal(Channel.stringifyServerMessage("didReceive", { text }), JSON.stringify(server));

        assertThrowsCode(() => Channel.parseClientMessage("{"), "SLOPPY_E_REALTIME_MALFORMED_JSON");
        assertThrowsCode(
            () => Channel.parseClientMessage(JSON.stringify({ type: "unknown", data: {} })),
            "SLOPPY_E_REALTIME_UNKNOWN_EVENT",
        );
        assertThrowsCode(
            () => Channel.parseClientMessage(JSON.stringify({ type: "sendValue" })),
            "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
        );
        assertThrowsCode(
            () => Channel.parseClientMessage(JSON.stringify({ type: "sendValue", data: { text: "x".repeat(maxLength + 1) } })),
            "SLOPPY_E_REALTIME_VALIDATION_FAILED",
        );
        assertThrowsCode(
            () => Channel.serializeServerMessage("didReceive", { text: "x".repeat(maxLength + 1) }),
            "SLOPPY_E_REALTIME_VALIDATION_FAILED",
        );
    },

    async workers(random) {
        assertThrows(() => WorkQueue.create("", {}), /name/);
        assertThrows(() => WorkQueue.create("bad-capacity", { maxQueued: 0 }), /positive integer/);
        assertThrows(() => WorkQueue.create("bad-overflow", { overflow: "drop" }), /overflow/);

        const queue = WorkQueue.create(`property-q-${random.nextU32()}`, {
            maxQueued: 2 + random.int(3),
            concurrency: 1 + random.int(2),
            overflow: "reject",
            retry: { maxAttempts: 2, backoffMs: 0 },
        });
        try {
            let active = 0;
            let maxActive = 0;
            const seen = [];
            queue.process(async (job) => {
                active += 1;
                maxActive = Math.max(maxActive, active);
                seen.push(job.data.index);
                await Promise.resolve();
                active -= 1;
                if (job.data.failOnce && job.attempt === 1) {
                    throw new Error("planned retry");
                }
                return job.data.index * 2;
            });
            const jobs = [];
            const count = 1 + random.int(3);
            for (let index = 0; index < count; index += 1) {
                jobs.push(queue.enqueue({ index, failOnce: index === 0 && random.bool() }));
            }
            const values = await Promise.all(jobs);
            assert.deepEqual(values, Array.from({ length: count }, (_value, index) => index * 2));
            assert(maxActive <= queue.state.concurrency);
            assert(seen.length >= count);
            await queue.drain();
            await queue.stop();
            await queue.stop();
            await assertRejects(() => queue.enqueue({ late: true }), /SLOPPY_E_WORK_QUEUE_STOPPED/);
        } finally {
            await queue.stop();
        }

        const controller = new CancellationController();
        controller.cancel("before enqueue");
        const cancelled = WorkQueue.create(`cancelled-q-${random.nextU32()}`, { maxQueued: 1, concurrency: 1 });
        try {
            cancelled.process(async () => "never");
            await assertRejects(() => cancelled.enqueue({ ok: false }, { signal: controller.signal }), /SLOPPY_E_WORK_JOB_CANCELLED/);
        } finally {
            await cancelled.stop();
        }
    },

    logging(random) {
        const builder = Sloppy.createBuilder();
        const sink = builder.logging.addMemorySink({ capacity: 3 });
        builder.logging.setMinimumLevel("debug");
        builder.logging.addRedactionKey("connectionString");
        const app = builder.build();
        const keys = ["authorization", "cookie", "token", "password", "connectionString", "apiKey"];
        for (const key of keys) {
            app.log.info("secret event", { [key]: SECRET_MARKER, visible: randomText(random, 12) });
        }
        app.log.debug("escaped", { value: `line\nquote"${randomText(random, 10)}` });
        app.log.trace("filtered", { token: SECRET_MARKER });
        const entries = sink.entries();
        assert(entries.length <= 3);
        assert(sink.overwritten() >= 0);
        const serialized = JSON.stringify(entries);
        assert.equal(serialized.includes(SECRET_MARKER), false);
        assert.equal(app.log.isEnabled("trace"), false);
        assert.equal(app.log.isEnabled("debug"), true);
        assertThrows(() => app.log.info("bad", { nested: { no: true } }), /shallow plain object|log fields|finite number/);
        assertThrows(() => Sloppy.createBuilder().logging.addMemorySink({ capacity: 0 }), /capacity/);
        assertThrows(() => Sloppy.createBuilder().logging.writeTo.console({ format: "xml" }), /format/);
    },

    config(random) {
        const builder = Sloppy.createBuilder();
        const port = 1024 + random.int(50000);
        const name = randomText(random, 16).replace(/[\0\r\n]/g, "") || "property-app";
        builder.config.addObject({
            App: {
                Name: name,
                Port: String(port),
                Enabled: random.bool() ? "true" : "false",
                Tags: ["a", "b", String(random.int(99))],
                Secrets: { ApiKey: SECRET_MARKER },
            },
        });
        assert.equal(builder.config.getString("App:Name", "fallback"), name);
        assert.equal(builder.config.getInt("App:Port", 0), port);
        assert(Array.isArray(builder.config.getArray("App:Tags", [])));
        const bound = builder.config.bind("App", {
            name: { type: "string" },
            port: { type: "integer", min: 1, max: 65535 },
            enabled: { type: "boolean", default: false },
        });
        assert.equal(bound.name, name);
        assert.equal(bound.port, port);
        assert.equal(typeof builder.config.__snapshot(), "object");
        assertThrows(() => builder.config.getString("", "fallback"), /key/);
        assertThrows(() => builder.config.bind("App", { broken: { type: "decimal" } }), /type/);
    },

    orm(random, iteration) {
        const suffix = `${iteration}_${random.int(1_000_000)}`;
        const tableName = `orm_property_${suffix}`;
        const columnName = `value_${random.int(1_000_000)}`;
        const Model = table(tableName, {
            id: column.uuid().primaryKey(),
            [columnName]: column.text().nullable().index(),
        });
        const snapshot = orm.migrations.snapshot(Model);
        assert.equal(snapshot.tables[0].name, tableName);
        assert.equal(snapshot.tables[0].columns.some((entry) => entry.name === columnName), true);
        assert.equal(orm.migrations.snapshot(Model).checksum, snapshot.checksum);

        const Expanded = table(tableName, {
            id: column.uuid().primaryKey(),
            [columnName]: column.text().nullable().index(),
            added: column.int().default(0),
        });
        const diff = orm.migrations.diff(snapshot, Expanded, { provider: "sqlite" });
        assert.match(diff.sql, /\balter\s+table\b/iu);
        assert.equal(diff.destructive, false);

        const text = randomText(random, 16);
        if (!/^[A-Za-z_][A-Za-z0-9_]*$/u.test(text)) {
            assertThrows(() => table(text, { id: column.int().primaryKey() }), /safe SQL identifier/);
        }
        assertThrows(() => table(`bad-${suffix}`, { id: column.int().primaryKey() }), /safe SQL identifier/);
    },
});

async function writeFailureArtifact(root, target, payload) {
    const directory = path.join(root, target);
    await mkdir(directory, { recursive: true });
    const file = path.join(directory, `${payload.seed}-${payload.iteration}.json`);
    await writeFile(file, JSON.stringify(payload, null, 2), "utf8");
    return file;
}

async function runPropertyTargets(options) {
    const selected = options.all ? DEFAULT_PROPERTY_TARGETS : [options.target];
    for (const name of selected) {
        if (!Object.hasOwn(properties, name)) {
            throw new Error(`unknown property target '${name}'`);
        }
        for (let iteration = 0; iteration < options.iterations; iteration += 1) {
            const iterationSeed = (options.seed + Math.imul(iteration + 1, 0x9e3779b1)) >>> 0;
            const random = makePrng(iterationSeed);
            try {
                await properties[name](random, iteration);
            } catch (error) {
                const artifact = await writeFailureArtifact(options.failureRoot, name, {
                    target: name,
                    seed: options.seed,
                    iteration,
                    iterationSeed,
                    repro: `node tests/bootstrap/property/run_property_tests.mjs --target ${name} --seed ${options.seed} --iterations ${iteration + 1}`,
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
        console.log(`property ${name} pass iterations=${options.iterations} seed=${options.seed}`);
    }
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
        console.log(usage());
        return;
    }
    await runPropertyTargets(options);
}

main().catch((error) => {
    console.error(error?.stack ?? String(error));
    process.exitCode = 1;
});
