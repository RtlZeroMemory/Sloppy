import { Jobs, data } from "sloppy";
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
    const value = Environment.get(name);
    if (value === undefined || value === "") {
        throw new Error(`Missing required environment value: ${name}`);
    }
    return value;
}

export async function main() {
    const db = data.postgres.open({
        connectionString: requireEnvironment("SLOPPY_JOBS_POSTGRES_URL"),
        access: "readwrite",
    });
    await db.exec`
        create table if not exists sloppy_jobs_live_log (
            id bigserial primary key,
            job_name text not null,
            requested_by text not null
        )
    `;

    const jobs = Jobs.create({ storage: Jobs.storage.postgres(db) });
    await jobs.storage.init();

    jobs.define("reindex-search", {
        queue: "maintenance",
        retries: { maxAttempts: 2, backoff: "fixed", initialDelayMs: 1 },
        timeoutMs: 30000,
    }, async (ctx, input) => {
        await db.exec`
            insert into sloppy_jobs_live_log(job_name, requested_by)
            values (${ctx.name}, ${input.requestedBy ?? "system"})
        `;
    });

    await jobs.enqueue("reindex-search", { requestedBy: "live-postgres" }, {
        queue: "maintenance",
        idempotencyKey: "live-postgres:reindex",
    });

    const worker = jobs.createWorker({
        id: "postgres-live-worker",
        queues: ["maintenance"],
        concurrency: 1,
    });
    await worker.runOnce();

    const overview = await jobs.admin().overview();
    console.log(JSON.stringify({ succeeded: overview.jobs.succeeded, workers: overview.workers }));
    return overview.jobs.succeeded >= 1 ? 0 : 1;
}
