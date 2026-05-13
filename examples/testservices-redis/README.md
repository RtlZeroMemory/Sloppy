# TestServices Redis

This experimental example shows the recommended shape for a Redis-backed
integration test with `TestServices` and `TestHost`.

Requirements:

- Docker CLI on `PATH`
- reachable Docker daemon
- Sloppy outbound network bridge
- opt-in test gate, for example `SLOPPY_TESTSERVICES=1`

The test harness or CI job should perform skip/exit behavior when the opt-in
gate, Docker probe, or network bridge is unavailable. Keep the application
snippet runtime neutral.

```ts
import { Cache, Results, Sloppy, TestHost, TestServices } from "sloppy";

const docker = await TestServices.docker.available();
if (!docker.ok) {
    throw new Error(`Docker unavailable for TestServices: ${docker.reason}`);
}

await using redis = await TestServices.redis();
await using cache = Cache.redis(redis.client("cache"), {
    name: "default",
    ttlMs: 60_000,
});

const app = Sloppy.create();
app.services.addCache(cache);
app.get("/settings", async (ctx) => {
    const provider = ctx.services.get("cache.default");
    const value = await provider.getOrCreate("settings", async () => ({
        enabled: true,
    }));
    return Results.json(value);
});

await using host = await TestHost.create(app, {
    config: redis.env(),
});

await host.get("/settings").expectStatus(200);
await redis.reset();
```

For artifact/package mode, pass environment instead of app-host services:

```ts
await using host = await TestHost.fromPackage("./dist/app", {
    mode: "loopback",
    env: redis.env(),
});
```

Cleanup is automatic with `await using`; without it, call `host.dispose()` and
then `redis.dispose()`.
