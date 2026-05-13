import { Cache, Redis, Results, Sloppy } from "sloppy";

function createCache(url = "redis://127.0.0.1:6379/0") {
    const redis = Redis.client("main", { url });
    return Cache.redis(redis, {
        name: "default",
        prefix: "example:",
        ttlMs: 60_000,
    });
}

async function loadSettings(cache) {
    return await cache.getOrCreate("settings", async () => ({
        theme: "dark",
        updatedAt: "2026-05-13T00:00:00.000Z",
    }), {
        ttlMs: 30_000,
        tags: ["settings"],
    });
}

function createApp(cache) {
    const app = Sloppy.create();
    app.services.addCache(cache);
    app.get("/settings", async (ctx) => {
        const provider = ctx.services.get("cache.default");
        return Results.json(await loadSettings(provider));
    });
    app.get("/health/redis-cache", async (ctx) => {
        const provider = ctx.services.get("cache.default");
        return Results.json(await provider.health());
    });
    return app;
}

export { createApp, createCache, loadSettings };
