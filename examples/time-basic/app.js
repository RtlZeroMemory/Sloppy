import { Time } from "sloppy/time";

async function loadUserSummary() {
    await Time.yield();
    return { count: 1, source: "cache" };
}

await Time.delay(250);

const summary = await Time.timeout(loadUserSummary(), {
    afterMs: 1000,
});

export default summary;
