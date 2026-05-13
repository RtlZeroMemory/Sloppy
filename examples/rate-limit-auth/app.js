import { RateLimit, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.get("/me", (ctx) => Results.ok({ sub: ctx.user.sub }))
  .requiresAuth()
  .rateLimit(RateLimit.tokenBucket({
    name: "me",
    capacity: 100,
    refillPerSecond: 10,
    partitionBy: RateLimit.partition.user(),
  }));

export default app;
