import { performance } from "node:perf_hooks";
import { Cache } from "../../stdlib/sloppy/cache.js";
import { Redis, RespParser, encodeCommand } from "../../stdlib/sloppy/redis.js";

const DEFAULT_ITERATIONS = 5000;

function normalizeIterations(value) {
    const parsed = Number(value ?? DEFAULT_ITERATIONS);
    if (!Number.isInteger(parsed) || parsed <= 0) {
        return DEFAULT_ITERATIONS;
    }
    return parsed;
}

function opsPerSecond(iterations, elapsedMs) {
    if (iterations <= 0 || elapsedMs <= 0) {
        return 0;
    }
    return Math.round((iterations / elapsedMs) * 1000);
}

function bench(name, iterations, fn) {
    const started = performance.now();
    for (let index = 0; index < iterations; index += 1) {
        fn(index);
    }
    const elapsedMs = performance.now() - started;
    return { name, iterations, elapsedMs: Number(elapsedMs.toFixed(3)), opsPerSecond: opsPerSecond(iterations, elapsedMs) };
}

function parserBench(iterations) {
    const frame = new TextEncoder().encode("*2\r\n$3\r\nGET\r\n$6\r\nkey:42\r\n");
    return bench("resp.parser.complete-frame", iterations, () => {
        const parser = new RespParser();
        parser.feed(frame);
        const reply = parser.read();
        if (!Array.isArray(reply) || reply.length !== 2) {
            throw new Error("parser benchmark invariant failed");
        }
    });
}

function encoderBench(iterations) {
    return bench("resp.encoder.command", iterations, (index) => {
        const bytes = encodeCommand(["SET", `key:${index}`, `value:${index}`]);
        if (bytes.byteLength === 0) {
            throw new Error("encoder benchmark invariant failed");
        }
    });
}

async function cacheBench(iterations) {
    const commands = [];
    const redis = Object.freeze({
        name: "bench",
        token: Redis.token("bench"),
        async get() {
            return undefined;
        },
        async set(key, value, options) {
            commands.push(["SET", key, value, options]);
            return true;
        },
        async command(name, args) {
            commands.push([name, ...args]);
            return 1;
        },
        async expire() {
            return true;
        },
        async delete() {
            return 1;
        },
        async exists() {
            return false;
        },
        async scan() {
            return { cursor: "0", keys: [] };
        },
        async pipeline(items) {
            commands.push(...items);
            return items.map(() => 1);
        },
        async script(script, keys, args) {
            commands.push(["SCRIPT", keys.length, args.length]);
            return 1;
        },
        async health() {
            return { status: "healthy" };
        },
    });
    const cache = Cache.redis(redis, { name: "bench", ttlMs: 1000 });
    const started = performance.now();
    for (let index = 0; index < iterations; index += 1) {
        await cache.set(`key:${index}`, { index });
    }
    const elapsedMs = performance.now() - started;
    return {
        name: "cache.redis.set-bookkeeping",
        iterations,
        elapsedMs: Number(elapsedMs.toFixed(3)),
        opsPerSecond: opsPerSecond(iterations, elapsedMs),
        commands: commands.length,
    };
}

const iterations = normalizeIterations(process.env.SLOPPY_REDIS_BENCH_ITERATIONS);
const results = [
    encoderBench(iterations),
    parserBench(iterations),
    await cacheBench(Math.min(iterations, 1000)),
];

console.log(JSON.stringify({ status: "local-regression-only", results }, null, 2));
