import assert from "node:assert/strict";
import crypto from "node:crypto";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";

import {
    Base64,
    Base64Url,
    Binary,
    CancellationController,
    Cache,
    Deadline,
    Hex,
    HttpClient,
    column,
    orm,
    ProblemDetails,
    RateLimit,
    Realtime,
    Redis,
    Results,
    schema,
    Sloppy,
    table,
    TestHost,
    Text,
    Time,
    Webhooks,
    WorkQueue,
} from "../../../stdlib/sloppy/index.js";

const DEFAULT_PROPERTY_TARGETS = Object.freeze([
    "codec",
    "results",
    "time",
    "http-client",
    "route-patterns",
    "static-files",
    "cache",
    "redis",
    "rate-limit",
    "websocket",
    "webhooks",
    "realtime",
    "schema",
    "scheduler",
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

function safeSegment(random, prefix = "p", maxLength = 16) {
    const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789-_";
    let output = prefix;
    const length = 1 + random.int(Math.max(1, maxLength));
    for (let index = 0; index < length; index += 1) {
        output += alphabet[random.int(alphabet.length)];
    }
    return output;
}

function lowerCaseHeaders(headers) {
    return new Map(Object.entries(headers).map(([name, value]) => [name.toLowerCase(), value]));
}

async function withCryptoBridge(callback) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = {
        ...(previous ?? {}),
        crypto: {
            ...(previous?.crypto ?? {}),
            randomUuid() {
                return crypto.randomUUID();
            },
            randomBytes(length) {
                return new Uint8Array(crypto.randomBytes(length));
            },
            hash(algorithm, bytes) {
                return new Uint8Array(crypto.createHash(algorithm).update(Buffer.from(bytes)).digest());
            },
            hmac(algorithm, key, bytes) {
                return new Uint8Array(crypto.createHmac(algorithm, Buffer.from(key)).update(Buffer.from(bytes)).digest());
            },
            constantTimeEquals(left, right) {
                const a = Buffer.from(left);
                const b = Buffer.from(right);
                return a.length === b.length && crypto.timingSafeEqual(a, b);
            },
        },
    };
    try {
        return await callback();
    } finally {
        if (previous === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previous;
        }
    }
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

    async "route-patterns"(random, iteration) {
        const routeId = 1 + random.int(1_000_000);
        const search = safeSegment(random, "q");
        const app = Sloppy.create();
        app.get(`/property/${iteration}/items/{id:int}`, (ctx) => Results.json({
            id: Number(ctx.route.id),
            query: ctx.query.search,
            header: ctx.request.headers.get("x-property"),
            url: app.urlFor("property.item", { id: ctx.route.id }, { search: ctx.query.search }),
        })).name("property.item");
        app.post(`/property/${iteration}/echo/{slug}`, async (ctx) => Results.json({
            slug: ctx.route.slug,
            body: await ctx.request.json(),
        }));
        const host = await TestHost.create(app);
        try {
            const response = await host.get(`/property/${iteration}/items/${routeId}`)
                .query({ search })
                .header("x-property", "yes")
                .send();
            response.expectStatus(200);
            assert.deepEqual(await response.json(), {
                id: routeId,
                query: search,
                header: "yes",
                url: `/property/${iteration}/items/${routeId}?search=${encodeURIComponent(search)}`,
            });

            await host.get(`/property/${iteration}/items/not-an-int`).expectStatus(404);
            const body = { value: randomText(random, 16).replace(/\0/gu, "") };
            const echoed = await host.post(`/property/${iteration}/echo/${safeSegment(random, "slug")}`).json(body).send();
            echoed.expectStatus(200);
            assert.deepEqual((await echoed.json()).body, body);
            await assertRejects(() => host.get(`/property/${iteration}/items/${routeId}?bad=%zz`), /percent escapes/);
            assertThrows(() => app.urlFor("property.item", { id: "not/an/id" }), /must satisfy 'int'/);
        } finally {
            await host.close();
        }
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
                const missing = await host.get("/assets/missing.js");
                missing.expectStatus(404);
            } finally {
                await host.close();
            }
        } finally {
            process.chdir(previousCwd);
            await rm(root, { recursive: true, force: true });
        }
    },

    async cache(random) {
        const key = Cache.key("property", safeSegment(random, "key"));
        const tag = safeSegment(random, "tag");
        const cache = Cache.memory(`property-${random.nextU32()}`, { maxEntries: 4 });
        try {
            const value = { count: random.int(1000), nested: { text: randomText(random, 16).replace(/\0/gu, "") } };
            await cache.set(key, value, { tags: [tag], maxValueBytes: 4096 });
            assert.deepEqual(await cache.get(key), value);
            const copy = await cache.get(key);
            copy.nested.text = "mutated";
            assert.deepEqual(await cache.get(key), value);
            assert.equal(await cache.invalidateTag(tag), 1);
            assert.equal(await cache.get(key), undefined);

            let current = 1_000;
            const clocked = Cache.memory(`clock-${random.nextU32()}`, {
                maxEntries: 8,
                clock: {
                    now() {
                        return new Date(current);
                    },
                    monotonicNowMs() {
                        return current;
                    },
                },
            });
            try {
                await clocked.set("sliding", { ok: true }, { slidingExpirationMs: 10, absoluteExpiration: new Date(current + 25) });
                current += 5;
                assert.deepEqual(await clocked.get("sliding"), { ok: true });
                current += 26;
                assert.equal(await clocked.get("sliding"), undefined);
            } finally {
                clocked.dispose();
            }

            let runs = 0;
            const [left, right] = await Promise.all([
                cache.getOrCreate("coalesced", { ttlMs: 100 }, async () => {
                    runs += 1;
                    await Promise.resolve();
                    return { winner: true };
                }),
                cache.getOrCreate("coalesced", { ttlMs: 100 }, async () => {
                    runs += 1;
                    return { winner: false };
                }),
            ]);
            assert.deepEqual(left, { winner: true });
            assert.deepEqual(right, { winner: true });
            assert.equal(runs, 1);
            await assertRejects(
                () => cache.set("too-large", { value: "abcdef" }, { maxValueBytes: 2 }),
                /SLOPPY_E_CACHE_VALUE_TOO_LARGE|maxValueBytes/,
            );
            await cache.set("typed", { id: 1 }, { schema: schema.object({ id: schema.integer() }) });
            await assertRejects(() => cache.get("typed", schema.object({ id: schema.string() })), /schema validation/);
            assert.equal(Cache.key("bad\0key"), "bad%00key");
            assertThrows(() => Cache.tags("good", ["bad\0tag"]), /cache tag|cache key/);
        } finally {
            cache.dispose();
        }
    },

    redis(random) {
        const payload = safeSegment(random, "value", 20);
        const encoded = Text.utf8.encode(`*2\r\n$3\r\nGET\r\n$${payload.length}\r\n${payload}\r\n`);
        const parser = new Redis.RespParser({ maxResponseBytes: 1024, maxArrayItems: 8, maxArrayDepth: 4 });
        const split = 1 + random.int(encoded.byteLength - 1);
        parser.feed(encoded.subarray(0, split));
        assert.equal(parser.read(), undefined);
        parser.feed(encoded.subarray(split));
        const value = parser.read();
        assert.equal(Array.isArray(value), true);
        assert.equal(Text.utf8.decode(value[0]), "GET");
        assert.equal(Text.utf8.decode(value[1]), payload);
        assert.equal(parser.read(), undefined);

        const integerParser = new Redis.RespParser();
        integerParser.feed(Text.utf8.encode(`:${random.int(1_000_000)}\r\n`));
        assert.equal(typeof integerParser.read(), "number");

        const errorParser = new Redis.RespParser();
        errorParser.feed(Text.utf8.encode("-ERR broken\r\n"));
        assert.equal(errorParser.read().code, "ERR");

        const capped = new Redis.RespParser({ maxResponseBytes: 4 });
        assertThrows(() => capped.feed(Text.utf8.encode("$5\r\nhello\r\n")), /byte limit/);
        const malformed = new Redis.RespParser();
        malformed.feed(Text.utf8.encode("$2\r\nabxx"));
        assertThrows(() => malformed.read(), /missing CRLF/);

        assert.equal(Redis._redactUrl("redis://user:secret@example.test:6379/0").includes("secret"), false);
        const client = Redis.client("property", { url: "redis://example.test/0" });
        assert.equal(client.diagnostics().url, "redis://example.test/0");
        client.close();
        assertThrows(() => Redis.client("bad name", { url: "redis://example.test/0" }), /letters, digits/);
        assertThrows(() => Redis.client("property", { url: "http://example.test" }), /redis:\/\/ or rediss:\/\//);
        assertThrows(() => Redis.client("property", { url: "redis://example.test/-1" }), /database path/);
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

    async webhooks(random, iteration) {
        await withCryptoBridge(async () => {
            const Event = Webhooks.event("property.created", {
                version: 1,
                schema: schema.object({
                    id: schema.string().minLength(1),
                    count: schema.integer(),
                }),
            });
            const payload = { id: `evt_${iteration}`, count: random.int(10_000) };
            const body = JSON.stringify(payload);
            const timestamp = String(2_000 + random.int(1_000));
            const secret = `secret-${safeSegment(random, "s")}`;
            const signed = await Webhooks.sign(body, {
                secret,
                event: Event.name,
                id: `delivery_${iteration}`,
                timestamp,
                attempt: 1 + random.int(5),
            });
            const request = {
                headers: lowerCaseHeaders(signed.headers),
                bytes() {
                    return Text.utf8.encode(body);
                },
            };
            const seen = new Set();
            const dedupe = {
                seen(id) {
                    return seen.has(id);
                },
                mark(id) {
                    seen.add(id);
                },
            };
            const verified = await Webhooks.verify(request, {
                secret,
                event: Event,
                toleranceMs: 1000,
                nowMs: Number(timestamp) * 1000,
                dedupe,
            });
            assert.deepEqual(verified.payload, payload);
            await assertRejects(() => Webhooks.verify(request, {
                secret,
                event: Event,
                toleranceMs: 1000,
                nowMs: Number(timestamp) * 1000,
                dedupe,
            }), /SLOPPY_E_WEBHOOK_REPLAY_DETECTED/);
            await assertRejects(() => Webhooks.verify(request, {
                secret: `${secret}-wrong`,
                toleranceMs: 1000,
                nowMs: Number(timestamp) * 1000,
            }), /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/);
            await assertRejects(() => Webhooks.verify(request, {
                secret,
                toleranceMs: 1000,
                nowMs: (Number(timestamp) + 2) * 1000,
            }), /SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE/);

            const invalidSigned = await Webhooks.sign(JSON.stringify({ id: "", count: "bad" }), {
                secret,
                event: Event.name,
                id: `delivery_invalid_${iteration}`,
                timestamp: String(Number(timestamp) + 1),
            });
            await assertRejects(() => Webhooks.verify({
                headers: lowerCaseHeaders(invalidSigned.headers),
                async text() {
                    return JSON.stringify({ id: "", count: "bad" });
                },
            }, {
                secret,
                event: Event,
                toleranceMs: 1000,
                nowMs: (Number(timestamp) + 1) * 1000,
            }), /SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED/);

            assertThrows(() => Webhooks.event("not_dotted", { version: 1, schema: Event.schema }), /dotted/);
            assertThrows(() => Webhooks.retry.fixed({ retryOnStatus: [99] }), /valid HTTP statuses/);
            assertThrows(() => Webhooks.outbox({ provider: "main", signingSecret: "" }), /signingSecret/);
        });
    },

    async websocket(random, iteration) {
        const protocol = `sloppy.prop${iteration}`;
        const origin = `https://${safeSegment(random, "origin")}.example`;
        const route = `/property/ws/${iteration}`;
        const app = Sloppy.create();
        app.ws(route, {
            protocols: [protocol],
            origins: [origin],
            maxMessageBytes: 256,
            maxSendQueueBytes: 2048,
        }, async (_ctx, socket) => {
            await socket.accept();
            await socket.sendText("ready");
            const message = await socket.messages().take(1000, "property message");
            if (message.kind === "text") {
                await socket.sendText(`text:${message.text}`);
            } else if (message.kind === "json") {
                await socket.sendJson({ json: message.json() });
            } else if (message.kind === "binary") {
                await socket.sendBytes(message.bytes);
            }
            await socket.close(1000, "property complete");
        });
        const host = await TestHost.create(app);
        try {
            await host.websocket(route).origin("https://wrong.example").protocols([protocol]).connect().expectRejected(403);
            await host.websocket(route).origin(origin).protocols(["wrong.protocol"]).connect().expectRejected(400);
            const socket = await host.websocket(route).origin(origin).protocols([protocol]).connect();
            assert.equal(socket.protocol, protocol);
            await socket.expectText("ready");
            const mode = random.pick(["text", "json", "binary"]);
            if (mode === "text") {
                const text = safeSegment(random, "msg");
                await socket.sendText(text);
                await socket.expectText(`text:${text}`);
            } else if (mode === "json") {
                const body = { value: random.int(1000), text: safeSegment(random, "j") };
                await socket.sendJson(body);
                await socket.expectJson({ json: body });
            } else {
                const body = new Uint8Array([random.int(256), random.int(256), random.int(256)]);
                await socket.sendBytes(body);
                await socket.expectBytes(body);
            }
            await socket.expectClose(1000);
            host.metrics.expectCounter("websocket.upgrades.rejected.total", 1, { outcome: "origin" });
            host.metrics.expectCounter("websocket.upgrades.rejected.total", 1, { outcome: "protocol" });
        } finally {
            await host.close();
        }
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

    schema(random) {
        const maxName = 1 + random.int(16);
        const Model = schema.object({
            name: schema.string().minLength(1).maxLength(maxName),
            count: schema.integer().min(0).max(1000),
            enabled: schema.boolean().default(true),
            tag: schema.string().nullable().optional(),
            values: schema.array(schema.number()),
        });
        const name = "x".repeat(1 + random.int(maxName));
        const payload = {
            name,
            count: random.int(1001),
            values: Array.from({ length: random.int(5) }, () => random.int(1000) / 10),
        };
        const valid = Model.validate(payload);
        assert.equal(valid.ok, true);
        assert.equal(valid.value.enabled, true);
        assert.notEqual(valid.value, payload);
        const invalid = Model.validate({
            name: "",
            count: -1,
            values: [1, "bad"],
        });
        assert.equal(invalid.ok, false);
        assert(invalid.issues.some((issue) => issue.path.join(".") === "name"));
        assert(invalid.issues.some((issue) => issue.path.join(".") === "count"));
        assert(invalid.issues.some((issue) => issue.path.join(".") === "values.1"));
        assertThrows(() => schema.string().minLength(-1), /non-negative/);
        assertThrows(() => schema.object({ bad: "not-a-schema" }), /schema/);
        assertThrows(() => schema.array("not-a-schema"), /schema/);
    },

    async scheduler(random) {
        const settle = async () => {
            for (let index = 0; index < 5; index += 1) {
                await Promise.resolve();
            }
        };
        const intervalMs = 1 + random.int(25);
        const expectedRuns = 2 + random.int(3);
        const clock = Time.fakeClock({ now: new Date("2026-01-01T00:00:00Z") });
        const runs = [];
        const job = Time.every(intervalMs, async (ctx) => {
            runs.push({ run: ctx.run, skippedRuns: ctx.skippedRuns, at: ctx.startedAt.toISOString() });
        }, {
            clock,
            immediate: true,
            maxRuns: expectedRuns,
        });
        await settle();
        for (let index = 1; index < expectedRuns; index += 1) {
            clock.advanceBy(intervalMs);
            await settle();
        }
        await job.stop();
        assert.deepEqual(runs.map((entry) => entry.run), Array.from({ length: expectedRuns }, (_value, index) => index + 1));
        assert.equal(runs.every((entry) => entry.skippedRuns === 0), true);

        const pausedRuns = [];
        const paused = Time.every(intervalMs, async (ctx) => {
            pausedRuns.push(ctx.run);
        }, { clock, maxRuns: 2 });
        paused.pause();
        clock.advanceBy(intervalMs);
        await settle();
        assert.deepEqual(pausedRuns, []);
        paused.resume();
        clock.advanceBy(intervalMs);
        await settle();
        await paused.stop();
        assert.deepEqual(pausedRuns, [1]);
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
