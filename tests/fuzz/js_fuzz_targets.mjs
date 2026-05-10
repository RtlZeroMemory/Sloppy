import assert from "node:assert/strict";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import {
    Deadline,
    HttpClient,
    Results,
    Sloppy,
    Testing,
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
    "worker-queue",
]);

const SECRET_MARKER = "SECRET_JS_FUZZ_SHOULD_NOT_APPEAR";

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
            return ((value ^ (value >>> 14)) >>> 0);
        },
        int(maxExclusive) {
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

function bytesFromPrng(random, maxLength = 128) {
    const bytes = new Uint8Array(random.int(maxLength + 1));
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = random.nextU32() & 0xff;
    }
    return bytes;
}

function textFromPrng(random, maxLength = 48) {
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

function safeRoutePattern(random) {
    const roots = ["/", "/items", "/items/{id:int}", "/users/{name}", "/health", "/reports/{slug}"];
    return random.pick(roots);
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
    if (descriptor !== undefined) {
        assert.equal(descriptor.__sloppyResult, true);
        assert.equal(Object.isFrozen(descriptor), true);
        assert.equal(typeof descriptor.status, "number");
    }
}

const targets = Object.freeze({
    "config-json"(random) {
        const builder = Sloppy.createBuilder();
        const configObject = {
            App: {
                Name: textFromPrng(random, 20) || "app",
                Port: String(1024 + random.int(50000)),
                Enabled: random.bool() ? "true" : "false",
                Tags: [textFromPrng(random, 8)],
                Metadata: { sample: jsonValue(random, 1) },
            },
        };
        maybeThrows(() => builder.config.addObject(configObject));
        maybeThrows(() => builder.config.getString("App:Name", "fallback"));
        maybeThrows(() => builder.config.getInt("App:Port", 3000));
        maybeThrows(() => builder.config.getBool("App:Enabled", false));
        maybeThrows(() => builder.config.getArray("App:Tags", []));
        maybeThrows(() => builder.config.bind("App", {
            name: { type: "string", default: "fallback" },
            port: { type: "integer", default: 3000, min: 1 },
            enabled: { type: "boolean", default: false },
        }));
    },

    "openapi-plan"(random) {
        const app = Sloppy.create();
        const count = 1 + random.int(5);
        for (let index = 0; index < count; index += 1) {
            maybeThrows(() => {
                app.mapGet(safeRoutePattern(random), () => Results.ok({ index, value: textFromPrng(random, 8) }))
                    .withName(`Route.${index}`);
            });
        }
        const routes = app.__getRoutes();
        assert(Array.isArray(routes));
        JSON.stringify({
            routes: routes.map((route) => ({
                method: route.method,
                pattern: route.pattern,
                name: route.name,
                metadata: route.metadata,
            })),
            plan: app.__getPlanContributions(),
        });
    },

    headers(random) {
        const headers = {};
        const names = ["X-Test", "Cache-Control", "ETag", "Content-Language", `X-${textFromPrng(random, 8)}`];
        for (let index = 0; index < random.int(4); index += 1) {
            headers[random.pick(names)] = textFromPrng(random, 24);
        }
        const result = maybeThrows(() => Results.text(textFromPrng(random, 32), { headers }));
        if (result.ok) {
            assertResultDescriptor(result.value);
        }
    },

    async "query-string"(random) {
        const app = Sloppy.create();
        app.mapGet("/items/{id:int}", ({ query, route }) => Results.ok({ id: route.id ?? "0", query }));
        const host = Testing.createHost(app);
        try {
            const target = `/items/${1 + random.int(999)}?q=${encodeURIComponent(textFromPrng(random, 12))}&page=${random.int(20)}`;
            const response = await host.get(target);
            assert.equal(response.status, 200);
            const value = await response.json();
            assert.equal(typeof value.query, "object");
        } finally {
            await host.close();
        }
    },

    async "percent-decoding"(random) {
        const app = Sloppy.create();
        app.mapGet("/echo/{slug}", ({ route, query }) => Results.ok({ route, query }));
        const host = Testing.createHost(app);
        const encoded = random.pick(["abc", "%7Bok%7D", "%zz", "%E0%A4%A", "hello+world"]);
        try {
            await maybeRejects(() => host.get(`/echo/${encoded}?value=${encoded}`));
        } finally {
            await host.close();
        }
    },

    "logging-json"(random) {
        const builder = Sloppy.createBuilder();
        const sink = builder.logging.addMemorySink();
        builder.logging.addRedactionKey("customSecret");
        builder.logging.setMinimumLevel("debug");
        const app = builder.build();
        maybeThrows(() => app.log.info(textFromPrng(random, 20) || "event", {
            requestId: String(random.int(9999)),
            token: SECRET_MARKER,
            customSecret: SECRET_MARKER,
            ok: random.bool(),
        }));
        const serialized = JSON.stringify(sink.entries());
        assert.equal(serialized.includes(SECRET_MARKER), false);
    },

    async "package-manifest"(random) {
        const manifestPath = random.pick([
            "packages/npm/sloppy/package.json",
            "packages/npm/sloppy-win32-x64/package.json",
            "stdlib/sloppy/bootstrap.manifest.json",
        ]);
        const text = await readFile(manifestPath, "utf8");
        const parsed = JSON.parse(text);
        assert.equal(typeof parsed, "object");
        JSON.stringify({ manifestPath, sample: jsonValue(random, 1), parsed });
    },

    async "route-table"(random) {
        const app = Sloppy.create();
        app.mapGet("/items", () => Results.ok([]));
        app.mapPost("/items", ({ request }) => Results.created("/items/1", { length: request.contentLength ?? 0 }));
        app.mapGet("/items/{id:int}", ({ route }) => Results.ok({ id: route.id ?? "0" }));
        const host = Testing.createHost(app);
        try {
            const response = await host.request(random.pick(["GET", "POST", "DELETE"]), random.pick(["/items", "/items/1", "/missing"]), {
                json: { value: textFromPrng(random, 12) },
            });
            assert(Number.isInteger(response.status));
        } finally {
            await host.close();
        }
    },

    async "required-features"(random) {
        await maybeRejects(() => HttpClient.get(`http://127.0.0.1:${1 + random.int(65534)}/`));
        maybeThrows(() => Deadline.after(random.pick([0, 1, 10, Number.NaN, -1])));
    },

    async "http-client-options"(random) {
        const options = {
            method: random.pick(["GET", "POST", "PUT", ""]),
            headers: random.bool() ? { "X-Test": textFromPrng(random, 16) } : { "Bad\nName": "x" },
            timeoutMs: random.pick([1, 10, -1, "soon"]),
            maxResponseBytes: random.pick([1, "1kb", "4 parsecs"]),
        };
        await maybeRejects(() => HttpClient.request(`http://127.0.0.1:${1 + random.int(65534)}/`, options));
    },

    "results-headers"(random) {
        assertResultDescriptor(Results.ok(jsonValue(random, 1)));
        assertResultDescriptor(Results.noContent());
        maybeThrows(() => Results.created(textFromPrng(random, 24), jsonValue(random, 1), {
            headers: { "X-Trace": textFromPrng(random, 16) },
        }));
        maybeThrows(() => Results.status(random.pick([100, 200, 299, 600, -1]), jsonValue(random, 1)));
    },

    async "worker-queue"(random) {
        const queue = WorkQueue.create(`q${random.int(99999)}`, {
            maxQueued: 1 + random.int(4),
            concurrency: 1 + random.int(2),
            overflow: random.pick(["reject", "backpressure"]),
            retry: { maxAttempts: 1 + random.int(3), backoffMs: 0 },
        });
        queue.process(async (job) => ({ seen: job.data }));
        const jobs = [];
        const count = 1 + random.int(3);
        for (let index = 0; index < count; index += 1) {
            jobs.push(maybeRejects(() => queue.enqueue({ index, payload: textFromPrng(random, 12) })));
        }
        await Promise.all(jobs);
        await queue.stop();
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
                const artifact = await writeFailureArtifact(options.failureRoot, name, {
                    target: name,
                    seed: options.seed,
                    iteration,
                    iterationSeed,
                    repro: options.reproCommand,
                    error: {
                        name: error?.name,
                        message: error?.message,
                        stack: error?.stack,
                    },
                });
                error.message = `${error.message} (failure artifact: ${artifact})`;
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

const invokedPath = process.argv[1] === undefined ? "" : fileURLToPath(pathToFileURL(process.argv[1]));
if (fileURLToPath(import.meta.url) === invokedPath) {
    main().catch((error) => {
        console.error(error?.stack ?? String(error));
        process.exitCode = 1;
    });
}
