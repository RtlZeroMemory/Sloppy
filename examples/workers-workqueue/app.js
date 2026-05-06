import { Sloppy, Results } from "sloppy";
import { WorkQueue } from "sloppy/workers";

const app = Sloppy.create();

const emails = WorkQueue.create("emails", {
  maxQueued: 1000,
  concurrency: 4,
  overflow: "reject",
  retry: { maxAttempts: 3, backoffMs: 0 },
});

emails.process(async (job, ctx) => {
  ctx.signal.throwIfCancelled?.();
  return { accepted: true, template: job.data.template };
});

app.use(emails);
app.mapPost("/emails/welcome", async () => {
  await emails.enqueue({ to: "user@example.com", template: "welcome" });
  return Results.accepted({ queued: true });
});

export default app;
