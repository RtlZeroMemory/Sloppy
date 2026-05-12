import { Jobs, Schema, data } from "sloppy";

export async function main(args) {
    const database = args[0] ?? "jobs-recurring.db";
    const db = data.sqlite.open({
        database,
        capability: "data.jobs",
        access: "readwrite",
    });
    const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
    await jobs.storage.init();

    jobs.define("sync-users", {
        input: Schema.object({ tenantId: Schema.string().min(1) }),
        queue: "sync",
        retries: { maxAttempts: 3, backoff: "exponential", initialDelayMs: 1, maxDelayMs: 10 },
        timeoutMs: 30000,
    }, async (_ctx, input) => {
        return { synced: input.tenantId };
    });

    await jobs.recurring("sync-users-every-minute", "sync-users", {
        tenantId: "main",
    }, {
        cron: "* * * * *",
        timezone: "UTC",
        queue: "sync",
        misfirePolicy: "run-once",
    });

    const recurring = await jobs.admin().listRecurring();
    console.log(JSON.stringify({
        recurring: recurring.length,
        nextRunAt: recurring[0]?.nextRunAt ?? null,
    }));
    return recurring.length === 1 ? 0 : 1;
}
