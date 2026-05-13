import { Schema, data } from "sloppy";
import { Jobs } from "sloppy/jobs";
import { Environment } from "sloppy/os";

const db = data.sqlite.open({
    database: Environment.get("SLOPPY_JOBS_SQLITE_PATH") ?? "jobs-recurring.db",
    capability: "data.jobs",
    access: "readwrite",
});

const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
await jobs.storage.init();

jobs.define("sync-users", {
    input: Schema.object({ tenantId: Schema.string().min(1) }),
    queue: "sync",
    retries: { maxAttempts: 5, backoff: "exponential", initialDelayMs: 1000, maxDelayMs: 60000 },
    timeoutMs: 120000,
}, async (_ctx, input) => {
    return { synced: input.tenantId };
});

await jobs.recurring("sync-users-every-five", "sync-users", {
    tenantId: "main",
}, {
    cron: "*/5 * * * *",
    timezone: "UTC",
    queue: "sync",
    misfirePolicy: "run-once",
});

await jobs.tickRecurring({ owner: "scheduler-example", ttlMs: 30000 });

export default jobs.admin();
