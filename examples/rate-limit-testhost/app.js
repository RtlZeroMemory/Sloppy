import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.get("/limited", () => Results.ok({ ok: true }))
  .rateLimit(RateLimit.fixedWindow({
    name: "test",
    limit: 1,
    windowMs: 1000,
    partitionBy: RateLimit.partition.ip(),
  }));

export default app;
