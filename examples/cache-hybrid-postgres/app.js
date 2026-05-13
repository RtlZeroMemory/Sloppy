import { Cache, Results, Sloppy, data } from "sloppy";

const app = Sloppy.create();

app.services.addSingleton("data.main", () => data.postgres.open({
    connectionString: app.config.require("Sloppy:Providers:postgres:main:connectionString"),
}));

app.services.addSingleton("cache.main", (scope) => Cache.hybrid("main", {
    memory: Cache.memory({ maxEntries: 10000, ttlMs: 10000 }),
    distributed: Cache.postgres(scope.get("data.main"), {
        namespace: "products",
        ttlMs: 60000,
    }),
}));

app.get("/products/{id}", async (ctx) => {
    const cache = ctx.services.get("cache.main");
    const product = await cache.getOrCreate(`products:${ctx.route.id}`, {
        ttlMs: 30000,
        tags: [`product:${ctx.route.id}`, "products"],
    }, async () => ({ id: ctx.route.id, source: "postgres" }));

    return Results.json(product);
});

export default app;
