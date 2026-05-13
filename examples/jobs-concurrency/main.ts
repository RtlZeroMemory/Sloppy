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

function openProvider(provider, args) {
    if (provider === "sqlite") {
        return {
            db: data.sqlite.open({
                database: args[1] ?? "jobs-concurrency.db",
                capability: "data.sqlite.program",
                access: "readwrite",
            }),
            storage(db) {
                return Jobs.storage.sqlite(db);
            },
        };
    }
    if (provider === "postgres") {
        return {
            db: data.postgres.open({
                connectionString: requireEnvironment("SLOPPY_JOBS_POSTGRES_URL"),
                capability: "data.postgres.program",
                access: "readwrite",
            }),
            storage(db) {
                return Jobs.storage.postgres(db);
            },
        };
    }
    if (provider === "sqlserver") {
        return {
            db: data.sqlserver.open({
                connectionString: requireEnvironment("SLOPPY_JOBS_SQLSERVER_CONNECTION_STRING"),
                capability: "data.sqlserver.program",
                access: "readwrite",
            }),
            storage(db) {
                return Jobs.storage.sqlserver(db);
            },
        };
    }
    throw new Error(`Unsupported provider: ${provider}`);
}

function defineConcurrencyJob(jobs) {
    jobs.define("concurrency-job", {
        queue: "concurrency",
        retries: { maxAttempts: 1 },
        timeoutMs: 30000,
    }, async () => {});
    return jobs;
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

async function runAll(provider, operations) {
    if (provider === "sqlite") {
        const results = [];
        for (const operation of operations) {
            results.push(await operation());
        }
        return results;
    }
    return await Promise.all(operations.map((operation) => operation()));
}

export async function main(args) {
    try {
        return await run(args);
    } catch (error) {
        console.log(JSON.stringify({ error: String(error?.stack ?? error?.message ?? error) }));
        return 1;
    }
}

async function run(args) {
    const provider = args[0] ?? "sqlite";
    console.log(JSON.stringify({ stage: "start", provider }));
    let currentTime = new Date();
    const clock = { now: () => currentTime };
    const createJobs = () => {
        const opened = openProvider(provider, args);
        return defineConcurrencyJob(Jobs.create({ storage: opened.storage(opened.db), clock }));
    };
    const jobs = createJobs();
    await jobs.storage.init();

    const duplicates = await runAll(provider, Array.from({ length: 16 }, () => () => {
        const contender = createJobs();
        return contender.enqueue("concurrency-job", { duplicate: true }, {
            queue: "concurrency",
            idempotencyKey: `${provider}:duplicate`,
        });
    }));
    console.log(JSON.stringify({ stage: "duplicates" }));
    assert(new Set(duplicates.map((job) => job.id)).size === 1, "duplicate enqueue returned more than one durable job");

    const lockResults = await runAll(provider, [
        () => createJobs().locks("owner-a").acquire(`${provider}:single-owner`, { ttlMs: 1000 }),
        () => createJobs().locks("owner-b").acquire(`${provider}:single-owner`, { ttlMs: 1000 }),
        () => createJobs().locks("owner-c").acquire(`${provider}:single-owner`, { ttlMs: 1000 }),
    ]);
    console.log(JSON.stringify({ stage: "locks" }));
    assert(lockResults.filter(Boolean).length === 1, "lock acquire allowed more than one owner");
    assert(await jobs.locks("owner-old").acquire(`${provider}:expired`, { ttlMs: 1 }), "expired fixture lock was not acquired");
    currentTime = new Date(currentTime.getTime() + 10);
    assert(await jobs.locks("owner-expired").acquire(`${provider}:expired`, { ttlMs: 1000 }), "expired lock was not acquired");
    try {
        await jobs.locks("owner-not-current").release(`${provider}:expired`);
        throw new Error("non-owner release unexpectedly succeeded");
    } catch (error) {
        assert(String(error?.code ?? error).includes("SLOPPY_E_JOBS_LOCK_CONFLICT"), "non-owner release failed with the wrong error");
    }

    for (let index = 0; index < 20; index += 1) {
        await createJobs().enqueue("concurrency-job", { index }, {
            queue: "concurrency",
            idempotencyKey: `${provider}:claim:${index}`,
        });
    }
    console.log(JSON.stringify({ stage: "enqueued-claims" }));
    const firstClaims = await runAll(provider, [
        () => createJobs().storage.claim({ id: `${provider}-worker-1`, queues: ["concurrency"], concurrency: 8 }, { leaseMs: 5000 }),
        () => createJobs().storage.claim({ id: `${provider}-worker-2`, queues: ["concurrency"], concurrency: 8 }, { leaseMs: 5000 }),
        () => createJobs().storage.claim({ id: `${provider}-worker-3`, queues: ["concurrency"], concurrency: 8 }, { leaseMs: 5000 }),
    ]);
    console.log(JSON.stringify({ stage: "claimed" }));
    const claimedIds = firstClaims.flat().map((job) => job.id);
    assert(claimedIds.length === new Set(claimedIds).size, "workers claimed the same job more than once");

    await jobs.enqueue("concurrency-job", { lease: true }, {
        queue: "leases",
        idempotencyKey: `${provider}:lease-reclaim`,
    });
    const [leased] = await jobs.storage.claim({ id: `${provider}-lease-worker-1`, queues: ["leases"], concurrency: 1 }, { leaseMs: 1 });
    assert(leased !== undefined, "initial lease claim failed");
    currentTime = new Date(currentTime.getTime() + 10);
    const [reclaimed] = await createJobs().storage.claim({ id: `${provider}-lease-worker-2`, queues: ["leases"], concurrency: 1 }, { leaseMs: 5000 });
    assert(reclaimed?.id === leased.id, "expired processing lease was not reclaimed");

    console.log(JSON.stringify({
        provider,
        duplicateJobId: duplicates[0].id,
        lockOwnersAccepted: lockResults.filter(Boolean).length,
        claimedJobs: claimedIds.length,
        uniqueClaimedJobs: new Set(claimedIds).size,
        reclaimedLeaseJobId: reclaimed.id,
    }));
    return 0;
}
