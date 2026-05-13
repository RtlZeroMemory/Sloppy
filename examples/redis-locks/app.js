import { Redis } from "sloppy";

function createLocks(url = "redis://127.0.0.1:6379/0") {
    const redis = Redis.client("locks", { url });
    return Redis.locks(redis, {
        prefix: "example:locks:",
    });
}

async function runExclusive(locks, name, operation) {
    const lease = await locks.acquire(name, {
        ttlMs: 30_000,
        waitTimeoutMs: 5000,
        retryDelayMs: 50,
    });
    let finished = false;
    let timer;
    let resolveExtender;
    const extender = new Promise((resolve) => {
        resolveExtender = resolve;
        const tick = async () => {
            if (finished) {
                resolve();
                return;
            }
            try {
                await lease.extend(30_000);
            } finally {
                if (finished) {
                    resolve();
                } else {
                    timer = setTimeout(tick, 15_000);
                }
            }
        };
        timer = setTimeout(tick, 15_000);
    });
    try {
        return await operation();
    } finally {
        finished = true;
        clearTimeout(timer);
        resolveExtender();
        await extender.catch(() => {});
        await lease.dispose();
    }
}

export { createLocks, runExclusive };
