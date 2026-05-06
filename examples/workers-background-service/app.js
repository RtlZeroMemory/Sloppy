import { Sloppy, Results } from "sloppy";
import { Time } from "sloppy/time";
import { BackgroundService } from "sloppy/workers";

const app = Sloppy.create();

const cleanup = BackgroundService.create("cleanup", async (ctx) => {
  while (!ctx.signal.cancelled) {
    await Time.delay(5 * 60 * 1000, { signal: ctx.signal });
  }
});

app.use(cleanup);
app.mapGet("/", () => Results.text("worker service registered"));

export default app;
