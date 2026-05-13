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
    try {
        const result = await operation();
        await lease.extend(30_000);
        return result;
    } finally {
        await lease.dispose();
    }
}

export { createLocks, runExclusive };
