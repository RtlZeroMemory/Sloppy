import { Schema, data } from "sloppy";
import { Jobs } from "sloppy/jobs";
import { Environment } from "sloppy/os";

const db = data.sqlite.open({
    database: Environment.get("SLOPPY_JOBS_SQLITE_PATH") ?? "jobs-basic.db",
    capability: "data.jobs",
    access: "readwrite",
});

const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
await jobs.storage.init();

const EmailPayload = Schema.object({
    to: Schema.string().email(),
    token: Schema.string().min(1),
});

jobs.define("send-email", {
    input: EmailPayload,
    queue: "emails",
    retries: { maxAttempts: 3, backoff: "fixed", initialDelayMs: 1000 },
    timeoutMs: 30000,
    payloadRedactionKeys: ["token"],
}, async (ctx, input) => {
    await ctx.extendLease();
    return { deliveredTo: input.to };
});

await jobs.enqueue("send-email", {
    to: "ada@example.com",
    token: "secret",
}, {
    idempotencyKey: "welcome:ada",
});

const worker = jobs.createWorker({
    id: "jobs-basic-worker",
    queues: ["emails"],
    concurrency: 1,
});

await worker.runOnce();

export default jobs.admin();
