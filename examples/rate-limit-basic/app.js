import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.post("/login", () => Results.ok({ ok: true }))
  .rateLimit(RateLimit.slidingWindow({
    name: "login",
    limit: 5,
    windowMs: 60_000,
    partitionBy: RateLimit.partition.ip(),
  }));

export default app;
