import { RateLimit } from "../stdlib/sloppy/rate-limit.js";

const attempts = Number.parseInt(process.env.SLOPPY_RATE_LIMIT_BENCH_ATTEMPTS ?? "1000", 10);
const store = RateLimit.memory({ maxKeys: attempts + 10 });
const policy = RateLimit.fixedWindow({
  name: "bench",
  limit: attempts + 1,
  windowMs: 60_000,
  partitionBy: RateLimit.partition.custom((ctx) => ctx.id),
});

const start = performance.now();
for (let index = 0; index < attempts; index += 1) {
  await store.check({
    policy,
    cost: 1,
    partitionHash: String(index),
    nowMs: index,
  });
}
const elapsedMs = performance.now() - start;

console.log(JSON.stringify({
  benchmark: "rate-limit-memory-fixed-window-smoke",
  attempts,
  elapsedMs,
  stats: store.stats(),
}, null, 2));
