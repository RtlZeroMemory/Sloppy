import { Jobs, Schema, data } from "sloppy";

export async function main(args) {
    const database = args[0] ?? "jobs-basic.db";
    const db = data.sqlite.open({
        database,
        capability: "data.jobs",
        access: "readwrite",
    });
    const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
    await jobs.storage.init();

    const EmailPayload = Schema.object({
        to: Schema.string().email(),
        token: Schema.string().min(1),
    });
    const delivered = [];

    jobs.define("send-email", {
        input: EmailPayload,
        queue: "emails",
        retries: { maxAttempts: 2, backoff: "fixed", initialDelayMs: 1 },
        timeoutMs: 30000,
        payloadRedactionKeys: ["token"],
    }, async (ctx, input) => {
        await ctx.extendLease();
        delivered.push(input.to);
    });

    await jobs.enqueue("send-email", {
        to: "ada@example.com",
        token: "secret",
    }, {
        idempotencyKey: "welcome:ada",
        queue: "emails",
    });

    const worker = jobs.createWorker({
        id: "jobs-basic-program",
        queues: ["emails"],
        concurrency: 1,
    });
    await worker.runOnce();

    const overview = await jobs.admin().overview();
    console.log(JSON.stringify({
        delivered,
        succeeded: overview.jobs.succeeded,
        workers: overview.workers,
    }));
    return overview.jobs.succeeded >= 1 ? 0 : 1;
}
