import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

const redisStore = RateLimit.redis(undefined, {
  prefix: "sloppy:rl:",
});

app.services.addRateLimitStore("redis", redisStore);

app.post("/login", () => Results.ok({ ok: true }))
  .rateLimit(RateLimit.slidingWindow({
    name: "login",
    limit: 5,
    windowMs: 60_000,
    store: "redis",
    partitionBy: RateLimit.partition.ip(),
  }));

export default app;
