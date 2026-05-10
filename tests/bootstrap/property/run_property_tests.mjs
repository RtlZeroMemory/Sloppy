import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";

import {
    Base64,
    Base64Url,
    Binary,
    CancellationController,
    Deadline,
    Hex,
    HttpClient,
    ProblemDetails,
    Results,
    Sloppy,
    Text,
    Time,
    WorkQueue,
} from "../../../stdlib/sloppy/index.js";

const DEFAULT_PROPERTY_TARGETS = Object.freeze([
    "codec",
    "results",
    "time",
    "http-client",
    "workers",
    "logging",
    "config",
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
        assert.equal(await Time.timeout(Promise.resolve("fast"), { afterMs: 10, clock: fastClock }), "fast");
        fastClock.dispose();

        const slowClock = Time.fakeClock();
        const slow = Time.timeout(new Promise(() => {}), { afterMs: 5, clock: slowClock });
        slowClock.advanceBy(5);
        await assertRejects(() => slow, /exceeded its deadline|SLOPPY_E_TIME_TIMEOUT/);
        slowClock.dispose();

        const orderedClock = Time.fakeClock({ now: 1000 });
        const order = [];
        const firstDelay = orderedClock.delay(10).then(() => order.push("first"));
        const secondDelay = orderedClock.delay(5).then(() => order.push("second"));
        orderedClock.advanceBy(5);
        await secondDelay;
        orderedClock.advanceBy(5);
        await firstDelay;
        assert.deepEqual(order, ["second", "first"]);
        orderedClock.dispose();
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

        const controller = new CancellationController();
        controller.cancel("before enqueue");
        const cancelled = WorkQueue.create(`cancelled-q-${random.nextU32()}`, { maxQueued: 1, concurrency: 1 });
        cancelled.process(async () => "never");
        await assertRejects(() => cancelled.enqueue({ ok: false }, { signal: controller.signal }), /SLOPPY_E_WORK_JOB_CANCELLED/);
        await cancelled.stop();
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
