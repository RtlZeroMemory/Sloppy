import { Jobs, data } from "sloppy";

function positiveInteger(text, fallback) {
    const value = Number(text ?? fallback);
    return Number.isInteger(value) && value > 0 ? value : fallback;
}

export async function main(args) {
    const database = args[0] ?? "jobs-stress.db";
    const jobCount = positiveInteger(args[1], 1000);
    const db = data.sqlite.open({
        database,
        capability: "data.jobs",
        access: "readwrite",
    });
    const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
    await jobs.storage.init();

    let handled = 0;
    jobs.define("stress-job", {
        queue: "stress",
        retries: { maxAttempts: 1 },
        timeoutMs: 30000,
    }, async () => {
        handled += 1;
    });

    const enqueueStart = Date.now();
    for (let index = 0; index < jobCount; index += 1) {
        await jobs.enqueue("stress-job", { index }, {
            queue: "stress",
            idempotencyKey: `stress:${index}`,
        });
    }
    const enqueueMs = Math.max(1, Date.now() - enqueueStart);

    const worker = jobs.createWorker({
        id: "jobs-stress-worker",
        queues: ["stress"],
        concurrency: 16,
    });

    const drainStart = Date.now();
    while (handled < jobCount) {
        const claimed = await worker.runOnce();
        if (claimed === 0) {
            break;
        }
    }
    const drainMs = Math.max(1, Date.now() - drainStart);
    const overview = await jobs.admin().overview();

    console.log(JSON.stringify({
        provider: "sqlite",
        jobs: jobCount,
        succeeded: overview.jobs.succeeded,
        enqueueOpsPerSec: Math.round((jobCount * 1000) / enqueueMs),
        drainOpsPerSec: Math.round((handled * 1000) / drainMs),
        enqueueMs,
        drainMs,
    }));

    return overview.jobs.succeeded >= jobCount ? 0 : 1;
}
