import { Health, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

const ordersCreated = app.metrics.counter("orders_created_total", {
    description: "Orders accepted by the example app.",
});

app.health()
    .check("self", Health.self(), { tags: ["live", "ready", "startup"] })
    .check("memory", Health.memory(), { tags: ["health"], critical: false, cacheMs: 1000 })
    .expose();

app.post("/orders", () => {
    ordersCreated.inc({ route: "/orders" });
    return Results.accepted({ accepted: true });
});

app.management({
    path: "/_sloppy",
    protect: (ctx) => ctx.request.headers.get("x-ops-key") === "local-dev-key",
});

export { app };
