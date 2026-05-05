import { Time } from "sloppy/time";

const ticks = [];

for await (const tick of Time.interval(1000, { immediate: true, maxTicks: 3 })) {
    ticks.push(tick.index);
}

const job = Time.every(
    "5m",
    async (ctx) => {
        await cleanup({ signal: ctx.signal });
    },
    {
        noOverlap: true,
        missedRunPolicy: "skip",
    },
);

const scheduledJob = Time.every("5m", async (ctx) => cleanup({ signal: ctx.signal }), {
    noOverlap: true,
    missedRunPolicy: "skip",
    maxRuns: 1,
});

await job.stop();
await scheduledJob.stop();

async function cleanup({ signal }) {
    if (signal.aborted) {
        return "cancelled";
    }
    await Time.yield();
    return "clean";
}

export default { ticks, skippedRuns: job.skippedRuns };
