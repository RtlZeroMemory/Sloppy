import { WorkQueue } from "sloppy/workers";
import { Deadline } from "sloppy/time";

const queue = WorkQueue.create("core-worker-time", {
    maxQueued: 8,
    concurrency: 2,
    overflow: "backpressure",
});

queue.process(async (job, ctx) => {
    ctx.signal.throwIfCancelled();
    return { id: job.data.id, attempt: job.attempt };
});

export const scheduled = queue.enqueue({ id: "daily-summary" }, {
    deadline: Deadline.after(500),
});
