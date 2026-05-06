import { Sloppy, Results } from "sloppy";
import { WorkQueue } from "sloppy/workers";

const app = Sloppy.create();

const queue = WorkQueue.create("shutdown-demo", {
  maxQueued: 8,
  concurrency: 1,
  overflow: "reject",
});

queue.process(async (job, ctx) => {
  if (ctx.signal.cancelled) {
    return { skipped: true };
  }
  return { handled: job.data.id };
});

app.use(queue);
app.mapPost("/jobs", async () => {
  await queue.enqueue({ id: 1 });
  await queue.stop({ drain: true });
  return Results.accepted({ drained: true });
});

export default app;
