import { data } from "sloppy";
import { Jobs } from "sloppy/jobs";

function positiveInteger(text, fallback) {
    const value = Number(text ?? fallback);
    return Number.isInteger(value) && value > 0 ? value : fallback;
}

export async function main(args) {
    const database = args[0] ?? "jobs-stress.db";
    const jobCount = positiveInteger(args[1], 1000);
    const startIndex = Number.isInteger(Number(args[2])) && Number(args[2]) >= 0 ? Number(args[2]) : 0;
    let handled = 0;
    const createRuntime = () => {
        const db = data.sqlite.open({
            database,
            capability: "data.sqlite.program",
            access: "readwrite",
        });
        const jobs = Jobs.create({ storage: Jobs.storage.sqlite(db) });
        jobs.define("stress-job", {
            queue: "stress",
            retries: { maxAttempts: 1 },
            timeoutMs: 30000,
        }, async () => {
            handled += 1;
        });
        return { db, jobs };
    };

    const { db, jobs } = createRuntime();
    await jobs.storage.init();

    const enqueueStart = Date.now();
    for (let index = 0; index < jobCount; index += 1) {
        const jobIndex = startIndex + index;
        await jobs.enqueue("stress-job", { index: jobIndex }, {
            queue: "stress",
            idempotencyKey: `stress:${jobIndex}`,
        });
    }
    const enqueueMs = Math.max(1, Date.now() - enqueueStart);
    db.close();

    const drainStart = Date.now();
    while (handled < jobCount) {
        const workerRuntime = createRuntime();
        const worker = workerRuntime.jobs.createWorker({
            id: "jobs-stress-worker",
            queues: ["stress"],
            concurrency: 1,
        });
        let batch = 0;
        while (batch < 10 && handled < jobCount) {
            const claimed = await worker.runOnce();
            if (claimed === 0) {
                break;
            }
            batch += claimed;
        }
        if (batch === 0) {
            workerRuntime.db.close();
            break;
        }
        workerRuntime.db.close();
    }
    const drainMs = Math.max(1, Date.now() - drainStart);
    const overviewRuntime = createRuntime();
    const overview = await overviewRuntime.jobs.admin().overview();
    overviewRuntime.db.close();

    console.log(JSON.stringify({
        provider: "sqlite",
        jobs: jobCount,
        startIndex,
        succeeded: overview.jobs.succeeded,
        enqueueOpsPerSec: Math.round((jobCount * 1000) / enqueueMs),
        drainOpsPerSec: Math.round((handled * 1000) / drainMs),
        enqueueMs,
        drainMs,
    }));

    return overview.jobs.succeeded >= startIndex + jobCount ? 0 : 1;
}
