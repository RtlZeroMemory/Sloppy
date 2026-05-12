import { Jobs, data } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
    const value = Environment.get(name);
    if (value === undefined || value === "") {
        throw new Error(`Missing required environment value: ${name}`);
    }
    return value;
}

const db = data.postgres.open({
    connectionString: requireEnvironment("SLOPPY_JOBS_POSTGRES_URL"),
    access: "readwrite",
});

const jobs = Jobs.create({ storage: Jobs.storage.postgres(db) });
await jobs.storage.init();

jobs.define("reindex-search", {
    queue: "maintenance",
    retries: { maxAttempts: 4, backoff: "exponential", initialDelayMs: 5000, maxDelayMs: 120000 },
    timeoutMs: 300000,
}, async (ctx, input) => {
    await ctx.extendLease(300000);
    await db.exec("insert into search_reindex_log(job_name, requested_by) values ($1, $2)", [
        ctx.name,
        input.requestedBy ?? "system",
    ]);
});

const worker = jobs.createWorker({
    id: "maintenance-worker",
    queues: ["maintenance"],
    concurrency: 2,
});

await worker.start();

export default jobs;
