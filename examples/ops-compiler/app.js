import { Health, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.health()
    .check("self", Health.self(), { tags: ["live", "ready", "startup"] })
    .check("runtime", Health.runtime(), { tags: ["ready", "startup"], critical: true })
    .check("memory", Health.memory(), { tags: ["health"], critical: false })
    .expose({
        health: "/health",
        live: "/live",
        ready: "/ready",
        startup: "/startup",
    });

app.management({
    path: "/_sloppy",
    health: true,
    metrics: true,
    info: true,
    runtime: true,
});

app.get("/orders/{id}", () => Results.ok({ ok: true }));

export default app;
