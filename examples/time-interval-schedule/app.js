import { Time } from "sloppy/time";

const ticks = [];
const runs = [];

for await (const tick of Time.interval(1000, { immediate: true, maxTicks: 3 })) {
    ticks.push(tick.index);
}

const job = Time.every(
    "5m",
    async (ctx) => {
        runs.push(ctx.run);
        await cleanup({ signal: ctx.signal });
    },
    {
        immediate: true,
        noOverlap: true,
        missedRunPolicy: "skip",
        maxRuns: 1,
    },
);

const scheduledJob = Time.every("5m", async (ctx) => cleanup({ signal: ctx.signal }), {
    immediate: true,
    noOverlap: true,
    missedRunPolicy: "skip",
    maxRuns: 1,
});

await Time.yield();
await job.stop();
await scheduledJob.stop();

async function cleanup({ signal }) {
    if (signal.aborted) {
        return "cancelled";
    }
    await Time.yield();
    return "clean";
}

export default { ticks, runs, skippedRuns: job.skippedRuns };
