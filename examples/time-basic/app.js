import { Time } from "sloppy/time";

async function loadUserSummary({ signal } = {}) {
    if (signal?.aborted) {
        throw signal.reason;
    }
    await Time.yield({ signal });
    return { count: 1, source: "cache" };
}

await Time.delay(250);

const summary = await Time.timeout((signal) => loadUserSummary({ signal }), { afterMs: 1000 });

export default summary;
