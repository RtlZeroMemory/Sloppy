import { Cache, Results, Sloppy } from "sloppy";

const app = Sloppy.create();

function createOutputCache() {
    return Cache.memory("default", { maxEntries: 1000, ttlMs: 30000 });
}

app.services.addSingleton("cache.default", () => createOutputCache());

let productCalls = 0;
app.get("/products", (ctx) => {
    productCalls += 1;
    const calls = productCalls;
    const category = ctx.query.category ?? "all";
    const page = Number(ctx.query.page ?? 1);
    return Results.json({
        category,
        page,
        calls,
    });
}).outputCache({
    ttlMs: 30000,
    varyByQuery: ["category", "page"],
    tags: ["products"],
});

app.get("/assets/config.json", () => Results.json({ version: 1 }))
    .cacheHeaders({
        cacheControl: "public, max-age=60",
        vary: ["Accept-Encoding"],
        etag: true,
    });

export default app;
