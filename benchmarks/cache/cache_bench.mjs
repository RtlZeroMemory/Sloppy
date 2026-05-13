import { Cache, Results, Sloppy, TestHost } from "../../stdlib/sloppy/index.js";

const requestedIterations = Number.parseInt(process.env.SLOPPY_CACHE_BENCH_ITERATIONS ?? "1000", 10);
const iterations = Number.isFinite(requestedIterations) && requestedIterations > 0 ? requestedIterations : 1000;
const warmup = Math.min(100, iterations);

function nowNs() {
    return process.hrtime.bigint();
}

async function measure(name, operation, count = iterations) {
    for (let index = 0; index < warmup; index += 1) {
        await operation(index);
    }
    const started = nowNs();
    for (let index = 0; index < count; index += 1) {
        await operation(index);
    }
    const elapsedNs = Number(nowNs() - started);
    return {
        name,
        iterations: count,
        elapsedNs,
        nsPerOp: elapsedNs / Math.max(1, count),
    };
}

const memory = Cache.memory("bench", { maxEntries: 10000, ttlMs: 60000 });
await memory.set("hit", { ok: true });

const rows = [];
rows.push(await measure("cache.memory.get.hit", () => memory.get("hit")));
rows.push(await measure("cache.memory.get.miss", (index) => memory.get(`miss:${index}`)));
rows.push(await measure("cache.memory.set", (index) => memory.set(`set:${index}`, { index })));
rows.push(await measure("cache.memory.get_or_create.hit", () =>
    memory.getOrCreate("hit", { ttlMs: 60000 }, () => ({ ok: false }))));

let factories = 0;
rows.push(await measure("cache.memory.get_or_create.concurrent_miss", async (index) => {
    await Promise.all(Array.from({ length: 8 }, () =>
        memory.getOrCreate(`coalesce:${index}`, { ttlMs: 60000 }, async () => {
            factories += 1;
            return { index };
        })));
}, Math.max(1, Math.trunc(iterations / 8))));

const app = Sloppy.create();
app.services.addCache(Cache.memory("default", { maxEntries: 10000, ttlMs: 60000 }));
app.get("/cached", () => Results.json({ ok: true })).outputCache({ ttlMs: 60000 });
const host = await TestHost.create(app);
await host.get("/cached");
rows.push(await measure("output_cache.hit", () => host.get("/cached")));
await host.close();

console.log(JSON.stringify({
    benchmark: "sloppy.cache",
    iterations,
    coalescedFactoryRuns: factories,
    rows,
}, null, 2));
