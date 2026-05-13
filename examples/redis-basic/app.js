import { Redis, schema } from "sloppy";

const userSchema = schema.object({
    id: schema.number(),
    email: schema.string().email(),
});

function createRedis(url = "redis://127.0.0.1:6379/0") {
    return Redis.client("main", {
        url,
        commandTimeoutMs: 1000,
        pool: {
            maxConnections: 4,
            pendingQueueLimit: 16,
            acquireTimeoutMs: 1000,
        },
    });
}

async function writeUser(redis, user) {
    await redis.set(`users:${user.id}`, user, { ttlMs: 60_000 });
    return await redis.get(`users:${user.id}`, userSchema);
}

async function writeStatus(redis) {
    await redis.setText("status", "ready", { ttlMs: 5000 });
    return await redis.getText("status");
}

async function writeBytes(redis, bytes) {
    await redis.setBytes("payload", bytes, { ttlMs: 5000 });
    return await redis.getBytes("payload");
}

async function deleteWithScript(redis, key) {
    return await redis.script("return redis.call('del', KEYS[1])", [key]);
}

export { createRedis, deleteWithScript, writeBytes, writeStatus, writeUser };
