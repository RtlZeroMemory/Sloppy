import { data } from "sloppy";
import { Environment } from "sloppy/os";
import { Jobs } from "sloppy/jobs";

function requireEnvironment(name) {
    const value = Environment.get(name);
    if (value === undefined || value === "") {
        throw new Error(`Missing required environment value: ${name}`);
    }
    return value;
}

function openProvider(provider, options) {
    if (provider === "sqlite") {
        return data.sqlite.open({
            database: options?.database ?? "jobs-concurrency.db",
            capability: "data.sqlite.program",
            access: "readwrite",
        });
    }
    if (provider === "postgres") {
        return data.postgres.open({
            connectionString: requireEnvironment("SLOPPY_JOBS_POSTGRES_URL"),
            capability: "data.postgres.program",
            access: "readwrite",
        });
    }
    if (provider === "sqlserver") {
        return data.sqlserver.open({
            connectionString: requireEnvironment("SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING"),
            capability: "data.sqlserver.program",
            access: "readwrite",
        });
    }
    throw new Error(`Unsupported provider: ${provider}`);
}

function createJobs(provider, options) {
    const db = openProvider(provider, options);
    const jobs = Jobs.create({ storage: Jobs.storage[provider](db) });
    jobs.define("concurrency-job", {
        queue: "concurrency",
        retries: { maxAttempts: 1 },
        timeoutMs: 30000,
    }, async () => {});
    return { db, jobs };
}

export async function main(args) {
    try {
        return await run(args);
    } catch (error) {
        console.log(JSON.stringify({ ok: false, error: String(error?.stack ?? error?.message ?? error) }));
        return 1;
    }
}

async function run(args) {
    const provider = args[0] ?? "sqlite";
    const operation = args[1] ?? "init";
    const sqliteDatabase = provider === "sqlite" ? args[2] : undefined;
    const operationArgs = provider === "sqlite" ? args.slice(3) : args.slice(2);
    const { db, jobs } = createJobs(provider, { database: sqliteDatabase });
    await jobs.storage.init();

    if (operation === "init") {
        console.log(JSON.stringify({ ok: true, operation, provider }));
        db.close?.();
        return 0;
    }

    if (operation === "enqueue-duplicate") {
        const key = operationArgs[0] ?? `${provider}:duplicate`;
        const queue = operationArgs[1] ?? "concurrency";
        const job = await jobs.enqueue("concurrency-job", { duplicate: true }, {
            queue,
            idempotencyKey: key,
        });
        console.log(JSON.stringify({ ok: true, operation, provider, queue, jobId: job.id }));
        db.close?.();
        return 0;
    }

    if (operation === "acquire-lock") {
        const name = operationArgs[0] ?? `${provider}:lock`;
        const owner = operationArgs[1] ?? "owner";
        const ttlMs = Number.isInteger(Number(operationArgs[2])) ? Number(operationArgs[2]) : 1000;
        const acquired = await jobs.locks(owner).acquire(name, { ttlMs });
        console.log(JSON.stringify({ ok: true, operation, provider, owner, acquired }));
        db.close?.();
        return 0;
    }

    if (operation === "release-lock") {
        const name = operationArgs[0] ?? `${provider}:lock`;
        const owner = operationArgs[1] ?? "owner";
        try {
            const released = await jobs.locks(owner).release(name);
            console.log(JSON.stringify({ ok: true, operation, provider, owner, released }));
            db.close?.();
            return 0;
        } catch (error) {
            console.log(JSON.stringify({
                ok: false,
                operation,
                provider,
                owner,
                code: String(error?.code ?? ""),
                message: String(error?.message ?? error),
            }));
            db.close?.();
            return 2;
        }
    }

    if (operation === "enqueue-claims") {
        const start = Number.isInteger(Number(operationArgs[0])) ? Number(operationArgs[0]) : 0;
        const count = Number.isInteger(Number(operationArgs[1])) ? Number(operationArgs[1]) : 1;
        for (let index = 0; index < count; index += 1) {
            const jobIndex = start + index;
            await jobs.enqueue("concurrency-job", { index: jobIndex }, {
                queue: "concurrency",
                idempotencyKey: `${provider}:claim:${jobIndex}`,
            });
        }
        console.log(JSON.stringify({ ok: true, operation, provider, start, count }));
        db.close?.();
        return 0;
    }

    if (operation === "claim") {
        const workerId = operationArgs[0] ?? `${provider}-worker`;
        const limit = Number.isInteger(Number(operationArgs[1])) ? Number(operationArgs[1]) : 1;
        const leaseMs = Number.isInteger(Number(operationArgs[2])) ? Number(operationArgs[2]) : 5000;
        const queue = operationArgs[3] ?? "concurrency";
        const claimed = await jobs.storage.claim(
            { id: workerId, queues: [queue], concurrency: limit },
            { leaseMs },
        );
        console.log(JSON.stringify({
            ok: true,
            operation,
            provider,
            workerId,
            queue,
            jobIds: claimed.map((job) => job.id),
        }));
        db.close?.();
        return 0;
    }

    throw new Error(`Unsupported operation: ${operation}`);
}
