import { data } from "sloppy";
import { Jobs } from "sloppy/jobs";

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function defineJob(jobs) {
    jobs.define("concurrency-job", {
        queue: "concurrency",
        retries: { maxAttempts: 1 },
        timeoutMs: 30000,
    }, async () => {});
    return jobs;
}

export async function main(args) {
    const database = args[0] ?? "jobs-concurrency.db";
    let currentTime = new Date();
    const clock = { now: () => currentTime };
    const createJobs = () => defineJob(Jobs.create({
        storage: Jobs.storage.sqlite(data.sqlite.open({
            database,
            capability: "data.sqlite.program",
            access: "readwrite",
        })),
        clock,
    }));

    const jobs = createJobs();
    await jobs.storage.init();

    const duplicates = [];
    for (let index = 0; index < 16; index += 1) {
        duplicates.push(await createJobs().enqueue("concurrency-job", { duplicate: true }, {
            queue: "concurrency",
            idempotencyKey: "sqlite:duplicate",
        }));
    }
    assert(new Set(duplicates.map((job) => job.id)).size === 1, "duplicate enqueue returned more than one durable job");

    const lockResults = [];
    for (const owner of ["owner-a", "owner-b", "owner-c"]) {
        lockResults.push(await createJobs().locks(owner).acquire("sqlite:single-owner", { ttlMs: 1000 }));
    }
    assert(lockResults.filter(Boolean).length === 1, "lock acquire allowed more than one owner");

    assert(await jobs.locks("owner-old").acquire("sqlite:expired", { ttlMs: 1 }), "expired fixture lock was not acquired");
    currentTime = new Date(currentTime.getTime() + 10);
    assert(await createJobs().locks("owner-expired").acquire("sqlite:expired", { ttlMs: 1000 }), "expired lock was not acquired");
    try {
        await jobs.locks("owner-not-current").release("sqlite:expired");
        throw new Error("non-owner release unexpectedly succeeded");
    } catch (error) {
        assert(String(error?.code ?? error).includes("SLOPPY_E_JOBS_LOCK_CONFLICT"), "non-owner release failed with the wrong error");
    }

    for (let index = 0; index < 20; index += 1) {
        await createJobs().enqueue("concurrency-job", { index }, {
            queue: "concurrency",
            idempotencyKey: `sqlite:claim:${index}`,
        });
    }

    console.log(JSON.stringify({
        provider: "sqlite",
        duplicateJobId: duplicates[0].id,
        lockOwnersAccepted: lockResults.filter(Boolean).length,
        enqueuedClaimCandidates: 20,
    }));
    return 0;
}
