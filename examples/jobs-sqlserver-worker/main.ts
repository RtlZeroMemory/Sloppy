import { data } from "sloppy";
import { Jobs } from "sloppy/jobs";
import { Environment } from "sloppy/os";

function requireEnvironment(name) {
    const value = Environment.get(name);
    if (value === undefined || value === "") {
        throw new Error(`Missing required environment value: ${name}`);
    }
    return value;
}

export async function main() {
    const db = data.sqlserver.open({
        connectionString: requireEnvironment("SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING"),
        capability: "data.sqlserver.program",
        access: "readwrite",
    });
    await db.exec(`
        if object_id(N'dbo.sloppy_jobs_live_log', N'U') is null
        create table dbo.sloppy_jobs_live_log (
            id bigint identity(1,1) primary key,
            job_name nvarchar(256) not null,
            requested_by nvarchar(256) not null
        )
    `);

    const jobs = Jobs.create({ storage: Jobs.storage.sqlserver(db) });
    await jobs.storage.init();

    jobs.define("reindex-search", {
        queue: "maintenance",
        retries: { maxAttempts: 2, backoff: "fixed", initialDelayMs: 1 },
        timeoutMs: 30000,
    }, async (ctx, input) => {
        await db.exec(
            `
            insert into dbo.sloppy_jobs_live_log(job_name, requested_by)
            values (?, ?)
        `,
            [ctx.name, input.requestedBy ?? "system"],
        );
    });

    await jobs.enqueue("reindex-search", { requestedBy: "live-sqlserver" }, {
        queue: "maintenance",
        idempotencyKey: "live-sqlserver:reindex",
    });

    const worker = jobs.createWorker({
        id: "sqlserver-live-worker",
        queues: ["maintenance"],
        concurrency: 1,
    });
    await worker.runOnce();

    const overview = await jobs.admin().overview();
    console.log(JSON.stringify({ succeeded: overview.jobs.succeeded, workers: overview.workers }));
    return overview.jobs.succeeded >= 1 ? 0 : 1;
}
